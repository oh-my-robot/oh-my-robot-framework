/**
 * @file   hal_spi.c
 * @brief  SPI 框架实现（v2.0 — Linux message 模型）
 *
 * 核心 API：
 *   spi_transfer()        同步传输整条 SpiMessage
 *   spi_transfer_async()  异步传输整条 SpiMessage
 *   spi_write/read/write_then_read()  便利层
 *
 */

#include "drivers/peripheral/spi/pal_spi_dev.h"

#include "osal/osal_core.h"
#include "osal/osal_priority.h"
#include <stddef.h>
#include <string.h>

/*===========================================================================
 * 常量
 *===========================================================================*/

#define SPI_ASYNC_WQ_STACK_DEPTH 1024U
#define SPI_ASYNC_WQ_PRIORITY (OSAL_PRIO_ABOVE_NORMAL_BASE + 0U)

/*===========================================================================
 * 总线注册表（全局链表 + 关中断保护）
 *
 * register/unregister/get 的链表操作耗时几个指针赋值，关中断（微秒级）
 * 比互斥锁更轻量，且不依赖 OS 初始化状态。
 *===========================================================================*/

static ListHead gSpiBusList = LIST_HEAD_INIT(gSpiBusList);

SpiBus *spi_bus_get(uint8_t idx)
{
    SpiBus *found = NULL;
    OsalIrqIsrState key;
    osal_irq_lock(&key);

    uint8_t n = 0U;
    ListHead *pos;
    LIST_FOR_EACH(pos, &gSpiBusList)
    {
        SpiBus *bus = LIST_ENTRY(pos, SpiBus, busNode);
        if (n == idx)
        {
            found = bus;
            break;
        }
        n++;
    }

    osal_irq_unlock(key);
    return found;
}

/*===========================================================================
 * 内部辅助 — 锁
 *===========================================================================*/

static inline OmRet spi_bus_lock(SpiBus *bus)
{
    OsalStatus st = osal_mutex_lock(bus->lock, OSAL_WAIT_FOREVER);
    return (st == OSAL_OK) ? OM_OK : OM_ERR_IO;
}

static inline OmRet spi_bus_unlock(SpiBus *bus)
{
    OsalStatus st = osal_mutex_unlock(bus->lock);
    return (st == OSAL_OK) ? OM_OK : OM_ERR_IO;
}

/*===========================================================================
 * 内部辅助 — 配置缓存
 *===========================================================================*/

static OmRet spi_ensure_configured(SpiBus *bus, HalSpiDevice *dev)
{
    if (bus->lastCfgDev == dev)
        return OM_OK;

    OmRet ret = bus->ops->configure(bus, &dev->cfg);
    if (ret == OM_OK)
        bus->lastCfgDev = dev;
    return ret;
}

/*===========================================================================
 * 内部辅助 — 动态超时计算
 *
 * 公式: ceil(len × 8000 / actualHz) + overheadMs
 *
 * actualHz 由 BSP configure() 写入 bus->actualHz（分频后的真实 SCLK），
 * 框架用实际频率而非请求频率计算超时，避免 BR 离散档位导致低估。
 *
 * 本函数仅计算"传输本身所需的时间"（纯数学），不涉及 OS 调度精度容错——
 * 调度层（completion 后端）自己保证等待不短于本函数返回值。
 *===========================================================================*/

static uint32_t spi_calc_timeout_ms(size_t len, uint32_t actual_hz, uint32_t overhead_ms)
{
    if (len > 0xFFFFFFFFUL / 8000U)
        return 0xFFFFFFFFUL;

    /* 向上取整：避免 32*8000/1e6 = 0.256ms 被截断为 0 */
    uint32_t t = ((uint32_t)(len * 8000U) + actual_hz - 1U) / actual_hz;

    if (t > 0xFFFFFFFFUL - (uint32_t)overhead_ms)
        return 0xFFFFFFFFUL;

    return t + overhead_ms;
}

/*===========================================================================
 * 内部辅助 — 单段硬件传输
 *
 * busy=1 必须在 transferOne 之前设置：DMA 可能在 transferOne 内部同步完成，
 * ISR 在 transferOne 返回前就已经触发，若 busy 还在 0 则 ISR 会被丢弃。
 *
 * 调用者持有锁、负责 CS。
 *===========================================================================*/

static OmRet spi_do_transfer_one(SpiBus *bus, const uint8_t *tx, uint8_t *rx,
                                 size_t len, size_t *transferred_out,
                                 uint32_t timeout_ms)
{
    bus->busy = 1;

    OmRet ret = bus->ops->transferOne(bus, tx, rx, len);
    if (ret != OM_OK)
    {
        bus->busy = 0;
        return ret;
    }

    ret = completion_wait(&bus->transferDone, timeout_ms);
    if (ret == OM_ERR_TIMEOUT)
    {
        OsalIrqIsrState k;
        osal_irq_lock(&k);
        bus->busy = 0;
        osal_irq_unlock(k);
        completion_wait(&bus->transferDone, 0U);
        *transferred_out = 0U;
        return OM_ERR_TIMEOUT;
    }

    bus->busy = 0;
    *transferred_out = bus->lastTransferred;
    return bus->lastStatus;
}

/*===========================================================================
 * 前向声明
 *===========================================================================*/

static void spi_async_worker_func(Work *work);

/*===========================================================================
 * SpiBus 生命周期
 *===========================================================================*/

OmRet spi_bus_register(SpiBus *bus, void *hw_private,
                       SpiControllerOps *ops)
{
    if (!bus || !ops)
        return OM_ERR_NULL;

    memset(bus, 0, sizeof(*bus));
    bus->hwPrivate = hw_private;
    bus->ops = ops;
    init_list_head(&bus->deviceList);

    OmRet ret;

    OsalMutex *mtx;
    if (osal_mutex_create(&mtx) != OSAL_OK)
        return OM_ERR_NO_MEM;
    bus->lock = mtx;

    ret = completion_init(&bus->transferDone);
    if (ret != OM_OK)
    {
        osal_mutex_delete(bus->lock);
        bus->lock = NULL;
        return ret;
    }

    WorkqueueConfig wq_cfg = {
        .name = "spi_async",
        .stack_depth = SPI_ASYNC_WQ_STACK_DEPTH,
        .priority = SPI_ASYNC_WQ_PRIORITY,
    };
    ret = workqueue_init(&bus->asyncWq, &wq_cfg);
    if (ret != OM_OK)
    {
        completion_deinit(&bus->transferDone);
        osal_mutex_delete(bus->lock);
        bus->lock = NULL;
        return ret;
    }
    ret = workqueue_start(&bus->asyncWq);
    if (ret != OM_OK)
    {
        workqueue_deinit(&bus->asyncWq);
        completion_deinit(&bus->transferDone);
        osal_mutex_delete(bus->lock);
        bus->lock = NULL;
        return ret;
    }

    {
        OsalIrqIsrState key;
        osal_irq_lock(&key);
        init_list_head(&bus->busNode);
        list_add_tail(&bus->busNode, &gSpiBusList);
        osal_irq_unlock(key);
    }

    return OM_OK;
}

void spi_bus_unregister(SpiBus *bus)
{
    if (!bus)
        return;

    OsalIrqIsrState key;
    osal_irq_lock(&key);
    list_del(&bus->busNode);
    osal_irq_unlock(key);
}

void spi_bus_deinit(SpiBus *bus)
{
    if (!bus || bus->deviceCount > 0U)
        return;

    spi_bus_lock(bus);

    workqueue_stop(&bus->asyncWq);
    workqueue_deinit(&bus->asyncWq);
    completion_deinit(&bus->transferDone);

    /* 先取走 mutex 指针再置 NULL，防止 unlock→delete 间隙被其他线程抢锁 */
    OsalMutex *mtx = bus->lock;
    bus->lock = NULL;
    osal_mutex_unlock(mtx);
    osal_mutex_delete(mtx);

    memset(bus, 0, sizeof(*bus));
}

/*===========================================================================
 * 设备挂载 / 移除
 *===========================================================================*/

OmRet spi_device_attach(uint8_t busIdx, HalSpiDevice *dev,
                        const char *name, const SpiDeviceCfg *cfg)
{
    SpiBus *bus = spi_bus_get(busIdx);
    if (!bus || !dev || !name || !cfg)
        return OM_ERR_NULL;

    if (cfg->maxHz == 0U)
        return OM_ERR_INVALID_ARG;

    if (dev->bus) // 已挂载
        return OM_ERR_ALREADY;

    memset(dev, 0, sizeof(*dev));
    dev->cfg = *cfg;

    if (cfg->csSpec.controller != NULL)
    {
        OmRet pin_ret = gpio_pin_get(&cfg->csSpec, &dev->cs);
        if (pin_ret != OM_OK)
            return pin_ret;
    }

    static const DevInterface g_spi_dev_interface = {
        .init = spi_dev_init,
        .open = spi_dev_open,
        .close = spi_dev_close,
        .read = spi_dev_read,
        .write = spi_dev_write,
        .control = spi_dev_control,
    };
    dev->parent.interface = (DevInterface *)&g_spi_dev_interface;

    OmRet ret = spi_bus_lock(bus);
    if (ret != OM_OK)
        return ret;

    /* CS 线冲突检测：遍历 bus 上已挂载设备，同一 CS 线只能挂载一个设备 */
    {
        HalSpiDevice *iter;
        LIST_FOR_EACH_ENTRY(iter, &bus->deviceList, busNode)
        {
            bool cs_conflict = false;
            if (cfg->csSpec.controller != NULL && iter->cfg.csSpec.controller != NULL)
            {
                cs_conflict = (strcmp(cfg->csSpec.controller,
                                      iter->cfg.csSpec.controller) == 0 &&
                               cfg->csSpec.offset == iter->cfg.csSpec.offset);
            }
            else if (cfg->csSpec.controller == NULL && iter->cfg.csSpec.controller == NULL)
            {
                cs_conflict = (cfg->csSpec.offset == iter->cfg.csSpec.offset);
            }
            if (cs_conflict)
            {
                spi_bus_unlock(bus);
                return OM_ERR_ALREADY;
            }
        }
    }

    ret = device_register(&dev->parent, (char *)name, 0U);
    if (ret == OM_OK)
    {
        dev->bus = bus;
        bus->deviceCount++;
        list_add_tail(&dev->busNode, &bus->deviceList);
    }

    spi_bus_unlock(bus);
    return ret;
}

void spi_device_detach(HalSpiDevice *dev)
{
    if (!dev || !dev->bus)
        return;

    SpiBus *bus = dev->bus;

    spi_bus_lock(bus);

    if (bus->lastCfgDev == dev)
        bus->lastCfgDev = NULL;

    if (bus->deviceCount > 0U)
    {
        bus->deviceCount--;
        list_del(&dev->busNode);
    }

    spi_bus_unlock(bus);

    /* 必须从全局设备表摘除，否则下次 spi_device_attach 的 memset 会把
     * 仍在 g_dev_list 链表中的 list 节点清零，device_find 遍历到该节点时
     * deref NULL 指针触发 HardFault。 */
    device_unregister(&dev->parent);

    dev->bus = NULL;
    dev->suspended = 0;
}

/*===========================================================================
 * 标准 Device 接口
 *===========================================================================*/

OmRet spi_dev_init(Device *dev)
{
    (void)dev;
    return OM_OK;
}

OmRet spi_dev_open(Device *dev, uint32_t oparam)
{
    (void)dev;
    (void)oparam;
    return OM_OK;
}

OmRet spi_dev_close(Device *dev)
{
    (void)dev;
    return OM_OK;
}

size_t spi_dev_read(Device *dev, void *ctrl_info, void *data, size_t len)
{
    if (!dev || !data || len == 0U)
        return 0U;

    HalSpiDevice *spi_dev = (HalSpiDevice *)dev;

    if (ctrl_info)
    {
        OmRet ret = spi_write_then_read(spi_dev,
                                        (const uint8_t *)ctrl_info, 1U,
                                        (uint8_t *)data, len);
        return (ret == OM_OK) ? len : 0U;
    }

    OmRet ret = spi_read(spi_dev, (uint8_t *)data, len);
    return (ret == OM_OK) ? len : 0U;
}

size_t spi_dev_write(Device *dev, void *ctrl_info, void *data, size_t len)
{
    if (!dev || !data || len == 0U)
        return 0U;

    HalSpiDevice *spi_dev = (HalSpiDevice *)dev;

    if (ctrl_info)
    {
        SpiTransfer xfers[2];
        xfers[0].txBuf = (const uint8_t *)ctrl_info;
        xfers[0].rxBuf = NULL;
        xfers[0].len = 1U;
        xfers[0].flags = SPI_XFER_FLAG_CS_HOLD;
        xfers[0].speedHz = 0;
        xfers[0].bitsPerWord = 0;

        xfers[1].txBuf = (const uint8_t *)data;
        xfers[1].rxBuf = NULL;
        xfers[1].len = len;
        xfers[1].flags = 0;
        xfers[1].speedHz = 0;
        xfers[1].bitsPerWord = 0;

        SpiMessage msg = {.transfers = xfers, .count = 2};
        OmRet ret = spi_transfer(spi_dev, &msg);
        return (ret == OM_OK) ? len : 0U;
    }

    OmRet ret = spi_write(spi_dev, (const uint8_t *)data, len);
    return (ret == OM_OK) ? len : 0U;
}

OmRet spi_dev_control(Device *dev, size_t cmd, void *arg)
{
    if (!dev)
        return OM_ERR_NULL;

    HalSpiDevice *spi_dev = (HalSpiDevice *)dev;

    switch (cmd)
    {
    case SPI_CMD_SET_CFG: {
        if (!arg)
            return OM_ERR_INVALID_ARG;
        SpiDeviceCfg *new_cfg = (SpiDeviceCfg *)arg;
        if (new_cfg->maxHz == 0U)
            return OM_ERR_INVALID_ARG;

        OmRet ret = spi_bus_lock(spi_dev->bus);
        if (ret != OM_OK)
            return ret;

        if (spi_dev->bus->busy)
        {
            spi_bus_unlock(spi_dev->bus);
            return OM_ERR_BUSY;
        }

        spi_dev->cfg = *new_cfg;
        if (new_cfg->csSpec.controller != NULL)
        {
            ret = gpio_pin_get(&new_cfg->csSpec, &spi_dev->cs);
            if (ret != OM_OK)
            {
                spi_bus_unlock(spi_dev->bus);
                return ret;
            }
        }
        else
        {
            memset(&spi_dev->cs, 0, sizeof(spi_dev->cs));
        }

        ret = spi_ensure_configured(spi_dev->bus, spi_dev);
        spi_bus_unlock(spi_dev->bus);
        return ret;
    }
    case SPI_CMD_GET_CFG: {
        if (!arg)
            return OM_ERR_INVALID_ARG;
        *(SpiDeviceCfg *)arg = spi_dev->cfg;
        return OM_OK;
    }
    case SPI_CMD_SUSPEND:
        return spi_device_suspend(spi_dev);

    case SPI_CMD_RESUME:
        return spi_device_resume(spi_dev);

    case SPI_CMD_ABORT: {
        OmRet ret = spi_bus_lock(spi_dev->bus);
        if (ret != OM_OK)
            return ret;
        ret = spi_dev->bus->ops->control(spi_dev->bus, SPI_CMD_ABORT, NULL);
        spi_bus_unlock(spi_dev->bus);
        return ret;
    }

    default:
        return OM_ERR_NOT_SUPPORTED;
    }
}

/*===========================================================================
 * 核心 API — spi_transfer（同步传输整条 message）
 *===========================================================================*/

OmRet spi_transfer(HalSpiDevice *dev, SpiMessage *msg)
{
    if (!dev || !dev->bus || !msg)
        return OM_ERR_NULL;
    if (!msg->transfers || msg->count == 0U)
        return OM_ERR_INVALID_ARG;

    SpiBus *bus = dev->bus;
    msg->status = OM_OK;
    msg->transferred = 0U;

    /* 前置检查（无锁） */
    if (dev->suspended)
        return OM_ERR_SPI_DEV_SUSPENDED;

    OmRet ret = spi_bus_lock(bus);
    if (ret != OM_OK)
        return ret;

    /* 持锁后重检 */
    if (dev->suspended)
    {
        spi_bus_unlock(bus);
        return OM_ERR_SPI_DEV_SUSPENDED;
    }

    if (bus->busy)
    {
        spi_bus_unlock(bus);
        return OM_ERR_BUSY;
    }

    ret = spi_ensure_configured(bus, dev);
    if (ret != OM_OK)
    {
        spi_bus_unlock(bus);
        return ret;
    }

    /* 遍历所有 transfer */
    bool cs_held = false;

    for (size_t i = 0U; i < msg->count; i++)
    {
        SpiTransfer *xfer = &msg->transfers[i];

        if (xfer->len == 0U)
            continue;
        if (!xfer->txBuf && !xfer->rxBuf)
            continue;

        /* CS assert（仅首次或上次已释放时） */
        if (!cs_held)
        {
            spi_cs_assert_dev(dev);
            cs_held = true;
        }

        uint32_t timeout = spi_calc_timeout_ms(xfer->len, bus->actualHz,
                                               dev->cfg.transferOverheadMs);
        size_t transferred;
        ret = spi_do_transfer_one(bus, xfer->txBuf, xfer->rxBuf,
                                  xfer->len, &transferred, timeout);
        msg->transferred += transferred;

        if (ret != OM_OK)
        {
            spi_cs_deassert_dev(dev);
            msg->status = ret;
            spi_bus_unlock(bus);
            return ret;
        }

        /* CS management */
        if (xfer->flags & SPI_XFER_FLAG_CS_HOLD)
            cs_held = true;
        else
        {
            spi_cs_deassert_dev(dev);
            cs_held = false;
        }
    }

    msg->status = OM_OK;
    spi_bus_unlock(bus);
    return OM_OK;
}

/*===========================================================================
 * 公共 CS 控制（供 CS_HOLD 结束后的手动释放）
 *===========================================================================*/

void spi_cs_deassert(HalSpiDevice *dev)
{
    if (!dev || !dev->bus)
        return;

    spi_bus_lock(dev->bus);
    spi_cs_deassert_dev(dev);
    spi_bus_unlock(dev->bus);
}

/*===========================================================================
 * 便利层
 *===========================================================================*/

OmRet spi_write(HalSpiDevice *dev, const uint8_t *buf, size_t len)
{
    SpiTransfer xfer = {
        .txBuf = buf,
        .rxBuf = NULL,
        .len = len,
        .flags = 0,
        .speedHz = 0,
        .bitsPerWord = 0,
    };
    SpiMessage msg = {.transfers = &xfer, .count = 1};
    return spi_transfer(dev, &msg);
}

OmRet spi_read(HalSpiDevice *dev, uint8_t *buf, size_t len)
{
    SpiTransfer xfer = {
        .txBuf = NULL,
        .rxBuf = buf,
        .len = len,
        .flags = 0,
        .speedHz = 0,
        .bitsPerWord = 0,
    };
    SpiMessage msg = {.transfers = &xfer, .count = 1};
    return spi_transfer(dev, &msg);
}

OmRet spi_write_then_read(HalSpiDevice *dev,
                          const uint8_t *tx, size_t tx_len,
                          uint8_t *rx, size_t rx_len)
{
    SpiTransfer xfers[2];

    xfers[0].txBuf = tx;
    xfers[0].rxBuf = NULL;
    xfers[0].len = tx_len;
    xfers[0].flags = SPI_XFER_FLAG_CS_HOLD;
    xfers[0].speedHz = 0;
    xfers[0].bitsPerWord = 0;

    xfers[1].txBuf = NULL;
    xfers[1].rxBuf = rx;
    xfers[1].len = rx_len;
    xfers[1].flags = 0;
    xfers[1].speedHz = 0;
    xfers[1].bitsPerWord = 0;

    SpiMessage msg = {.transfers = xfers, .count = 2};
    return spi_transfer(dev, &msg);
}

/*===========================================================================
 * 异步传输
 *===========================================================================*/

OmRet spi_transfer_async(HalSpiDevice *dev, SpiMessage *msg,
                         void (*callback)(void *param, SpiMessage *msg),
                         void *param)
{
    if (!dev || !dev->bus || !msg || !callback)
        return OM_ERR_NULL;
    if (!msg->transfers || msg->count == 0U)
        return OM_ERR_INVALID_ARG;

    SpiBus *bus = dev->bus;

    if (dev->suspended)
        return OM_ERR_SPI_DEV_SUSPENDED;

    /* irq_lock 临界区：仅保护 asyncBusy 竞态 + epoch 快照，
     * 不持有 bus->lock，避免受同步传输阻塞导致异步退化为同步。 */
    OsalIrqIsrState k;
    osal_irq_lock(&k);
    if (dev->asyncBusy)
    {
        osal_irq_unlock(k);
        return OM_ERR_BUSY;
    }
    dev->asyncBusy = 1;
    dev->asyncEpoch = ++bus->asyncEpoch;
    osal_irq_unlock(k);

    /* 独占 slot，无竞争安全填充 */
    dev->asyncMsg = msg;
    dev->asyncCb = callback;
    dev->asyncCbParam = param;
    dev->asyncCsHeld = false;

    msg->status = OM_OK;
    msg->transferred = 0U;

    work_init(&dev->asyncWork, spi_async_worker_func, NULL);

    OmRet ret = workqueue_enqueue(&bus->asyncWq, &dev->asyncWork);
    if (ret != OM_OK)
    {
        OsalIrqIsrState k2;
        osal_irq_lock(&k2);
        dev->asyncBusy = 0;
        osal_irq_unlock(k2);
        return ret;
    }
    return OM_OK;
}

/*===========================================================================
 * 异步 worker 回调（workqueue worker 线程中执行）
 *===========================================================================*/

static void spi_async_worker_func(Work *work)
{
    /* Work 嵌入在 HalSpiDevice.asyncWork 中，通过 container_of 反查设备。
     * bus 指针在 enqueue 时快照，worker 持锁后二次检查 dev->bus 是否为 zombie。 */
    HalSpiDevice *dev = container_of(work, HalSpiDevice, asyncWork);
    SpiBus *bus = dev->bus;
    SpiMessage *msg = dev->asyncMsg;
    OmRet ret;
    bool locked = false;

    /* bus 可能已被 detach 置 NULL（注：bus 自身是静态分配，指针有效，仅 dev->bus 成 zombie） */
    if (!bus)
    {
        msg->status = OM_ERR_NOT_SUPPORTED;
        goto done;
    }

    ret = spi_bus_lock(bus);
    if (ret != OM_OK)
    {
        msg->status = ret;
        goto done;
    }
    locked = true;

    /* 持锁后权威检查：设备是否在排队期间被 detach */
    if (dev->bus != bus)
    {
        msg->status = OM_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

    if (dev->suspended)
    {
        msg->status = OM_ERR_SPI_DEV_SUSPENDED;
        goto cleanup;
    }

    ret = spi_ensure_configured(bus, dev);
    if (ret != OM_OK)
    {
        msg->status = ret;
        goto cleanup;
    }

    for (size_t i = 0U; i < msg->count; i++)
    {
        SpiTransfer *xfer = &msg->transfers[i];

        if (xfer->len == 0U)
            continue;
        if (!xfer->txBuf && !xfer->rxBuf)
            continue;

        if (!dev->asyncCsHeld)
        {
            spi_cs_assert_dev(dev);
            dev->asyncCsHeld = true;
        }

        const uint8_t *tx_src = xfer->txBuf;

        uint32_t timeout = spi_calc_timeout_ms(xfer->len, bus->actualHz,
                                               dev->cfg.transferOverheadMs);

        bus->busy = 1;
        ret = bus->ops->transferOne(bus, tx_src, xfer->rxBuf, xfer->len);
        if (ret != OM_OK)
        {
            bus->busy = 0;
            msg->status = ret;
            goto cleanup;
        }

        spi_bus_unlock(bus);
        locked = false;

        ret = completion_wait(&bus->transferDone, timeout);
        if (ret == OM_ERR_TIMEOUT)
        {
            OsalIrqIsrState k;
            osal_irq_lock(&k);
            bus->busy = 0;
            osal_irq_unlock(k);
            completion_wait(&bus->transferDone, 0U);
            spi_bus_lock(bus);
            locked = true;
            msg->status = OM_ERR_SPI_TRANSFER_TIMEOUT;
            goto cleanup;
        }

        msg->transferred += bus->lastTransferred;

        if (bus->lastStatus != OM_OK)
        {
            msg->status = bus->lastStatus;
            bus->busy = 0;
            goto cleanup;
        }

        if (xfer->flags & SPI_XFER_FLAG_CS_HOLD)
        {
            dev->asyncCsHeld = true;
        }
        else
        {
            spi_bus_lock(bus);
            spi_cs_deassert_dev(dev);
            spi_bus_unlock(bus);
            dev->asyncCsHeld = false;
        }

        if (i + 1U < msg->count)
        {
            ret = spi_bus_lock(bus);
            if (ret != OM_OK)
            {
                msg->status = ret;
                goto done;
            }
            locked = true;
            if (dev->suspended)
            {
                msg->status = OM_ERR_SPI_DEV_SUSPENDED;
                goto cleanup;
            }
            /* 重检：设备可能在上一段 DMA 等待期间被 detach */
            if (dev->bus != bus)
            {
                msg->status = OM_ERR_NOT_SUPPORTED;
                goto cleanup;
            }
        }
        else
        {
            spi_bus_lock(bus);
            bus->busy = 0;
            spi_bus_unlock(bus);
        }
    }

    msg->status = OM_OK;
    goto done;

cleanup:
    if (dev->asyncCsHeld)
    {
        if (!locked)
        {
            spi_bus_lock(bus);
            locked = true;
        }
        spi_cs_deassert_dev(dev);
    }
    if (locked)
        spi_bus_unlock(bus);

done:
    /* 先释放 async slot，再调 callback。
     * 这样 callback 可以立即重发 spi_transfer_async() 而不被 asyncBusy 拦下。 */
    {
        OsalIrqIsrState k;
        osal_irq_lock(&k);
        dev->asyncBusy = 0;
        osal_irq_unlock(k);
    }
    dev->asyncCb(dev->asyncCbParam, msg);
}

/*===========================================================================
 * 低功耗挂起 / 恢复
 *===========================================================================*/

OmRet spi_device_suspend(HalSpiDevice *dev)
{
    if (!dev || !dev->bus)
        return OM_ERR_NULL;

    SpiBus *bus = dev->bus;

    OmRet ret = spi_bus_lock(bus);
    if (ret != OM_OK)
        return ret;

    if (dev->suspended)
    {
        spi_bus_unlock(bus);
        return OM_OK;
    }

    dev->suspended = 1;
    bus->suspendedCount++;

    if (bus->suspendedCount == bus->deviceCount)
        bus->ops->control(bus, SPI_CMD_SUSPEND, NULL);

    spi_bus_unlock(bus);
    return OM_OK;
}

OmRet spi_device_resume(HalSpiDevice *dev)
{
    if (!dev || !dev->bus)
        return OM_ERR_NULL;

    SpiBus *bus = dev->bus;

    OmRet ret = spi_bus_lock(bus);
    if (ret != OM_OK)
        return ret;

    if (!dev->suspended)
    {
        spi_bus_unlock(bus);
        return OM_OK;
    }

    if (bus->suspendedCount == bus->deviceCount)
        bus->ops->control(bus, SPI_CMD_RESUME, NULL);

    bus->suspendedCount--;
    dev->suspended = 0;

    bus->lastCfgDev = NULL;

    spi_bus_unlock(bus);
    return OM_OK;
}

/*===========================================================================
 * 框架 ISR 入口（由 BSP DMA / 中断处理函数调用）
 *===========================================================================*/

void hal_spi_isr(SpiBus *bus, OmRet status, size_t transferred)
{
    if (!bus)
        return;

    /* busy 在 transferOne 之前置位，因此 DMA 提前完成的 ISR 不会丢失。
     * 此处防御超时路径已将 busy 清零后仍然到达的迟到 ISR。 */
    if (!bus->busy)
        return;

    bus->lastTransferred = transferred;
    bus->lastStatus = status;
    completion_done(&bus->transferDone);
}

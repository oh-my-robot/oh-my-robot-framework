#include "drivers/peripheral/spi/pal_spi_dev.h"

#include "osal/osal_core.h"
#include <stddef.h>
#include <string.h>

/*===========================================================================
 * 常量
 *===========================================================================*/

#define SPI_ASYNC_WQ_STACK_DEPTH     1024U
#define SPI_ASYNC_WQ_PRIORITY        4U

/*===========================================================================
 * 内部辅助 — 锁
 *===========================================================================*/

static inline OmRet spi_bus_lock(SpiBus *bus)
{
    OsalStatus st = osal_mutex_lock(bus->lock, OSAL_WAIT_FOREVER);
    return (st == OSAL_OK) ? OM_OK : OM_ERROR;
}

static inline OmRet spi_bus_unlock(SpiBus *bus)
{
    OsalStatus st = osal_mutex_unlock(bus->lock);
    return (st == OSAL_OK) ? OM_OK : OM_ERROR;
}

/*===========================================================================
 * 内部辅助 — 配置缓存
 *===========================================================================*/

static OmRet spi_bus_ensure_configured(SpiBus *bus, HalSpiDevice *dev)
{
    if (bus->cachedDevice == dev)
        return OM_OK;

    OmRet ret = bus->interface->configure(bus, &dev->cfg);
    if (ret == OM_OK)
        bus->cachedDevice = dev;
    return ret;
}

/*===========================================================================
 * 内部辅助 — 动态超时计算
 *
 * 公式: (len × 8000 / maxHz) + overheadMs
 *
 * 第一项 (len × 8000 / maxHz) 是纯 SCLK 总线上走完 len 字节的理论最短时间:
 *   len      发送/接收字节数
 *   maxHz    SCLK 时钟频率 (Hz)，来自 cfg.maxHz
 *   8000     = 8 bits/byte × 1000 ms/s，量纲转换常数
 *   推导:    T = (len × 8 bit) / (maxHz Hz) × 1000 ms/s
 *           T = len × 8000 / maxHz  (ms)
 *
 *   例: 32 字节 @ 10 MHz → 32×8000/10⁷ = 0.0256 ms
 *
 * 第二项 overheadMs 是 per-device 软件附加余量，补偿:
 *   - BSP transfer() 启动延迟（DMA 配置 / 寄存器写入）
 *   - ISR 延迟（NVIC 仲裁 + 上下文保存）
 *   - RTOS 调度抖动（worker 被唤醒到真正运行）
 *   调用者通过 cfg.transferOverheadMs 按设备特性配置，可设为 0。
 *
 *   例: IMU 寄存器读写通常 < 1ms → overheadMs=2ms 足够
 *       Flash 页擦除需要等待数百 ms → overheadMs 应覆盖擦除时长
 *       LCD 40MHz DMA 帧推送几乎无软件延迟 → overheadMs=0
 *===========================================================================*/

static uint32_t spi_calc_timeout_ms(size_t len, uint32_t max_hz, uint32_t overhead_ms)
{
    /* 乘法溢出保护：len 过大时直接返回最大超时 */
    if (len > 0xFFFFFFFFUL / 8000U)
        return 0xFFFFFFFFUL;

    uint32_t t = (uint32_t)(len * 8000U) / max_hz;

    /* 加法溢出保护：软件余量不会把结果推过上限 */
    if (t > 0xFFFFFFFFUL - (uint32_t)overhead_ms)
        return 0xFFFFFFFFUL;

    return t + overhead_ms;
}

/*===========================================================================
 * 内部辅助 — 同步传输核心（调用者持有锁、负责 CS）
 *
 * 返回 OM_OK 后 xfer->transferred 已填入实际传输字节数。
 * 返回错误码时 CS 仍由调用者负责。
 *===========================================================================*/

static OmRet spi_do_transfer(SpiBus *bus, const uint8_t *tx, uint8_t *rx, size_t len, size_t *transferred_out, uint32_t timeout_ms)
{
    OmRet ret = bus->interface->transfer(bus, tx, rx, len);
    if (ret != OM_OK)
        return ret;

    bus->busy = 1;

    ret = completion_wait(&bus->transferDone, timeout_ms);
    if (ret == OM_ERROR_TIMEOUT) {
        OsalIrqIsrState k;
        osal_irq_lock(&k);
        bus->busy = 0;
        osal_irq_unlock(k);
        completion_wait(&bus->transferDone, 0U);
        *transferred_out = 0U;
        return OM_ERROR_TIMEOUT;
    }

    bus->busy = 0;
    *transferred_out = bus->lastTransferred;
    return bus->lastStatus;
}

/*===========================================================================
 * 内部辅助 — DoubleBuf 开关
 *===========================================================================*/

static inline bool spi_dbuf_enabled(SpiBus *bus)
{
    return dbuf_capacity(&bus->txDbuf) > 0U;
}

/*===========================================================================
 * 前向声明
 *===========================================================================*/

static void spi_async_worker_func(Work *work);

/*===========================================================================
 * SpiBus 生命周期
 *===========================================================================*/

OmRet hal_spi_bus_register(SpiBus *bus, void *hwPrivate,
                           SpiInterface *interface,
                           size_t dbuf_page_size)
{
    if (!bus || !interface)
        return OM_ERROR_PARAM;

    memset(bus, 0, sizeof(*bus));
    bus->hwPrivate = hwPrivate;
    bus->interface = interface;

    OmRet ret;

    /* 创建互斥锁 */
    OsalMutex *mtx;
    if (osal_mutex_create(&mtx) != OSAL_OK)
        return OM_ERROR_MEMORY;
    bus->lock = mtx;

    /* 初始化同步完成信号 */
    ret = completion_init(&bus->transferDone);
    if (ret != OM_OK) {
        osal_mutex_delete(bus->lock);
        bus->lock = NULL;
        return ret;
    }

    /* 可选双缓冲 */
    if (dbuf_page_size > 0U) {
        if (!dbuf_alloc(&bus->txDbuf, dbuf_page_size, NULL)) {
            completion_deinit(&bus->transferDone);
            osal_mutex_delete(bus->lock);
            bus->lock = NULL;
            return OM_ERROR_MEMORY;
        }
    }

    /* 自建 per-bus workqueue */
    WorkqueueConfig wqCfg = {
        .name        = "spi_async",
        .stack_depth = SPI_ASYNC_WQ_STACK_DEPTH,
        .priority    = SPI_ASYNC_WQ_PRIORITY,
    };
    ret = workqueue_init(&bus->asyncWq, &wqCfg);
    if (ret != OM_OK) {
        dbuf_free(&bus->txDbuf, NULL);
        completion_deinit(&bus->transferDone);
        osal_mutex_delete(bus->lock);
        bus->lock = NULL;
        return ret;
    }
    ret = workqueue_start(&bus->asyncWq);
    if (ret != OM_OK) {
        workqueue_deinit(&bus->asyncWq);
        dbuf_free(&bus->txDbuf, NULL);
        completion_deinit(&bus->transferDone);
        osal_mutex_delete(bus->lock);
        bus->lock = NULL;
        return ret;
    }

    return OM_OK;
}

void hal_spi_bus_deinit(SpiBus *bus)
{
    if (!bus)
        return;

    workqueue_stop(&bus->asyncWq);
    workqueue_deinit(&bus->asyncWq);
    dbuf_free(&bus->txDbuf, NULL);
    completion_deinit(&bus->transferDone);

    if (bus->lock) {
        osal_mutex_delete(bus->lock);
        bus->lock = NULL;
    }

    memset(bus, 0, sizeof(*bus));
}

/*===========================================================================
 * 设备挂载 / 移除
 *===========================================================================*/

OmRet hal_spi_device_attach(SpiBus *bus, HalSpiDevice *dev,
                            const char *name, const SpiDeviceCfg *cfg)
{
    if (!bus || !dev || !name || !cfg)
        return OM_ERROR_PARAM;

    if (cfg->maxHz == 0U)
        return OM_ERROR_PARAM;

    memset(dev, 0, sizeof(*dev));
    dev->bus = bus;
    dev->cfg = *cfg;

    /* 解析 CS 引脚 */
    if (cfg->csSpec.controller != NULL)
        dev->cs = gpio_pin_get(&cfg->csSpec);

    /* 注册为标准 Device */
    static const DevInterface g_spiDevInterface = {
        .init    = hal_spi_dev_init,
        .open    = hal_spi_dev_open,
        .close   = hal_spi_dev_close,
        .read    = hal_spi_dev_read,
        .write   = hal_spi_dev_write,
        .control = hal_spi_dev_control,
    };
    dev->parent.interface = (DevInterface *)&g_spiDevInterface;

    OmRet ret = spi_bus_lock(bus);
    if (ret != OM_OK)
        return ret;

    ret = device_register(&dev->parent, (char *)name, 0U);
    if (ret == OM_OK)
        bus->deviceCount++;

    spi_bus_unlock(bus);
    return ret;
}

void hal_spi_device_detach(HalSpiDevice *dev)
{
    if (!dev || !dev->bus)
        return;

    SpiBus *bus = dev->bus;

    spi_bus_lock(bus);

    /* 清除配置缓存引用，避免下次传输误命中已移除的设备 */
    if (bus->cachedDevice == dev)
        bus->cachedDevice = NULL;

    if (bus->deviceCount > 0U)
        bus->deviceCount--;

    spi_bus_unlock(bus);

    dev->bus = NULL;
    dev->inTransaction = 0;
    dev->suspended = 0;
}

/*===========================================================================
 * 标准 Device 接口
 *===========================================================================*/

OmRet hal_spi_dev_init(Device *dev)
{
    (void)dev;
    return OM_OK;
}

OmRet hal_spi_dev_open(Device *dev, uint32_t oparam)
{
    (void)dev;
    (void)oparam;
    return OM_OK;
}

OmRet hal_spi_dev_close(Device *dev)
{
    (void)dev;
    return OM_OK;
}

size_t hal_spi_dev_read(Device *dev, void *ctrl_info, void *data, size_t len)
{
    if (!dev || !data || len == 0U)
        return 0U;

    HalSpiDevice *spiDev = (HalSpiDevice *)dev;

    if (ctrl_info) {
        /* 发 1 字节命令前缀，再收 len 字节 */
        SpiXfer xfer = {
            .txBuf = (const uint8_t *)ctrl_info,
            .rxBuf = (uint8_t *)data,
            .txLen = 1U,
            .rxLen = len,
        };
        if (hal_spi_write_then_read(spiDev, &xfer) != OM_OK)
            return 0U;
        return xfer.transferred > 1U ? xfer.transferred - 1U : 0U;
    }

    /* 纯收：发 dummy 收 len 字节 */
    SpiXfer xfer = {
        .txBuf = NULL,
        .rxBuf = (uint8_t *)data,
        .txLen = len,
        .rxLen = len,
    };
    if (hal_spi_transfer(spiDev, &xfer) != OM_OK)
        return 0U;
    return xfer.transferred;
}

size_t hal_spi_dev_write(Device *dev, void *ctrl_info, void *data, size_t len)
{
    if (!dev || !data || len == 0U)
        return 0U;

    HalSpiDevice *spiDev = (HalSpiDevice *)dev;

    if (ctrl_info) {
        /* 手动事务：发 1 字节命令 + len 字节数据，CS 保持低 */
        if (hal_spi_transaction_begin(spiDev) != OM_OK)
            return 0U;

        SpiXfer cmdXfer = {
            .txBuf = (const uint8_t *)ctrl_info,
            .txLen = 1U,
            .rxLen = 1U,
        };
        if (hal_spi_transaction_transfer(spiDev, &cmdXfer) != OM_OK) {
            hal_spi_transaction_end(spiDev);
            return 0U;
        }

        SpiXfer dataXfer = {
            .txBuf = (const uint8_t *)data,
            .txLen = len,
            .rxLen = len,
        };
        size_t written = 0U;
        if (hal_spi_transaction_transfer(spiDev, &dataXfer) == OM_OK)
            written = dataXfer.transferred;

        hal_spi_transaction_end(spiDev);
        return written;
    }

    /* 纯发：发送 data，丢弃接收 */
    SpiXfer xfer = {
        .txBuf = (const uint8_t *)data,
        .rxBuf = NULL,
        .txLen = len,
        .rxLen = len,
    };
    if (hal_spi_transfer(spiDev, &xfer) != OM_OK)
        return 0U;
    return xfer.transferred;
}

OmRet hal_spi_dev_control(Device *dev, size_t cmd, void *arg)
{
    if (!dev)
        return OM_ERROR_PARAM;

    HalSpiDevice *spiDev = (HalSpiDevice *)dev;

    switch (cmd) {
    case SPI_CMD_SET_CFG: {
        if (!arg)
            return OM_ERROR_PARAM;
        SpiDeviceCfg *newCfg = (SpiDeviceCfg *)arg;
        if (newCfg->maxHz == 0U)
            return OM_ERROR_PARAM;

        OmRet ret = spi_bus_lock(spiDev->bus);
        if (ret != OM_OK)
            return ret;

        spiDev->cfg = *newCfg;
        /* 解析新的 CS 引脚：GPIO→硬件CS 切换时需清除旧 GPIO 句柄 */
        if (newCfg->csSpec.controller != NULL)
            spiDev->cs = gpio_pin_get(&newCfg->csSpec);
        else
            memset(&spiDev->cs, 0, sizeof(spiDev->cs));

        ret = spi_bus_ensure_configured(spiDev->bus, spiDev);
        spi_bus_unlock(spiDev->bus);
        return ret;
    }
    case SPI_CMD_GET_CFG: {
        if (!arg)
            return OM_ERROR_PARAM;
        *(SpiDeviceCfg *)arg = spiDev->cfg;
        return OM_OK;
    }
    case SPI_CMD_SUSPEND:
        return hal_spi_device_suspend(spiDev);

    case SPI_CMD_RESUME:
        return hal_spi_device_resume(spiDev);

    case SPI_CMD_ABORT: {
        OmRet ret = spi_bus_lock(spiDev->bus);
        if (ret != OM_OK)
            return ret;
        ret = spiDev->bus->interface->control(spiDev->bus, SPI_CMD_ABORT, NULL);
        spi_bus_unlock(spiDev->bus);
        return ret;
    }

    default:
        return OM_ERROR_NOT_SUPPORT;
    }
}

/*===========================================================================
 * 同步全双工传输（自动 CS）
 *===========================================================================*/

OmRet hal_spi_transfer(HalSpiDevice *dev, SpiXfer *xfer)
{
    if (!dev || !dev->bus || !xfer)
        return OM_ERROR_PARAM;

    if (xfer->txLen != xfer->rxLen)
        return OM_ERROR_PARAM;

    size_t len = xfer->txLen;
    if (len == 0U)
        return OM_ERROR_PARAM;

    if (!xfer->txBuf && !xfer->rxBuf)
        return OM_ERROR_PARAM;

    SpiBus *bus = dev->bus;

    /* 前置检查（无锁） */
    if (dev->suspended)
        return (OmRet)ERR_SPI_DEV_SUSPENDED;

    OmRet ret = spi_bus_lock(bus);
    if (ret != OM_OK)
        return ret;

    /* 持锁后重检：防止锁前窗口被 suspend */
    if (dev->suspended) {
        spi_bus_unlock(bus);
        return (OmRet)ERR_SPI_DEV_SUSPENDED;
    }

    /* 拒绝嵌套：事务内应使用 transaction_transfer */
    if (dev->inTransaction) {
        spi_bus_unlock(bus);
        return (OmRet)ERR_SPI_CS_CONFLICT;
    }

    if (bus->busy) {
        spi_bus_unlock(bus);
        return (OmRet)ERR_SPI_BUSY;
    }

    ret = spi_bus_ensure_configured(bus, dev);
    if (ret != OM_OK) {
        spi_bus_unlock(bus);
        return ret;
    }

    spi_cs_assert(dev);

    /* 发起硬件传输、阻塞等待完成 */
    uint32_t timeout_ms = spi_calc_timeout_ms(len, dev->cfg.maxHz, dev->cfg.transferOverheadMs);
    ret = spi_do_transfer(bus, xfer->txBuf, xfer->rxBuf, len, &xfer->transferred, timeout_ms);

    spi_cs_deassert(dev);
    spi_bus_unlock(bus);
    return ret;
}

/*===========================================================================
 * 一问一答（单 CS 周期内先写命令再读数据）
 *===========================================================================*/

OmRet hal_spi_write_then_read(HalSpiDevice *dev, SpiXfer *xfer)
{
    if (!dev || !dev->bus || !xfer)
        return OM_ERROR_PARAM;

    if (xfer->txLen == 0U || xfer->rxLen == 0U)
        return OM_ERROR_PARAM;

    if (!xfer->txBuf || !xfer->rxBuf)
        return OM_ERROR_PARAM;

    SpiBus *bus = dev->bus;

    if (dev->suspended)
        return (OmRet)ERR_SPI_DEV_SUSPENDED;

    OmRet ret = spi_bus_lock(bus);
    if (ret != OM_OK)
        return ret;

    /* 持锁后重检：防止锁前窗口被 suspend */
    if (dev->suspended) {
        spi_bus_unlock(bus);
        return (OmRet)ERR_SPI_DEV_SUSPENDED;
    }

    if (dev->inTransaction) {
        spi_bus_unlock(bus);
        return (OmRet)ERR_SPI_CS_CONFLICT;
    }

    if (bus->busy) {
        spi_bus_unlock(bus);
        return (OmRet)ERR_SPI_BUSY;
    }

    ret = spi_bus_ensure_configured(bus, dev);
    if (ret != OM_OK) {
        spi_bus_unlock(bus);
        return ret;
    }

    spi_cs_assert(dev);

    /* 阶段1：发命令段，丢弃接收 */
    uint32_t cmd_timeout = spi_calc_timeout_ms(xfer->txLen, dev->cfg.maxHz, dev->cfg.transferOverheadMs);
    size_t cmd_transferred;
    ret = spi_do_transfer(bus, xfer->txBuf, NULL, xfer->txLen, &cmd_transferred, cmd_timeout);
    if (ret != OM_OK) {
        spi_cs_deassert(dev);
        spi_bus_unlock(bus);
        return ret;
    }

    /* 阶段2：发 dummy 收数据段 */
    uint32_t data_timeout = spi_calc_timeout_ms(xfer->rxLen, dev->cfg.maxHz, dev->cfg.transferOverheadMs);
    size_t data_transferred;
    ret = spi_do_transfer(bus, NULL, xfer->rxBuf, xfer->rxLen, &data_transferred, data_timeout);

    xfer->transferred = cmd_transferred + data_transferred;

    spi_cs_deassert(dev);
    spi_bus_unlock(bus);
    return ret;
}

/*===========================================================================
 * 异步传输（per-request 回调，workqueue 序列化）
 *===========================================================================*/

OmRet hal_spi_transfer_async(HalSpiDevice *dev, SpiAsyncRequest *req)
{
    if (!dev || !dev->bus || !req)
        return OM_ERROR_PARAM;

    if (!req->asyncCb)
        return OM_ERROR_PARAM;

    if (req->len == 0U)
        return OM_ERROR_PARAM;

    if (!req->tx && !req->rx)
        return OM_ERROR_PARAM;

    SpiBus *bus = dev->bus;

    if (dev->suspended)
        return (OmRet)ERR_SPI_DEV_SUSPENDED;

    /* DoubleBuf 容量校验 */
    if (spi_dbuf_enabled(bus) && req->len > dbuf_capacity(&bus->txDbuf))
        return OM_ERROR_PARAM;

    req->dev = dev;
    req->status = OM_OK;
    req->transferred = 0U;

    /* 关中断保证 epoch 递增原子性（单核足够；多核需原子操作） */
    {
        OsalIrqIsrState k;
        osal_irq_lock(&k);
        req->epoch = ++bus->asyncEpoch;
        osal_irq_unlock(k);
    }

    work_init(&req->work, spi_async_worker_func, req);

    return workqueue_enqueue(&bus->asyncWq, &req->work);
}

/*===========================================================================
 * 异步 worker 回调（workqueue worker 线程中执行）
 *===========================================================================*/

static void spi_async_worker_func(Work *work)
{
    SpiAsyncRequest *req = (SpiAsyncRequest *)work->data;
    HalSpiDevice    *dev = req->dev;
    SpiBus          *bus = dev->bus;
    OmRet            ret;

    /* ---- 获取总线锁 ---- */
    ret = spi_bus_lock(bus);
    if (ret != OM_OK) {
        req->status = ret;
        req->asyncCb(req->asyncParam, req);
        return;
    }

    /* Worker 再次检查 suspend（并发安全） */
    if (dev->suspended) {
        spi_bus_unlock(bus);
        req->status = (OmRet)ERR_SPI_DEV_SUSPENDED;
        req->asyncCb(req->asyncParam, req);
        return;
    }

    /* ---- 配置缓存 + CS ---- */
    ret = spi_bus_ensure_configured(bus, dev);
    if (ret != OM_OK) {
        spi_bus_unlock(bus);
        req->status = ret;
        req->asyncCb(req->asyncParam, req);
        return;
    }

    spi_cs_assert(dev);

    /* ---- 数据就位 + 启动硬件 ---- */
    if (spi_dbuf_enabled(bus) && req->tx) {
        if (dbuf_is_pending(&bus->txDbuf) && req == bus->prefillTarget
            && req->epoch == bus->prefillEpoch) {
            /* 上轮 peek 预填充了当前请求 → 零拷贝 flip */
            dbuf_swap(&bus->txDbuf);
            bus->prefillTarget = NULL;
        } else {
            /* 无预填 / 目标不匹配 / epoch 过期 → CPU 拷贝 + commit */
            if (dbuf_is_pending(&bus->txDbuf))
                dbuf_flush(&bus->txDbuf);
            bus->prefillTarget = NULL;
            memcpy(dbuf_get_write_ptr(&bus->txDbuf), req->tx, req->len);
            dbuf_commit(&bus->txDbuf, req->len);
        }
        uint8_t *rp = dbuf_get_read_ptr(&bus->txDbuf, NULL);
        ret = bus->interface->transfer(bus, rp, req->rx, req->len);
    } else {
        ret = bus->interface->transfer(bus, req->tx, req->rx, req->len);
    }

    if (ret != OM_OK) {
        spi_cs_deassert(dev);
        spi_bus_unlock(bus);
        req->status = ret;
        req->asyncCb(req->asyncParam, req);
        return;
    }

    bus->busy = 1;
    spi_bus_unlock(bus);

    /* ---- Peek 预填充（CPU / DMA 时间并行）---- */
    if (spi_dbuf_enabled(bus)) {
        OsalIrqIsrState k;
        osal_irq_lock(&k);
        if (!list_empty(&bus->asyncWq.pending)) {
            SpiAsyncRequest *next = list_first_entry(&bus->asyncWq.pending,
                                                     SpiAsyncRequest, work.node);
            if (next->tx) {
                memcpy(dbuf_get_write_ptr(&bus->txDbuf), next->tx, next->len);
                dbuf_mark_written(&bus->txDbuf, next->len);
                bus->prefillTarget = next;
                bus->prefillEpoch  = next->epoch;
            }
        }
        osal_irq_unlock(k);
    }

    /* ---- 阻塞等待硬件完成 ---- */
    uint32_t timeout_ms = spi_calc_timeout_ms(req->len, dev->cfg.maxHz, dev->cfg.transferOverheadMs);
    ret = completion_wait(&bus->transferDone, timeout_ms);

    if (ret == OM_ERROR_TIMEOUT) {
        /* ISR gate + drain */
        OsalIrqIsrState k;
        osal_irq_lock(&k);
        bus->busy = 0;
        osal_irq_unlock(k);
        completion_wait(&bus->transferDone, 0U);

        spi_bus_lock(bus);
        spi_cs_deassert(dev);
        spi_bus_unlock(bus);

        req->status = (OmRet)ERR_SPI_TRANSFER_TIMEOUT;
        req->asyncCb(req->asyncParam, req);
        return;
    }

    /* 传输正常完成 */
    req->status      = bus->lastStatus;
    req->transferred = bus->lastTransferred;

    /* ---- 收尾：CS 恢复 + 消费 DoubleBuf ---- */
    spi_bus_lock(bus);
    spi_cs_deassert(dev);
    bus->busy = 0;
    if (spi_dbuf_enabled(bus))
        dbuf_consume(&bus->txDbuf);
    spi_bus_unlock(bus);

    req->asyncCb(req->asyncParam, req);
}

/*===========================================================================
 * 手动 CS 事务 API
 *===========================================================================*/

OmRet hal_spi_transaction_begin(HalSpiDevice *dev)
{
    if (!dev || !dev->bus)
        return OM_ERROR_PARAM;

    SpiBus *bus = dev->bus;

    /* 防止嵌套事务自死锁（非递归 mutex 下同一线程重复 begin） */
    if (dev->inTransaction)
        return (OmRet)ERR_SPI_CS_CONFLICT;

    OmRet ret = spi_bus_lock(bus);
    if (ret != OM_OK)
        return ret;

    if (dev->suspended) {
        spi_bus_unlock(bus);
        return (OmRet)ERR_SPI_DEV_SUSPENDED;
    }

    if (bus->busy) {
        spi_bus_unlock(bus);
        return (OmRet)ERR_SPI_BUSY;
    }

    ret = spi_bus_ensure_configured(bus, dev);
    if (ret != OM_OK) {
        spi_bus_unlock(bus);
        return ret;
    }

    spi_cs_assert(dev);
    dev->inTransaction = 1;
    return OM_OK;
}

OmRet hal_spi_transaction_transfer(HalSpiDevice *dev, SpiXfer *xfer)
{
    if (!dev || !dev->bus || !dev->inTransaction)
        return OM_ERROR_PARAM;

    if (!xfer || xfer->txLen == 0U)
        return OM_ERROR_PARAM;

    if (xfer->txLen != xfer->rxLen)
        return OM_ERROR_PARAM;

    if (!xfer->txBuf && !xfer->rxBuf)
        return OM_ERROR_PARAM;

    SpiBus *bus = dev->bus;
    size_t len = xfer->txLen;
    uint32_t timeout_ms = spi_calc_timeout_ms(len, dev->cfg.maxHz, dev->cfg.transferOverheadMs);

    return spi_do_transfer(bus, xfer->txBuf, xfer->rxBuf, len, &xfer->transferred, timeout_ms);
}

OmRet hal_spi_transaction_end(HalSpiDevice *dev)
{
    if (!dev || !dev->bus || !dev->inTransaction)
        return OM_ERROR_PARAM;

    SpiBus *bus = dev->bus;

    spi_cs_deassert(dev);
    dev->inTransaction = 0;
    return spi_bus_unlock(bus);
}

/*===========================================================================
 * 低功耗挂起 / 恢复
 *===========================================================================*/

OmRet hal_spi_device_suspend(HalSpiDevice *dev)
{
    if (!dev || !dev->bus)
        return OM_ERROR_PARAM;

    SpiBus *bus = dev->bus;

    OmRet ret = spi_bus_lock(bus);
    if (ret != OM_OK)
        return ret;

    if (dev->suspended) {
        spi_bus_unlock(bus);
        return OM_OK;
    }

    dev->suspended = 1;
    bus->suspendedCount++;

    /* 全部设备挂起时关闭 SPI 外设时钟 */
    if (bus->suspendedCount == bus->deviceCount) {
        bus->interface->control(bus, SPI_CMD_SUSPEND, NULL);
    }

    spi_bus_unlock(bus);
    return OM_OK;
}

OmRet hal_spi_device_resume(HalSpiDevice *dev)
{
    if (!dev || !dev->bus)
        return OM_ERROR_PARAM;

    SpiBus *bus = dev->bus;

    OmRet ret = spi_bus_lock(bus);
    if (ret != OM_OK)
        return ret;

    if (!dev->suspended) {
        spi_bus_unlock(bus);
        return OM_OK;
    }

    /* 第一个设备恢复时开启 SPI 外设时钟 */
    if (bus->suspendedCount == bus->deviceCount) {
        bus->interface->control(bus, SPI_CMD_RESUME, NULL);
    }

    bus->suspendedCount--;
    dev->suspended = 0;

    /* suspend 期间配置可能已丢失，失效缓存 */
    bus->cachedDevice = NULL;

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

    /* ISR gate: 传输已被超时放弃，不投递残余信号 */
    if (!bus->busy)
        return;

    bus->lastTransferred = transferred;
    bus->lastStatus      = status;
    completion_done(&bus->transferDone);
}

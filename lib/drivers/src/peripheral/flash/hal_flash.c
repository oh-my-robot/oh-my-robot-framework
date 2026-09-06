/**
 * @file   hal_flash.c
 * @brief  Flash 设备抽象框架实现 v1（注册/几何校验/请求队列/执行路由/busy 协议）
 *
 * 职责（docs/boot_ota/flash_dev_design.md v1 + flash_dev_impl_design.md）：
 * - 参数与几何校验（越界/写对齐/擦除整扇区边界）；
 * - 异步执行路由：请求提交到设备所属域（worker 线程），完成回调/同步唤醒；
 * - 设备 busy 协议：read 同步直跑与写/擦执行互斥（杜绝混合读）；
 * - 后端 ops 只做物理操作（同步实现 + 等待让出），不感知队列/域。
 *
 * 槽占用模型：槽空闲 = work 状态 IDLE（workqueue 在 func 返回后才置 IDLE）。
 * 空闲槽才可提交 → 队列深度 = 并发等待者数（OM_FLASH_QUEUE_DEPTH）；
 * 提交在 irq_lock 临界区内完成（含 enqueue），杜绝两提交者选中同一槽。
 */

#include <string.h>

#include "drivers/peripheral/flash/pal_flash_dev.h"

/*===========================================================================
 * 请求类型
 *===========================================================================*/

#define FLASH_REQ_WRITE 1u
#define FLASH_REQ_ERASE 2u

/*===========================================================================
 * 几何辅助（双模：均匀单值 / 非均一区域表）
 *===========================================================================*/

/** @brief 定位 addr 所在扇区：回填其起始偏移与大小
 *  @retval true 命中（addr < capacity）；false addr 越界或几何非法 */
static bool flash_geom_sector_at(const FlashGeometry *g, uint32_t addr, uint32_t *start,
                                 uint32_t *size)
{
    if (addr >= g->capacity)
    {
        return false;
    }
    if (g->sectorSize > 0)
    {
        *size = g->sectorSize;
        *start = (addr / g->sectorSize) * g->sectorSize;
        return true;
    }
    /* 非均一：注册期校验区域恰好连续覆盖 [0, capacity)，遍历以覆盖至 capacity 为表尾 */
    for (const FlashSectorRegion *r = g->sectorRegions;; r++)
    {
        uint64_t regionEnd = (uint64_t)r->offset + (uint64_t)r->size * r->count;
        if (addr >= r->offset && addr < regionEnd)
        {
            uint64_t rel = addr - r->offset;
            *size = r->size;
            *start = r->offset + (uint32_t)((rel / r->size) * r->size);
            return true;
        }
        if (regionEnd >= g->capacity)
        {
            break;
        }
    }
    return false;
}

/** @brief 判断 off 是否为扇区边界（含 capacity 末端边界） */
static bool flash_geom_is_sector_boundary(const FlashGeometry *g, uint32_t off)
{
    if (off >= g->capacity)
    {
        return off == g->capacity;
    }
    uint32_t start;
    uint32_t size;
    if (!flash_geom_sector_at(g, off, &start, &size))
    {
        return false;
    }
    return off == start;
}

/** @brief [addr, addr+len) ⊆ [0, capacity)，64 位中间量防溢出 */
static bool flash_range_valid(const FlashGeometry *g, uint32_t addr, size_t len)
{
    if (addr > g->capacity)
    {
        return false;
    }
    return (uint64_t)len <= (uint64_t)g->capacity - addr;
}

/** @brief 擦除整扇区制校验：addr 与 addr+len 都必须是扇区边界
 *  （范围两端都是边界 ⇔ 区间恰为若干连续整扇区之并，跨大小不同的区域也成立） */
static OmRet flash_erase_validate(const FlashGeometry *g, uint32_t addr, size_t len)
{
    if (len == 0)
    {
        return OM_OK;
    }
    if (!flash_range_valid(g, addr, len))
    {
        return OM_ERR_INVALID_ARG;
    }
    if (!flash_geom_is_sector_boundary(g, addr))
    {
        return OM_ERR_INVALID_ARG;
    }
    if (!flash_geom_is_sector_boundary(g, addr + (uint32_t)len))
    {
        return OM_ERR_INVALID_ARG;
    }
    return OM_OK;
}

/*===========================================================================
 * 槽管理（槽空闲 = work IDLE；提交全程在 irq_lock 临界区内）
 *===========================================================================*/

/** @brief 找空闲请求槽（work IDLE）；无则返回 NULL（须持锁调用） */
static FlashRequest *flash_find_free_slot(FlashDev *dev)
{
    for (uint32_t i = 0; i < OM_FLASH_QUEUE_DEPTH; i++)
    {
        if (!work_is_busy(&dev->slots[i].work))
        {
            return &dev->slots[i];
        }
    }
    return NULL;
}

/*===========================================================================
 * 请求执行（域 worker 线程上下文）
 *===========================================================================*/

static void flash_request_worker(Work *work)
{
    FlashRequest *req = (FlashRequest *)work; /* work 为请求首成员 */
    FlashDev *dev = req->dev;

    /* 设备 busy（read 直跑中）→ 让出等待，直至 read 完成 */
    while (dev->busy)
    {
        osal_sleep_ms(1);
    }

    dev->busy = true;
    if (req->type == FLASH_REQ_WRITE)
    {
        req->result = dev->ops->write(dev, req->addr, req->data, req->len);
    }
    else
    {
        req->result = dev->ops->erase(dev, req->addr, req->len);
    }
    dev->busy = false;

    /* 完成回调（func 内；槽在本 work 返回后由 workqueue 置 IDLE 才可复用——
     * 回调内再提交会落在其它 IDLE 槽或 BUSY，不会撞本槽） */
    if (req->done)
    {
        req->done(dev, req->result, req->param);
    }
}

/*===========================================================================
 * 提交（线程上下文；find+fill+enqueue 全程持锁，杜绝同槽双选）
 *===========================================================================*/

static OmRet flash_submit(FlashDev *dev, uint32_t type, uint32_t addr, const void *data,
                          size_t len, FlashDoneCb done, void *param, FlashRequest **out_req)
{
    osal_irq_lock_task();
    FlashRequest *req = flash_find_free_slot(dev);
    if (!req)
    {
        osal_irq_unlock_task();
        return OM_ERR_FLASH_BUSY;
    }
    req->type = type;
    req->addr = addr;
    req->len = len;
    req->data = data;
    req->done = done;
    req->param = param;
    req->result = OM_ERR_IO; /* 防未执行读脏值 */

    OmRet ret = workqueue_enqueue(&dev->domain->wq, &req->work);
    osal_irq_unlock_task();
    if (ret == OM_OK && out_req)
    {
        *out_req = req;
    }
    return ret;
}

/*===========================================================================
 * 标准 Device 接口（read/write 薄转发）
 *===========================================================================*/

OmRet flash_dev_init(Device *dev)
{
    return dev ? OM_OK : OM_ERR_INVALID_ARG;
}

OmRet flash_dev_open(Device *dev, uint32_t oparam)
{
    (void)oparam;
    return dev ? OM_OK : OM_ERR_INVALID_ARG;
}

OmRet flash_dev_close(Device *dev)
{
    return dev ? OM_OK : OM_ERR_INVALID_ARG;
}

/* ctrl_info = (void *)(uintptr_t)offset；flash_read 的薄转发。
 * 返回 size_t 通道丢失错误详情（device 模型限制）：成功 = len，失败 = 0；
 * 需要精确错误码走 flash_read()。 */
size_t flash_dev_read(Device *dev, void *ctrl_info, void *data, size_t len)
{
    if (!dev || !data || len == 0)
    {
        return 0;
    }
    FlashDev *fdev = (FlashDev *)dev; /* parent 首成员（container 语义，见头文件） */
    if (flash_read(fdev, (uint32_t)(uintptr_t)ctrl_info, data, len) != OM_OK)
    {
        return 0;
    }
    return len;
}

size_t flash_dev_write(Device *dev, void *ctrl_info, void *data, size_t len)
{
    if (!dev || !data || len == 0)
    {
        return 0;
    }
    FlashDev *fdev = (FlashDev *)dev;
    if (flash_write(fdev, (uint32_t)(uintptr_t)ctrl_info, data, len) != OM_OK)
    {
        return 0;
    }
    return len;
}

OmRet flash_dev_control(Device *dev, size_t cmd, void *args)
{
    if (!dev || !args)
    {
        return OM_ERR_INVALID_ARG;
    }
    FlashDev *fdev = (FlashDev *)dev;
    if (cmd == FLASH_CMD_GET_GEOMETRY)
    {
        *(const FlashGeometry **)args = fdev->geom;
        return OM_OK;
    }
    return OM_ERR_FLASH_NOT_SUPPORTED;
}

static DevInterface flash_dev_interface = {
    /* Device.interface 为非 const 指针（模型现状） */
    .init = flash_dev_init,
    .open = flash_dev_open,
    .close = flash_dev_close,
    .read = flash_dev_read,
    .write = flash_dev_write,
    .control = flash_dev_control,
};

/*===========================================================================
 * 生命周期 / 查找
 *===========================================================================*/

OmRet flash_register(FlashDev *dev, const char *name, const FlashGeometry *geom,
                     const FlashOps *ops, void *hw, FlashDomain *domain)
{
    if (!dev || !name || !geom || !ops)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (!ops->read || !ops->write || !ops->erase)
    {
        return OM_ERR_INVALID_ARG;
    }

    /* 几何基本合法性（注册期一次校验） */
    if (geom->capacity == 0 || geom->writeUnit == 0)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (geom->sectorSize > 0)
    {
        if (geom->sectorCount == 0 || geom->capacity % geom->sectorSize != 0)
        {
            return OM_ERR_INVALID_ARG;
        }
    }
    else
    {
        if (!geom->sectorRegions)
        {
            return OM_ERR_INVALID_ARG;
        }
        uint32_t expect = 0;
        for (const FlashSectorRegion *r = geom->sectorRegions;; r++)
        {
            if (r->size == 0 || r->count == 0)
            {
                return OM_ERR_INVALID_ARG; /* 区域表必须显式覆盖，无哨兵 */
            }
            if (r->offset != expect || (uint64_t)r->size * r->count > geom->capacity - expect)
            {
                return OM_ERR_INVALID_ARG;
            }
            expect += (uint32_t)((uint64_t)r->size * r->count);
            if (expect == geom->capacity)
            {
                break;
            }
            if (expect > geom->capacity)
            {
                return OM_ERR_INVALID_ARG;
            }
        }
    }

    dev->geom = geom;
    dev->ops = ops;
    dev->hw = hw;
    dev->busy = false;
    dev->parent.type = DEVICE_TYPE_FLASH;
    dev->parent.handle = hw;
    dev->parent.interface = &flash_dev_interface;

    /* 请求槽初始化：work 绑定请求执行函数 */
    for (uint32_t i = 0; i < OM_FLASH_QUEUE_DEPTH; i++)
    {
        FlashRequest *req = &dev->slots[i];
        memset(req, 0, sizeof(*req));
        req->dev = dev; /* worker 经请求取设备 */
        work_init(&req->work, flash_request_worker, req);
    }

    /* 执行域：显式域或设备内嵌独立域 */
    if (domain)
    {
        dev->domain = domain;
    }
    else
    {
        dev->domain = &dev->autoDomain;
        if (flash_domain_init(dev->domain, name, OSAL_PRIO_NORMAL_BASE, 3072u) != OM_OK)
        {
            return OM_ERR_FLASH_NOT_SUPPORTED;
        }
    }

    OmRet ret = device_register(&dev->parent, (char *)name, 0);
    if (ret != OM_OK)
    {
        return ret;
    }
    return OM_OK;
}

FlashDev *flash_find(const char *name)
{
    Device *d = device_find((char *)name);
    if (!d || d->type != DEVICE_TYPE_FLASH)
    {
        return NULL;
    }
    return (FlashDev *)d; /* parent 首成员 */
}

const FlashGeometry *flash_geometry(FlashDev *dev)
{
    if (!dev)
    {
        return NULL;
    }
    return dev->geom;
}

/*===========================================================================
 * 核心 API
 *===========================================================================*/

OmRet flash_read(FlashDev *dev, uint32_t addr, void *buf, size_t len)
{
    if (!dev)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (len == 0)
    {
        return OM_OK;
    }
    if (!buf)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (!flash_range_valid(dev->geom, addr, len))
    {
        return OM_ERR_INVALID_ARG;
    }

    /* busy 协议：设备有写/擦执行中或另一读在跑 → 拒绝（杜绝混合读） */
    osal_irq_lock_task();
    if (dev->busy)
    {
        osal_irq_unlock_task();
        return OM_ERR_FLASH_BUSY;
    }
    dev->busy = true;
    osal_irq_unlock_task();

    OmRet ret = dev->ops->read(dev, addr, buf, len);

    dev->busy = false;
    return ret;
}

void flash_set_done_cb(FlashDev *dev, FlashDoneCb done, void *param)
{
    if (!dev)
    {
        return;
    }
    osal_irq_lock_task();
    dev->doneCb = done;
    dev->doneParam = param;
    osal_irq_unlock_task();
}

/** @brief 提交时快照设备级完成通知（锁内，防与 setter 竞争） */
static void flash_snapshot_done(FlashDev *dev, FlashDoneCb *done, void **param)
{
    osal_irq_lock_task();
    *done = dev->doneCb;
    *param = dev->doneParam;
    osal_irq_unlock_task();
}

OmRet flash_write_async(FlashDev *dev, uint32_t addr, const void *data, size_t len)
{
    if (!dev)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (len == 0)
    {
        return OM_OK;
    }
    if (!data)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (!flash_range_valid(dev->geom, addr, len))
    {
        return OM_ERR_INVALID_ARG;
    }
    if (addr % dev->geom->writeUnit != 0 || len % dev->geom->writeUnit != 0)
    {
        return OM_ERR_INVALID_ARG;
    }
    FlashDoneCb done;
    void *param;
    flash_snapshot_done(dev, &done, &param);
    return flash_submit(dev, FLASH_REQ_WRITE, addr, data, len, done, param, NULL);
}

OmRet flash_erase_async(FlashDev *dev, uint32_t addr, size_t len)
{
    if (!dev)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (len == 0)
    {
        return OM_OK; /* 空擦 = 无操作，不占槽不回调 */
    }
    OmRet ret = flash_erase_validate(dev->geom, addr, len);
    if (ret != OM_OK)
    {
        return ret;
    }
    FlashDoneCb done;
    void *param;
    flash_snapshot_done(dev, &done, &param);
    return flash_submit(dev, FLASH_REQ_ERASE, addr, NULL, len, done, param, NULL);
}

/** @brief 同步等待原语公共路径：同域 worker 上下文 → BUSY（自锁拒绝）；
 *         否则提交 + work_wait_idle 阻塞等待（func 返回后唤醒） */
static OmRet flash_sync_wait(FlashDev *dev, uint32_t type, uint32_t addr, const void *data,
                             size_t len)
{
    if (!dev)
    {
        return OM_ERR_INVALID_ARG;
    }
    /* 校验（与 async 同一语义） */
    if (len == 0)
    {
        return OM_OK;
    }
    if (!flash_range_valid(dev->geom, addr, len))
    {
        return OM_ERR_INVALID_ARG;
    }
    if (type == FLASH_REQ_WRITE)
    {
        if (!data)
        {
            return OM_ERR_INVALID_ARG;
        }
        if (addr % dev->geom->writeUnit != 0 || len % dev->geom->writeUnit != 0)
        {
            return OM_ERR_INVALID_ARG;
        }
    }
    else
    {
        OmRet ret = flash_erase_validate(dev->geom, addr, len);
        if (ret != OM_OK)
        {
            return ret;
        }
    }

    /* 同域自锁拒绝：调用者即本设备域 worker（回调/请求执行中）→ 同步等待会等自己 */
    if (dev->domain && osal_thread_self() == dev->domain->wq.thread)
    {
        return OM_ERR_FLASH_BUSY;
    }

    /* 提交 + 阻塞等待：work_wait_idle 在本请求 work 的 func 返回（IDLE）后返回，
     * 此时 result 已稳定、槽已可复用（无 RUNNING 窗口提前唤醒问题） */
    FlashRequest *req = NULL;
    OmRet ret = flash_submit(dev, type, addr, data, len, NULL, NULL, &req);
    if (ret != OM_OK)
    {
        return ret;
    }
    ret = work_wait_idle(&req->work, OSAL_WAIT_FOREVER);
    if (ret != OM_OK)
    {
        return ret;
    }
    return req->result;
}

OmRet flash_write(FlashDev *dev, uint32_t addr, const void *data, size_t len)
{
    return flash_sync_wait(dev, FLASH_REQ_WRITE, addr, data, len);
}

OmRet flash_erase(FlashDev *dev, uint32_t addr, size_t len)
{
    return flash_sync_wait(dev, FLASH_REQ_ERASE, addr, NULL, len);
}

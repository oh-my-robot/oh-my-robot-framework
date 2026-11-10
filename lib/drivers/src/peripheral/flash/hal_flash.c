/**
 * @file   hal_flash.c
 * @brief  Flash 设备抽象框架实现（注册/几何校验/擦后校验/设备锁）
 *
 * 职责：
 * - 参数与几何校验（越界、写对齐、擦除整扇区边界）；
 * - 擦后校验与重试（擦除报告的"成功"未必为真）；
 * - 每设备睡眠互斥量：读/写/擦共用，操作在调用者上下文同步执行；
 * - 后端 ops 只做物理操作（同步实现 + 等待让出），不感知锁。
 *
 * 本层不持有线程。其它任务不被饿死由后端让出契约承担（见头文件）。
 */

#include "drivers/peripheral/flash/pal_flash_dev.h"

/*===========================================================================
 * 几何辅助
 *===========================================================================*/

/** @brief 定位 addr 所在扇区：回填其起始偏移与大小 */
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

/** @brief [addr, addr+len) 落在 [0, capacity) 内，64 位中间量防溢出 */
static bool flash_range_valid(const FlashGeometry *g, uint32_t addr, size_t len)
{
    if (addr > g->capacity)
    {
        return false;
    }
    return (uint64_t)len <= (uint64_t)g->capacity - addr;
}

/*===========================================================================
 * 路径校验
 *===========================================================================*/

/** @brief 擦除整扇区制校验：addr 与 addr+len 都必须是扇区边界
 *  （范围两端都是边界，等价于区间恰为若干连续整扇区之并，跨大小不同的区域也成立） */
static OmRet flash_erase_validate(const FlashGeometry *g, uint32_t addr, size_t len)
{
    if (len == 0)
    {
        return OM_OK;
    }
    if (!flash_range_valid(g, addr, len))
    {
        return OM_ERR_RANGE; /* 越界与未对齐分开报：调用方行动不同（改地址 vs 改长度/粒度） */
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

/** @brief 写（program）制校验：数据非空 + 范围内 + 两地址端 writeUnit 对齐
 *  写单位在全器件均一，故对齐判定用模运算即可。
 *  （擦除单位可以非均一，其校验不能退化成模运算——见 flash_erase_validate） */
static OmRet flash_write_validate(const FlashGeometry *g, uint32_t addr, const void *data,
                                  size_t len)
{
    if (len == 0)
    {
        return OM_OK;
    }
    if (!data)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (!flash_range_valid(g, addr, len))
    {
        return OM_ERR_RANGE; /* 越界与未对齐分开报（同 flash_erase_validate） */
    }
    if (addr % g->writeUnit != 0 || len % g->writeUnit != 0)
    {
        return OM_ERR_INVALID_ARG;
    }
    return OM_OK;
}

/*===========================================================================
 * 擦后校验
 *===========================================================================*/

/** 擦后校验的分块读粒度：栈上小块逐段比对。粒度即每次后端读调用的开销
 *  （外置器件每调一次 = 一条命令+地址的总线事务），故取页量级的折中值：
 *  再放大只省总线开销却多占调用者栈，再缩小则事务开销按比例上升 */
#define FLASH_VERIFY_CHUNK 256u

/** @brief 擦后校验：擦除报告的"成功"未必为真——擦除单元失效时数据仍不为擦后值。
 *  本层是唯一能廉价发现此事的层：消费者若各自校验，漏掉一处就是静默坏数据，
 *  而"以为已擦"是追加型存储最致命的假设。
 *  @param dev 设备（调用者已持设备锁，读回不会与外部操作交错）
 *  @retval OM_OK                 全段已达 erasedValue
 *  @retval OM_ERR_FLASH_IO       读回本身失败——不构成"不可用"判据，按读错误原样上报
 *  @retval OM_ERR_FLASH_UNUSABLE 存在未达擦后值的字节，该区结构性不可用
 *  @note 几何未声明擦除单位者（经适配器包装的免擦介质）无从校验，跳过。 */
static OmRet flash_verify_erased(FlashDev *dev, uint32_t addr, size_t len)
{
    if (!flash_geom_needs_erase(dev->geom))
    {
        return OM_OK;
    }
    uint8_t buf[FLASH_VERIFY_CHUNK];
    uint8_t erased = dev->geom->erasedValue;
    uint32_t cur = addr;
    size_t remaining = len;

    while (remaining > 0u)
    {
        size_t take = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
        OmRet ret = dev->ops->read(dev, cur, buf, take);
        if (ret != OM_OK)
        {
            return ret;
        }
        for (size_t i = 0u; i < take; i++)
        {
            if (buf[i] != erased)
            {
                return OM_ERR_FLASH_UNUSABLE;
            }
        }
        cur += (uint32_t)take;
        remaining -= take;
    }
    return OM_OK;
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
                     const FlashOps *ops, void *hw)
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

    /* 设备锁：读/写/擦共用。设备永驻，故不提供注销路径 */
    OsalMutex *lock = NULL;
    if (osal_mutex_create(&lock) != OSAL_OK)
    {
        return OM_ERR_NO_MEM;
    }

    OmRet ret = device_register(&dev->parent, (char *)name, 0);
    if (ret != OM_OK)
    {
        osal_mutex_delete(lock);
        return ret;
    }

    dev->geom = geom;
    dev->ops = ops;
    dev->hw = hw;
    dev->lock = lock;
    dev->parent.type = DEVICE_TYPE_FLASH;
    dev->parent.handle = hw;
    dev->parent.interface = &flash_dev_interface;
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
 * 核心 API（调用者上下文同步执行，读/写/擦共用设备锁）
 *===========================================================================*/

/** @brief 取设备锁；失败仅在锁句柄无效时发生（注册后不可能）——属框架内部故障，
 *  与介质操作失败的 OM_ERR_FLASH_IO 归因不同，故取通用 IO */
static OmRet flash_lock(FlashDev *dev)
{
    return (osal_mutex_lock(dev->lock, OSAL_WAIT_FOREVER) == OSAL_OK) ? OM_OK : OM_ERR_IO;
}

static void flash_unlock(FlashDev *dev)
{
    osal_mutex_unlock(dev->lock);
}

OmRet flash_read(FlashDev *dev, uint32_t addr, void *buf, size_t len)
{
    if (!dev)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (len == 0u)
    {
        return OM_OK;
    }
    if (!buf)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (!flash_range_valid(dev->geom, addr, len))
    {
        return OM_ERR_RANGE; /* 越界不等同参数非法（同写/擦路径） */
    }

    OmRet ret = flash_lock(dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    ret = dev->ops->read(dev, addr, buf, len);
    flash_unlock(dev);
    return ret;
}

OmRet flash_write(FlashDev *dev, uint32_t addr, const void *data, size_t len)
{
    if (!dev)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (len == 0u)
    {
        return OM_OK;
    }
    OmRet ret = flash_write_validate(dev->geom, addr, data, len);
    if (ret != OM_OK)
    {
        return ret;
    }

    ret = flash_lock(dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    ret = dev->ops->write(dev, addr, data, len);
    flash_unlock(dev);
    return ret;
}

OmRet flash_erase(FlashDev *dev, uint32_t addr, size_t len)
{
    if (!dev)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (len == 0u)
    {
        return OM_OK; /* 空擦 = 无操作 */
    }
    OmRet ret = flash_erase_validate(dev->geom, addr, len);
    if (ret != OM_OK)
    {
        return ret;
    }

    ret = flash_lock(dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    /* 擦加擦后校验，按 OM_FLASH_ERASE_VERIFY_ATTEMPTS 轮次重试。
     * 持锁期间做完，读回不会与其它操作交错 */
    uint32_t attempts = 0u;
    for (;;)
    {
        ret = dev->ops->erase(dev, addr, len);
        if (ret != OM_OK)
        {
            break; /* 后端失败 = 这一次 IO 失败；不在此重试（归消费者策略） */
        }
        ret = flash_verify_erased(dev, addr, len);
        attempts++;
        if (ret != OM_ERR_FLASH_UNUSABLE || attempts >= OM_FLASH_ERASE_VERIFY_ATTEMPTS)
        {
            break;
        }
    }
    flash_unlock(dev);
    return ret;
}

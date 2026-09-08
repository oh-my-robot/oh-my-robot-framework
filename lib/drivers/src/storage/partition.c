/**
 * @file   partition.c
 * @brief  分区表抽象实现
 *
 * 实现要点：
 * - 注册形态：表经 om_partition_register 交入模块私有持有（static 指针，
 *   不拷贝——表保持 const 只读存储驻留）；公共头零数据符号，无绕过面；
 * - 未注册 = 空表语义：一切操作返回 OM_ERR_NOT_FOUND；
 * - 便捷层一律 name 入口：内部重解析权威表，杜绝外部描述符伪造；
 * - 器件访问经底层器件 API（flash_* 同步面）——对齐/容量由该层强制。
 */

#include <string.h>

#include "core/om_def.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "drivers/storage/partition.h"

/* 模块私有表持有（注册交入；不拷贝——表本体 const 静态存储期） */
static const OmPartitionEntry *s_table;
static uint32_t s_count;

/** @brief 权威表线性查找（表小；name 主键唯一） */
static const OmPartitionEntry *om_partition_lookup(const char *name)
{
    if (!name)
    {
        return NULL;
    }
    for (uint32_t i = 0; i < s_count; i++)
    {
        if (strcmp(s_table[i].name, name) == 0)
        {
            return &s_table[i];
        }
    }
    return NULL;
}

/** @brief 解析器件句柄（条目校验：器件存在 + 分区在器件容量内） */
static OmRet om_partition_resolve_dev(const OmPartitionEntry *e, FlashDev **dev)
{
    *dev = flash_find(e->devName);
    if (!*dev)
    {
        return OM_ERR_NOT_FOUND;
    }
    const FlashGeometry *g = flash_geometry(*dev);
    if (e->offset >= g->capacity || e->size > g->capacity - e->offset)
    {
        return OM_ERR_INVALID_ARG; /* 板配置错误：分区越器件容量 */
    }
    return OM_OK;
}

/** @brief 扇区友好判定：分区擦除闭包恰好等于自身——start 为扇区起点，
 *  size 为自 start 起整扇区数（跨 region 时逐段验证，均匀几何为退化情形） */
static bool is_partition_sector_aligned(const FlashGeometry *g, uint32_t off, uint32_t size)
{
    if (g->sectorSize > 0u)
    {
        /* 均匀几何：一个扇区大小贯穿全器件 */
        return off % g->sectorSize == 0u && size % g->sectorSize == 0u;
    }
    /* region 表几何：逐 region 消费，每段边界须为扇区边界 */
    uint32_t remaining = size;
    uint32_t cur = off;
    const FlashSectorRegion *r = g->sectorRegions;
    for (; r && remaining > 0u; r++)
    {
        uint32_t rStart = r->offset;
        uint32_t rLen = r->size * r->count;
        if (cur >= rStart + rLen)
        {
            continue; /* 分区整体在更靠后的 region */
        }
        if (cur < rStart || (cur - rStart) % r->size != 0u)
        {
            return false; /* 起点落在 region 间隙或非本 region 扇区边界 */
        }
        uint32_t avail = rStart + rLen - cur;
        uint32_t take = (remaining < avail) ? remaining : avail;
        if (take % r->size != 0u)
        {
            return false; /* 跨界点不是扇区边界（终点非整扇区） */
        }
        remaining -= take;
        cur += take;
    }
    return remaining == 0u; /* region 表耗尽仍未消费完 → 越界/非法 */
}

OmRet om_partition_register(const OmPartitionEntry *table, uint32_t count)
{
    if (!table || count == 0u)
    {
        return OM_ERR_INVALID_ARG;
    }
    /* 注册期结构校验：字段非空、size 非零、offset+size 无溢出 */
    for (uint32_t i = 0; i < count; i++)
    {
        const OmPartitionEntry *e = &table[i];
        if (!e->name || !e->devName || e->size == 0u || e->offset > UINT32_MAX - e->size)
        {
            return OM_ERR_INVALID_ARG;
        }
        for (uint32_t j = 0; j < i; j++) /* 重名校验（防表数据笔误） */
        {
            if (strcmp(table[j].name, e->name) == 0)
            {
                return OM_ERR_INVALID_ARG;
            }
        }
    }
    /* 注册期几何校验（fail-fast）：器件已注册则逐条过扇区友好判定；
     * 器件未注册（顺序解耦）跳过——操作期 resolve + 器件层 erase 断言兜底 */
    for (uint32_t i = 0; i < count; i++)
    {
        FlashDev *dev = flash_find(table[i].devName);
        if (!dev)
        {
            continue;
        }
        const FlashGeometry *g = flash_geometry(dev);
        const OmPartitionEntry *e = &table[i];
        if (e->offset >= g->capacity || e->size > g->capacity - e->offset)
        {
            return OM_ERR_INVALID_ARG; /* 越器件容量 */
        }
        if (!is_partition_sector_aligned(g, e->offset, e->size))
        {
            return OM_ERR_INVALID_ARG; /* 非扇区友好（erase 闭包 != 自身） */
        }
    }
    s_table = table;
    s_count = count;
    return OM_OK;
}

OmRet om_partition_query(const char *name, OmPartitionEntry *out)
{
    if (!name || !out)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e = om_partition_lookup(name);
    if (!e)
    {
        return OM_ERR_NOT_FOUND; /* 含未注册=空表语义；*out 不动 */
    }
    *out = *e;
    return OM_OK;
}

/** @brief 便捷层公共校验：name 解析 + 器件解析 + 分区内范围断言 */
static OmRet om_partition_range(const char *name, uint32_t off, size_t len,
                                const OmPartitionEntry **outE, FlashDev **outDev)
{
    if (!name)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e = om_partition_lookup(name);
    if (!e)
    {
        return OM_ERR_NOT_FOUND; /* 含未注册=空表语义 */
    }
    if (off >= e->size || len > e->size - off)
    {
        return OM_ERR_INVALID_ARG; /* 双端越界（含 off+len 溢出防护：off<size 先行） */
    }
    FlashDev *dev;
    OmRet ret = om_partition_resolve_dev(e, &dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    *outE = e;
    *outDev = dev;
    return OM_OK;
}

OmRet om_partition_read(const char *name, uint32_t off, void *buf, size_t len)
{
    if (!buf)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e;
    FlashDev *dev;
    OmRet ret = om_partition_range(name, off, len, &e, &dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    return flash_read(dev, e->offset + off, buf, len);
}

OmRet om_partition_write(const char *name, uint32_t off, const void *data, size_t len)
{
    if (!data)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e;
    FlashDev *dev;
    OmRet ret = om_partition_range(name, off, len, &e, &dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    return flash_write(dev, e->offset + off, data, len);
}

OmRet om_partition_erase(const char *name)
{
    if (!name)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e = om_partition_lookup(name);
    if (!e)
    {
        return OM_ERR_NOT_FOUND; /* 含未注册=空表语义 */
    }
    FlashDev *dev;
    OmRet ret = om_partition_resolve_dev(e, &dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    /* 整分区擦：扇区对齐由器件层强制（配置错误显式报 INVALID_ARG，不静默扩擦） */
    return flash_erase(dev, e->offset, e->size);
}

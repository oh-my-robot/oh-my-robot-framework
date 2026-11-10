/**
 * @file   partition.c
 * @brief  分区表抽象实现 v2（注册表 + 句柄，模块零状态）
 *
 * 实现要点：
 * - 零状态：无私有全局、无 init 依赖；全部 API 天然可重入；
 * - 注册表由调用方持有，模块只读其 table/count；
 * - 句柄域校验：index < reg->count，offset/size 恒从表取；
 * - 器件指针在 open 期解析一次并缓存——热路径无名字查找；
 *   交换代价（dev 可伪造）见 partition.h 句柄的 @warning；
 * - 器件访问经底层器件 API（flash_* 同步面）——对齐/容量由该层强制。
 */

#include <string.h>

#include "core/om_def.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "drivers/storage/partition.h"

/** 已擦自检的分块读粒度：栈上小块逐段比对，避免为整段申请缓冲 */
#define PARTITION_IS_ERASED_CHUNK 64u

/* ===================================================================
 * 内部：注册表遍历
 * =================================================================== */

/** @brief 注册表可用性（表非空 + count 非零） */
static bool partition_reg_usable(const OmPartitionRegistry *reg)
{
    return reg != NULL && reg->table != NULL && reg->count > 0u;
}

/** @brief 按索引取条目指针（调用方保证 index < count） */
static const OmPartitionEntry *partition_entry_at(const OmPartitionRegistry *reg, uint32_t index)
{
    return &reg->table[index];
}

/** @brief 句柄域校验：注册表可用 + index 在域内 + 缓存的器件指针非空
 *  （零初始化或半填句柄在此被拒——比"信任开发者"多一道廉价保险） */
static bool partition_handle_valid(const OmPartitionHandle *h)
{
    return h != NULL && h->reg != NULL && partition_reg_usable(h->reg) &&
           h->index < h->reg->count && h->dev != NULL;
}

/** @brief 权威表线性查找（表小；name 主键唯一）
 *  NULL name 条目视为畸形配置（registry_validate 可跳过、RAM 来源表可能半填）
 *  ——不可匹配，跳过：未经校验的表不得在 strcmp 上硬故障 */
static const OmPartitionEntry *partition_lookup(const OmPartitionRegistry *reg, const char *name)
{
    if (!partition_reg_usable(reg) || !name)
    {
        return NULL;
    }
    for (uint32_t i = 0; i < reg->count; i++)
    {
        const OmPartitionEntry *e = &reg->table[i];
        if (!e->name)
        {
            continue;
        }
        if (strcmp(e->name, name) == 0)
        {
            return e;
        }
    }
    return NULL;
}

/* ===================================================================
 * 内部：几何校验（v1 逻辑原样保留）
 * =================================================================== */

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

/* ===================================================================
 * 内部：擦除单位枚举（纯函数：只依赖器件几何 + 分区窗口）
 *
 * 分区在 open 期已校验"扇区友好"（擦除闭包 == 自身），故窗口内的擦除单位
 * 恒为若干个完整扇区；非均一几何下大小不一，由数组元素携带。
 * 同一器件可含多种扇区尺寸，故不存在分区级 eraseUnit 标量。
 * =================================================================== */

/** @brief 器件内偏移 cur 处的擦除单位大小；非扇区边界返回 0
 *  @note 均匀几何退化为模运算；区域表几何按段消费，段内偏移须落在扇区边界上 */
static uint32_t flash_erase_unit_size_at(const FlashGeometry *g, uint32_t cur)
{
    if (g->sectorSize > 0u)
    {
        return (cur % g->sectorSize == 0u) ? (uint32_t)g->sectorSize : 0u;
    }
    if (!g->sectorRegions || cur >= g->capacity)
    {
        return 0u;
    }
    uint32_t rStart = 0u;
    for (const FlashSectorRegion *r = g->sectorRegions;; r++)
    {
        if (r->size == 0u || r->count == 0u || rStart >= g->capacity)
        {
            return 0u; /* 表耗尽：注册期保证显式覆盖，此处为防御路径 */
        }
        uint32_t rLen = r->size * r->count;
        if (cur < rStart + rLen)
        {
            return ((cur - rStart) % r->size == 0u) ? r->size : 0u;
        }
        rStart += rLen;
    }
}

/** @brief 枚举分区窗口内的擦除单位（分区窗口 ∩ 器件扇区）
 *  @param g     器件几何
 *  @param off   分区起点（器件内偏移）
 *  @param size  分区大小
 *  @param index 目标序号（0 起）
 *  @param out   命中时写入（分区内偏移 + 大小）；可空 = 只判存在
 *  @return 命中返回 true
 *  @note 不物化数组、不设静态上限——按需走一步，故不存在 ENOMEM 路径 */
static bool partition_erase_unit_probe(const FlashGeometry *g, uint32_t off, uint32_t size,
                                       uint32_t index, OmPartitionEraseUnit *out)
{
    uint32_t end = off + size; /* 无溢出：open 期已保证 off+size <= capacity */
    uint32_t cur = off;
    for (uint32_t i = 0u; cur < end; i++)
    {
        uint32_t unit = flash_erase_unit_size_at(g, cur);
        if (unit == 0u || unit > end - cur)
        {
            return false; /* 未经扇区友好校验的窗口：防御性失败，不静默截断 */
        }
        if (i == index)
        {
            if (out)
            {
                out->offset = cur - off;
                out->size = unit;
            }
            return true;
        }
        cur += unit;
    }
    return false;
}

/** @brief 句柄 → 权威条目 + 器件几何（几何由器件指针唯一决定） */
static const FlashGeometry *partition_query_geom(const OmPartitionHandle *h,
                                                 const OmPartitionEntry **outEntry)
{
    if (!partition_handle_valid(h))
    {
        return NULL;
    }
    if (outEntry)
    {
        *outEntry = partition_entry_at(h->reg, h->index);
    }
    return flash_geometry(h->dev);
}

/** @brief 条目几何校验：器件解析 + 容量 + 扇区友好
 *  require_dev=false（全表校验）：器件未注册跳过——顺序解耦，几何由 open 期兜底；
 *  require_dev=true（open/操作期）：器件不存在即 NOT_FOUND（幽灵器件条目不阻断
 *  其它条目）。outDev 可空 = 调用方不需要器件指针 */
static OmRet partition_geom_check(const OmPartitionEntry *e, bool require_dev, FlashDev **outDev)
{
    FlashDev *dev = flash_find(e->devName);
    if (!dev)
    {
        return require_dev ? OM_ERR_NOT_FOUND : OM_OK;
    }
    const FlashGeometry *g = flash_geometry(dev);
    if (!g || e->offset >= g->capacity || e->size > g->capacity - e->offset)
    {
        /* 越器件容量（或几何不可得）——刻意保持 INVALID_ARG 而非 RANGE：
         * 这是表配置错误（修表，配置期可 REJECT），不是调用方请求越界。
         * 与 partition_range 的 RANGE 是两类不同的行动主体 */
        return OM_ERR_INVALID_ARG;
    }
    if (!is_partition_sector_aligned(g, e->offset, e->size))
    {
        return OM_ERR_INVALID_ARG; /* 非扇区友好：erase 闭包 != 自身 */
    }
    if (outDev)
    {
        *outDev = dev;
    }
    return OM_OK;
}

/** @brief 全表校验用条目校验（器件未注册 → 跳过，返回 OM_OK） */
static OmRet partition_entry_validate(const OmPartitionEntry *e)
{
    return partition_geom_check(e, false, NULL);
}

/* ===================================================================
 * 注册表面
 * =================================================================== */

OmRet om_partition_registry_validate(const OmPartitionRegistry *reg)
{
    if (!partition_reg_usable(reg))
    {
        return OM_ERR_INVALID_ARG;
    }
    /* 结构校验：字段非空、size 非零、offset+size 无溢出、重名 */
    for (uint32_t i = 0; i < reg->count; i++)
    {
        const OmPartitionEntry *e = partition_entry_at(reg, i);
        if (!e->name || !e->devName || e->size == 0u || e->offset > UINT32_MAX - e->size)
        {
            return OM_ERR_INVALID_ARG;
        }
        for (uint32_t j = 0; j < i; j++)
        {
            if (strcmp(reg->table[j].name, e->name) == 0)
            {
                return OM_ERR_INVALID_ARG;
            }
        }
    }
    /* 几何校验（器件未注册的条目跳过） */
    for (uint32_t i = 0; i < reg->count; i++)
    {
        OmRet ret = partition_entry_validate(partition_entry_at(reg, i));
        if (ret != OM_OK)
        {
            return ret;
        }
    }
    return OM_OK;
}

uint32_t om_partition_registry_count(const OmPartitionRegistry *reg)
{
    return partition_reg_usable(reg) ? reg->count : 0u;
}

OmRet om_partition_registry_at(const OmPartitionRegistry *reg, uint32_t index,
                               OmPartitionEntry *out)
{
    if (!partition_reg_usable(reg) || !out)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (index >= reg->count)
    {
        return OM_ERR_NOT_FOUND;
    }
    *out = *partition_entry_at(reg, index);
    return OM_OK;
}

OmRet om_partition_query(const OmPartitionRegistry *reg, const char *name, OmPartitionEntry *out)
{
    if (!name || !out)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e = partition_lookup(reg, name);
    if (!e)
    {
        return OM_ERR_NOT_FOUND; /* 含空表语义；*out 不动 */
    }
    *out = *e;
    return OM_OK;
}

/* ===================================================================
 * 解析面
 * =================================================================== */

OmRet om_partition_open(const OmPartitionRegistry *reg, const char *name,
                        OmPartitionHandle *h)
{
    if (!name || !h)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (!partition_reg_usable(reg))
    {
        return OM_ERR_NOT_FOUND; /* 空表语义 */
    }
    bool has_malformed = false;
    for (uint32_t i = 0; i < reg->count; i++)
    {
        const OmPartitionEntry *e = partition_entry_at(reg, i);
        if (!e->name)
        {
            /* 畸形条目：未经 registry_validate 的 RAM 来源表可能半填。不可
             * strcmp（硬故障）；不阻断其它条目，仅在最终未命中时以 INVALID_ARG
             * 报出配置错（而非查找未命中） */
            has_malformed = true;
            continue;
        }
        if (strcmp(e->name, name) != 0)
        {
            continue;
        }
        FlashDev *dev = NULL;
        OmRet ret = partition_geom_check(e, true, &dev);
        if (ret != OM_OK)
        {
            return ret;
        }
        const FlashGeometry *g = flash_geometry(dev);
        if (!g)
        {
            return OM_ERR_NOT_FOUND; /* 器件已解析必伴几何；防御 */
        }
        h->reg = reg;
        h->index = i;
        h->dev = dev; /* 解析一次，此后热路径直取 */
        return OM_OK;
    }
    return has_malformed ? OM_ERR_INVALID_ARG : OM_ERR_NOT_FOUND;
}

/* ===================================================================
 * 数据通路（句柄入口）
 * =================================================================== */

/** @brief 句柄域校验 + 权威条目/器件解析 + 分区内范围断言 */
static OmRet partition_range(const OmPartitionHandle *h, uint32_t off, size_t len,
                             const OmPartitionEntry **outEntry, FlashDev **outDev)
{
    if (!partition_handle_valid(h))
    {
        return OM_ERR_INVALID_ARG; /* 伪造/损坏句柄 */
    }
    const OmPartitionEntry *e = partition_entry_at(h->reg, h->index);
    if (off >= e->size || len > e->size - off)
    {
        return OM_ERR_RANGE; /* 双端越界（off < size 先行，杜绝 off+len 溢出）。
                              * 用 RANGE 而非 INVALID_ARG：这是调用方请求越出可访问
                              * 范围（改偏移即可），与 partition_geom_check 的表配置
                              * 错误（改表）行动不同，故分开报。越界须先于未对齐报出：
                              * 越界是更根本的错误 */
    }
    /* 器件与几何在 open 期解析并缓存——热路径不再做名字查找、
     * 几何查询与扇区友好校验（后者是 open 期一次性的配置判定）。
     * 仍保留的是"offset/size 恒从表取"：地址数值依旧不可伪造；
     * 让出的是 dev 指针的不可伪造性（显式交换，见 partition.h 的 @warning） */
    *outEntry = e;
    *outDev = h->dev;
    return OM_OK;
}

OmRet om_partition_read(const OmPartitionHandle *h, uint32_t off, void *buf, size_t len)
{
    if (!buf)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e;
    FlashDev *dev;
    OmRet ret = partition_range(h, off, len, &e, &dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    return flash_read(dev, e->offset + off, buf, len);
}

OmRet om_partition_write(const OmPartitionHandle *h, uint32_t off, const void *data, size_t len)
{
    if (!data)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e;
    FlashDev *dev;
    OmRet ret = partition_range(h, off, len, &e, &dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    return flash_write(dev, e->offset + off, data, len);
}

OmRet om_partition_erase(const OmPartitionHandle *h)
{
    if (!partition_handle_valid(h))
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e = partition_entry_at(h->reg, h->index);
    /* 整分区擦：扇区对齐由器件层强制（配置错误显式报 INVALID_ARG，不静默扩擦）；
     * 器件取句柄缓存 */
    return flash_erase(h->dev, e->offset, e->size);
}

OmRet om_partition_erase_range(const OmPartitionHandle *h, uint32_t off, size_t len)
{
    if (!partition_handle_valid(h))
    {
        return OM_ERR_INVALID_ARG; /* 句柄有效性先行——与家族其它入口一致 */
    }
    if (len == 0u)
    {
        return OM_OK; /* 无操作（句柄已确认有效） */
    }
    const OmPartitionEntry *e;
    FlashDev *dev;
    OmRet ret = partition_range(h, off, len, &e, &dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    /* 扇区对齐由器件层强制（flash_erase_validate）——非对齐显式报错，不扩擦 */
    return flash_erase(dev, e->offset + off, len);
}

/* ===================================================================
 * 能力与几何查询面——数据面之上只补这一层
 * =================================================================== */

uint32_t om_partition_capacity(const OmPartitionHandle *h)
{
    if (!partition_handle_valid(h))
    {
        return 0u;
    }
    return partition_entry_at(h->reg, h->index)->size;
}

const FlashGeometry *om_partition_dev_geom(const OmPartitionHandle *h)
{
    return partition_query_geom(h, NULL);
}

uint32_t om_partition_erase_unit_count(const OmPartitionHandle *h)
{
    const OmPartitionEntry *e = NULL;
    const FlashGeometry *g = partition_query_geom(h, &e);
    if (!g)
    {
        return 0u;
    }
    uint32_t end = e->offset + e->size;
    uint32_t cur = e->offset;
    uint32_t n = 0u;
    while (cur < end)
    {
        uint32_t unit = flash_erase_unit_size_at(g, cur);
        if (unit == 0u || unit > end - cur)
        {
            break; /* 防御：非扇区友好窗口（open 期已拦，此处不静默截断） */
        }
        cur += unit;
        n++;
    }
    return n;
}

OmRet om_partition_erase_unit_at(const OmPartitionHandle *h, uint32_t index,
                                 OmPartitionEraseUnit *out)
{
    if (!out)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e = NULL;
    const FlashGeometry *g = partition_query_geom(h, &e);
    if (!g)
    {
        return OM_ERR_INVALID_ARG;
    }
    return partition_erase_unit_probe(g, e->offset, e->size, index, out) ? OM_OK
                                                                         : OM_ERR_NOT_FOUND;
}

OmRet om_partition_erase_unit_covering(const OmPartitionHandle *h, uint32_t off,
                                       OmPartitionEraseUnit *out)
{
    if (!out)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e = NULL;
    const FlashGeometry *g = partition_query_geom(h, &e);
    if (!g)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (off >= e->size)
    {
        return OM_ERR_RANGE; /* 与家族其余入口同语义 */
    }
    uint32_t end = e->offset + e->size;
    uint32_t cur = e->offset;
    while (cur < end)
    {
        uint32_t unit = flash_erase_unit_size_at(g, cur);
        if (unit == 0u || unit > end - cur)
        {
            return OM_ERR_INVALID_ARG; /* 防御：非扇区友好窗口 */
        }
        uint32_t rel = cur - e->offset;
        if (off >= rel && off < rel + unit)
        {
            out->offset = rel;
            out->size = unit;
            return OM_OK;
        }
        cur += unit;
    }
    return OM_ERR_INVALID_ARG; /* 不可达：off < size 且窗口扇区友好 */
}

bool om_partition_is_uniform(const OmPartitionHandle *h)
{
    const OmPartitionEntry *e = NULL;
    const FlashGeometry *g = partition_query_geom(h, &e);
    if (!g)
    {
        return false; /* 保守方向：无法证明均一时，消费者必须逐单位取大小 */
    }
    uint32_t end = e->offset + e->size;
    uint32_t cur = e->offset;
    uint32_t first = 0u;
    while (cur < end)
    {
        uint32_t unit = flash_erase_unit_size_at(g, cur);
        if (unit == 0u || unit > end - cur)
        {
            return false;
        }
        if (first == 0u)
        {
            first = unit;
        }
        else if (unit != first)
        {
            return false;
        }
        cur += unit;
    }
    return true;
}

uint32_t om_partition_max_erase_unit(const OmPartitionHandle *h)
{
    const OmPartitionEntry *e = NULL;
    const FlashGeometry *g = partition_query_geom(h, &e);
    if (!g)
    {
        return 0u;
    }
    uint32_t end = e->offset + e->size;
    uint32_t cur = e->offset;
    uint32_t maxUnit = 0u;
    while (cur < end)
    {
        uint32_t unit = flash_erase_unit_size_at(g, cur);
        if (unit == 0u || unit > end - cur)
        {
            break;
        }
        if (unit > maxUnit)
        {
            maxUnit = unit;
        }
        cur += unit;
    }
    return maxUnit;
}

OmRet om_partition_is_erased(const OmPartitionHandle *h, uint32_t off, size_t len, bool *out)
{
    if (!out)
    {
        return OM_ERR_INVALID_ARG;
    }
    const OmPartitionEntry *e;
    FlashDev *dev;
    OmRet ret = partition_range(h, off, len, &e, &dev);
    if (ret != OM_OK)
    {
        return ret;
    }
    const FlashGeometry *g = flash_geometry(dev);
    if (!g)
    {
        return OM_ERR_NOT_FOUND;
    }
    /* 分块读 + 逐字节比对 erasedValue；不申请整段缓冲。
     * 不做写前自动校验——那要读回整段（I/O 翻倍），本层只提供自检能力 */
    uint8_t buf[PARTITION_IS_ERASED_CHUNK];
    uint8_t erased = g->erasedValue;
    uint32_t cur = e->offset + off;
    size_t remaining = len;
    bool all = true;
    while (remaining > 0u && all)
    {
        size_t take = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
        ret = flash_read(dev, cur, buf, take);
        if (ret != OM_OK)
        {
            return ret;
        }
        for (size_t i = 0u; i < take; i++)
        {
            if (buf[i] != erased)
            {
                all = false;
                break;
            }
        }
        cur += (uint32_t)take;
        remaining -= take;
    }
    *out = all;
    return OM_OK;
}

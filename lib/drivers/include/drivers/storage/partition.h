/**
 * @file   partition.h
 * @brief  分区表抽象——非易失存储设备的上层语义（boot/OTA/存储上层消费面）
 *
 * 接口与能力由器件层定义（见 pal_flash_dev.h），本模块不改写也不重述：分区视图
 * 是器件几何与能力的一份投影，含分区自己的容量。字节寻址器件（EEPROM/FRAM）与
 * 逻辑扇区器件（SD/eMMC）的分区形态另属（命名窗口 / 介质上的分区表），不并入。
 *
 * 裁剪姿态：可裁剪组件（非必备）——boot/OTA/存储上层才消费；静态库
 * "无引用不抽取"天然裁剪（无独立开关）。依赖面 = flash 设备族同步面，
 * 与 OM_FLASH_SYNC_ONLY / osal-none 裁剪组合兼容（本模块零 osal 依赖）。
 *
 * 形态（v2）：注册表由调用方持有（可整体 const、可驻 ROM 或调用方 RAM），
 * 句柄由 om_partition_open 产出。本模块零状态——无私有全局、无 init 顺序
 * 依赖、无并发保护需求（全部 API 天然可重入）。
 *
 * 信任模型：句柄内的 index 在操作期受 index < reg->count 域校验约束，
 * offset/size 恒从表取（调用方改不动）；几何正确性由 open 期按分区校验 +
 * 可选的全表 registry_validate 保证。
 *
 * 热路径：器件与几何指针在 open 期解析一次并缓存在句柄，每 I/O 的名字查找与
 * 几何校验随之移除。这是一次显式交换——让出 dev 指针的不可伪造性，换取热路径
 * 零解析；前提是设备永驻。详见句柄的 @warning。
 *
 * 错误码约定：注册表作为入参被检验（registry_validate / registry_at）→
 * OM_ERR_INVALID_ARG；注册表本身不可用或为空（reg NULL、table NULL、count==0）
 * → 空表语义：query / open 返回 OM_ERR_NOT_FOUND；名字未命中亦
 * OM_ERR_NOT_FOUND（例外：open 的表中存在畸形条目时改报 OM_ERR_INVALID_ARG，
 * 见其 @return）；registry_at 的 index >= count 同报 OM_ERR_NOT_FOUND。
 *
 * 偏移语义：便捷层 off 一律为分区内偏移；越界返回 OM_ERR_RANGE（与
 * OM_ERR_INVALID_ARG 分开：越界 = 调用方请求越出可访问范围，改偏移即可；
 * INVALID_ARG 保留给参数为空、句柄伪造、未对齐，以及表配置错误——后者由
 * open/registry_validate 报出，行动是改表）。
 * 对齐语义：erase/erase_range 的扇区对齐由底层器件访问层强制（配置错误在
 * 调用期显式报错，不静默波及邻区）。
 */

#ifndef OM_PARTITION_H
#define OM_PARTITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/om_def.h"
#include "drivers/peripheral/flash/pal_flash_dev.h" /* 句柄持 FlashDev/FlashGeometry 指针 */

typedef struct OmPartitionEntry {
    const char *name;    /* 逻辑名（表内唯一）——字符串本体在表内，只读 */
    const char *devName; /* 器件名（flash0…） */
    uint32_t offset;     /* 器件内偏移 */
    uint32_t size;       /* 分区大小 */
} OmPartitionEntry;

/** 注册表：表 + 条目数。可整体 const（ROM 常量）；表可指向 ROM 或调用方 RAM */
typedef struct OmPartitionRegistry {
    const OmPartitionEntry *table; /* 模块只读该表 */
    uint32_t count;
} OmPartitionRegistry;

/**
 * 编译期常量注册表：免运行期注册。
 * @warning 实参必须是数组（勿传指针）：传指针时 sizeof 比值退化为 0，
 *          得到空表语义（首次 open 即 NOT_FOUND）——响亮失败，不会越界。
 */
#define OM_PARTITION_REGISTRY(table_) \
    {(table_), (uint32_t)(sizeof(table_) / sizeof((table_)[0]))}

/** 擦除单位：分区内偏移加大小。大小在非均一几何下会变，故由数组元素携带；
 *  不存在分区级 eraseUnit 标量（同一器件可含多种扇区尺寸）。 */
typedef struct {
    uint32_t offset; /* 分区内偏移 */
    uint32_t size;   /* 该擦除单位的大小 */
} OmPartitionEraseUnit;

/** 句柄：open 产出，数据通路的唯一入口。调用方持有，不得手改字段。
 *
 *  @note 句柄按指针引用注册表——注册表对象须比它派生的任何句柄活得更久
 *        （栈上注册表返回后，其句柄即悬垂）。
 *  @note 句柄只存"注册表 + 索引 + 器件指针"三样：本模块零状态，没有模块静态量
 *        可承载它们，故在 open 期解析一次后随句柄存活。分区容量在权威表里、
 *        器件几何在器件指针后面——**两者都不在句柄里存副本**：多存一份就是一份
 *        无法被强制的一致性义务（"器件指向的几何"与"句柄里那份"须始终相等，
 *        而这靠约定而非靠结构保证）。
 *  @warning 信任模型：dev 是 open 期解析后缓存的指针，每 I/O 的名字解析与
 *        几何校验因此被移除。代价是 dev 成为可伪造字段（句柄被写坏即可指向
 *        任意设备）。仍保留的是 offset/size 恒从权威表取，以及句柄域校验
 *        index < reg->count。悬垂风险由"设备永驻"消解（PAL 无设备注销接口）；
 *        若将来引入注销，本交换必须重估。 */
typedef struct OmPartitionHandle {
    const OmPartitionRegistry *reg; /* 所属注册表 */
    uint32_t index;                 /* 操作期校验 index < reg->count */
    FlashDev *dev;                  /* open 期解析一次；设备永驻（见 @warning） */
} OmPartitionHandle;

/**
 * @brief 全表校验（可选，fail-fast）：结构 + 几何
 * @param reg 注册表
 * @return OM_OK / OM_ERR_INVALID_ARG（reg 空、count==0、条目字段非法、重名、
 *         或器件已注册时越容量/非扇区友好）
 * @note 器件未注册的条目跳过几何校验（顺序解耦）——与 v1 注册期语义一致；
 *       这些条目的几何正确性由 open 期兜底。应用在上电初始化调用；
 *       引导程序亦建议调用（配置错就停住的价值最高）。
 */
OmRet om_partition_registry_validate(const OmPartitionRegistry *reg);

/** @brief 条目数（reg 为 NULL 时返回 0） */
uint32_t om_partition_registry_count(const OmPartitionRegistry *reg);

/**
 * @brief 按索引取条目（枚举用，by-value 拷贝）
 * @return OM_OK / OM_ERR_INVALID_ARG / OM_ERR_NOT_FOUND（index 越界）
 */
OmRet om_partition_registry_at(const OmPartitionRegistry *reg, uint32_t index,
                               OmPartitionEntry *out);

/**
 * @brief 按名查询（by-value 纯信息，不碰器件，沿用 v1 语义）
 * @return OM_OK / OM_ERR_NOT_FOUND（含空表语义）/ OM_ERR_INVALID_ARG
 */
OmRet om_partition_query(const OmPartitionRegistry *reg, const char *name,
                         OmPartitionEntry *out);

/**
 * @brief 解析分区为句柄（名字 + 器件解析 + 几何校验）
 * @return OM_OK / OM_ERR_NOT_FOUND（名字未命中 / 器件不存在 / 空表）
 *         / OM_ERR_INVALID_ARG（越器件容量 / 非扇区友好 / 未命中且表中存在
 *         畸形条目 name==NULL）
 * @note open 不是全表校验器：首个名字命中即返回——表中别处存在畸形条目时，
 *       命中项仍返回 OM_OK；全表 fail-fast 是 registry_validate 的职责
 */
OmRet om_partition_open(const OmPartitionRegistry *reg, const char *name,
                        OmPartitionHandle *h);

/**
 * @brief 分区内偏移读（句柄域校验 + 双端越界校验）
 * @retval OM_ERR_RANGE         off/len 越出分区（越界先于其它检查报出）
 * @retval OM_ERR_INVALID_ARG   句柄伪造/损坏、buf 为空
 * @note len == 0 亦为合法调用（off 仍须在域内、buf 仍须非空）；与 erase_range
 *       的 len==0 无操作语义不同。write 同契约
 */
OmRet om_partition_read(const OmPartitionHandle *h, uint32_t off, void *buf, size_t len);

/** @brief 分区内偏移写（同 read 契约） */
OmRet om_partition_write(const OmPartitionHandle *h, uint32_t off, const void *data, size_t len);

/** @brief 整分区擦除（扇区对齐由器件层强制，配置错误显式报错） */
OmRet om_partition_erase(const OmPartitionHandle *h);

/**
 * @brief 分区内按范围擦除（扇区对齐由器件层强制，不静默扩擦）
 * @retval OM_ERR_RANGE         off/len 越出分区（越界先于对齐报出）
 * @retval OM_ERR_INVALID_ARG   句柄伪造/损坏、非扇区边界
 * @note len == 0 为无操作（返回 OM_OK）；句柄有效性仍先行校验
 */
OmRet om_partition_erase_range(const OmPartitionHandle *h, uint32_t off, size_t len);

/* ===================================================================
 * 能力与几何查询面——数据面之上只补这一层，
 * read/write/erase/erase_range 维持原样，不新增数据通路 API
 *
 * 分区只回答"我在哪、我多大"；器件的写/擦粒度、擦后值、能力位一律经器件查——
 * 分区层对它们不做解释，也不存副本。
 * =================================================================== */

/**
 * @brief 分区字节数（不是器件容量）
 * @return 分区大小；h 为 NULL 或句柄域非法时返回 0
 */
uint32_t om_partition_capacity(const OmPartitionHandle *h);

/**
 * @brief 分区背后的器件几何——写/擦粒度、擦后值、能力位的唯一来源
 * @return 器件几何；h 为 NULL 或句柄域非法时返回 NULL
 * @note 经句柄域校验后返回，调用方无须自持器件指针
 */
const FlashGeometry *om_partition_dev_geom(const OmPartitionHandle *h);

/**
 * @brief 分区窗口内的擦除单位个数
 * @return 个数；句柄域非法时返回 0
 * @note 单位为"器件扇区 ∩ 分区窗口"——分区在 open 期已校验扇区友好，
 *       故该集合恒为若干个完整扇区（非均一几何下大小不一）
 */
uint32_t om_partition_erase_unit_count(const OmPartitionHandle *h);

/**
 * @brief 按序号取擦除单位
 * @param index 序号（0 起）
 * @param out   输出（分区内偏移语义，与家族其余 API 一致）
 * @return OM_OK / OM_ERR_NOT_FOUND（index 越界）/ OM_ERR_INVALID_ARG（句柄或 out 非法）
 */
OmRet om_partition_erase_unit_at(const OmPartitionHandle *h, uint32_t index,
                                 OmPartitionEraseUnit *out);

/**
 * @brief 点查询：给定分区内偏移，问它落在哪个擦除单位
 * @return OM_OK / OM_ERR_RANGE（off 越出分区）/ OM_ERR_INVALID_ARG（句柄或 out 非法）
 */
OmRet om_partition_erase_unit_covering(const OmPartitionHandle *h, uint32_t off,
                                       OmPartitionEraseUnit *out);

/**
 * @brief 分区内擦除单位是否大小一致
 * @return false = 消费者必须逐单位取大小，不得假设均一
 */
bool om_partition_is_uniform(const OmPartitionHandle *h);

/**
 * @brief 分区内最大擦除单位（字节）；句柄域非法时返回 0
 * @note 存在的意义是让"取最大"成为显式决策，而非静默行为
 */
uint32_t om_partition_max_erase_unit(const OmPartitionHandle *h);

/**
 * @brief 已擦自检：与 erasedValue 逐字节比较
 * @param out 结果（true = 指定区间全为擦后值）
 * @retval OM_OK              *out 有效
 * @retval OM_ERR_RANGE       off/len 越出分区
 * @retval OM_ERR_INVALID_ARG 句柄或 out 非法
 * @note 两种消费者都需要：引导期校验他人的预擦（不信任、可检测）；
 *       运行期校验自己的擦除结果。本层不做写前自动校验
 *       （那要读回整段 = I/O 翻倍）——声明 + 消费者自检是最省形态
 */
OmRet om_partition_is_erased(const OmPartitionHandle *h, uint32_t off, size_t len, bool *out);

#endif /* OM_PARTITION_H */

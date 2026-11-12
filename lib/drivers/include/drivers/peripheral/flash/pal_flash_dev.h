/**
 * @file   pal_flash_dev.h
 * @brief  flash 器件接口（片内/外部 NOR 统一设备模型）
 *
 * 本层 = 一套接口 + 一份能力描述：
 * - 接口：读 / 写 / 擦 + 几何查询。擦除是接口里的一个操作，不是对所有实现的行为
 *   要求——擦除对该器件无意义时，其实现即无操作（见 FLASH_CAP_NO_ERASE_NEEDED）。
 * - 能力：描述器件的事实（有无擦除、擦后值、写与擦的粒度、寻址方式），集中在
 *   FlashGeometry.caps 与各数值字段中。
 * 消费者按能力分支，不按器件类型分支：需要擦除处先查能力位，判定空白处用
 * erasedValue。器件类型不是本接口的一部分——凡能被这套能力描述者，皆可用本接口；
 * 真写不进来的介质（字节寻址的 EEPROM/FRAM、逻辑扇区的 SD/eMMC）走各自的接口。
 *
 * 擦后校验的前提：逐字节严格比对成立的唯一前提是"擦后值确定且读取不引入误差"。
 * 带读取误码的介质（NAND 类）须按该介质的读取语义判定，不得沿用该形态。
 *
 * 设计思想：
 * - 介质无关单接口：片内 flash 与外部 SPI NOR 对上层呈现同一语义空间
 *   （设备内偏移 + 读/写/擦 + 几何查询），介质差异收敛在后端。
 * - 最小 API = 同步 read / 同步 write(program) / 同步 erase / 几何查询。
 * - "写" = program 语义：目标区必须已擦（调用方义务），本层不自动擦。
 * - 擦除 = 整扇区制：addr 与 addr+len 都必须是扇区边界（框架强制校验），不静默扩擦。
 *   擦后校验：擦除报告的"成功"未必为真（单元失效时数据仍不为擦后值），框架在
 *   擦除返回后读回比对 erasedValue——不通过即 OM_ERR_FLASH_UNUSABLE。
 *   本层是唯一能廉价发现此事的层；"以为已擦"是追加型存储最致命的假设。
 * - 执行模型：全部操作在调用者上下文同步执行，读/写/擦共用一把每设备睡眠互斥量。
 *   本层不持有线程，也不提供"提交即返回"的接口；需要后者的消费者在自己的层级
 *   开线程。
 * - 后端契约（承重条款）：write/erase 为同步实现，内部等待 BSY 必须让出 CPU
 *   （睡眠轮询或阻塞等硬件完成事件），禁止忙等。本层不阻塞调用者之外的任何
 *   执行体，其它任务不被饿死完全由本条款承担；read 有界。
 * - 每物理片一个设备实例，各持一把锁，互不影响。
 */

#ifndef __PAL_FLASH_DEV_H__
#define __PAL_FLASH_DEV_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/om_def.h"
#include "drivers/model/device.h"
#include "osal/osal_core.h"
#include "osal/osal_mutex.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 错误码（模块别名 → 通用错误码，正值体系）
 *===========================================================================*/

#define OM_ERR_FLASH_INVALID_ARG   OM_ERR_INVALID_ARG   /* 未对齐/参数为空/几何非法 */
#define OM_ERR_FLASH_RANGE         OM_ERR_RANGE         /* 越界：地址区间超出器件容量 */
#define OM_ERR_FLASH_IO            OM_ERR_IO            /* 硬件/物理错误 */
#define OM_ERR_FLASH_TIMEOUT       OM_ERR_TIMEOUT       /* 内部等待超时 */
#define OM_ERR_FLASH_NOT_SUPPORTED OM_ERR_NOT_SUPPORTED /* 不支持的操作（如未实现的控制命令） */

/* 模块特有码段（0x1000+）：无通用语义对应的模块专属错误 */
#define OM_ERR_FLASH_BASE ((OmRet)0x1000)
/** 该区结构性不可用——擦除后未达 erasedValue（擦除单元已失效）。
 *  与 OM_ERR_FLASH_IO 的分界：IO 表示这一次操作失败（可能瞬态，可重试）；
 *  UNUSABLE 表示这块区已验证不可用，继续写它无意义。
 *  消费者策略 = 跳过该区、换下一区继续（局部降级，不整体失败）。 */
#define OM_ERR_FLASH_UNUSABLE ((OmRet)(OM_ERR_FLASH_BASE + 1))

/** 擦后校验的总尝试次数（"擦除 + 校验"为一轮）。
 *  1（默认）：只做一轮——首次校验未过即报 OM_ERR_FLASH_UNUSABLE。
 *  大于 1：先重试再判死。重试把"这次没成"与"这块区坏了"拉开距离，
 *  重试次数越大，UNUSABLE 这个判定越站得住。
 *  只在"校验未过"时重试；后端擦除直接失败属 IO 语义，不在此处重试（归消费者）。 */
#ifndef OM_FLASH_ERASE_VERIFY_ATTEMPTS
#define OM_FLASH_ERASE_VERIFY_ATTEMPTS 1u
#endif

/*===========================================================================
 * 控制命令（经标准 Device 接口 control 通道）
 *===========================================================================*/

#define FLASH_CMD_GET_GEOMETRY (0x10U) /* args = const FlashGeometry ** 输出 */

/*===========================================================================
 * FlashGeometry —— 静态几何（适配器持有 const 实例）
 * 双模：sectorSize > 0 → 均匀扇区；== 0 → sectorRegions 非均一区域表
 *===========================================================================*/

/** 非均一扇区区域表条目（区域 = 同尺寸扇区的一段连续序列） */
typedef struct {
    uint32_t offset; /* 区域首扇区偏移 */
    uint32_t size;   /* 扇区大小（=擦除单位） */
    uint32_t count;  /* 连续扇区数 */
} FlashSectorRegion;

/* ---- 器件能力位图（位图 + 宏，与 gpio/pwm 的 caps 同款形态）----
 * 位一律取否定式：置位 = 声明该例外，未置位（含零初始化、漏填）= 保守默认。
 * 漏填因此永远落在安全侧——例如漏填 NO_ERASE_NEEDED 即"写前须擦"，不会让消费者
 * 误以为可以跳过擦除直接写。新增能力占用未用位，不加宽结构体。 */
#define FLASH_CAP_NO_ERASE_NEEDED (1u << 0) /* 擦除对本品无意义：其实现为无操作 */

typedef struct FlashGeometry {
    uint32_t capacity;    /* 总字节。全族按 32 位寻址，上限 4GB；超出须把 capacity/
                             分区偏移/擦除单位一并升为 64 位（勿单点放宽） */
    uint8_t erasedValue;  /* 擦后值（NOR 通常 = 0xFF）：器件物理特性，
                             擦除完成与否、区域是否空白皆以此为准 */
    uint16_t writeUnit;   /* 最小可编程单元（写对齐粒度，字节） */
    uint16_t pageSize;    /* 最优批量写页（0 = 无偏好） */
    uint32_t sectorSize;  /* 均匀扇区大小；0 = 使用 sectorRegions */
    uint16_t sectorCount; /* 均匀几何的扇区数：上限 65535，更多扇区改用 sectorRegions */
    const FlashSectorRegion *sectorRegions;
    uint32_t caps; /* 器件能力位图，见 FLASH_CAP_* */
} FlashGeometry;

/** @brief 写前是否须保证目标已擦（NO_ERASE_NEEDED 未置位即须擦）
 *  单一定义点：分区视图的写前须擦判定与器件层的擦后校验都以此为准。
 *  @note 当前注册校验要求几何声明擦除单位，故本判据对原生成员恒为真；免擦介质
 *        接入时须同时放开该校验并让 erase 走无操作路径。 */
static inline bool flash_geom_needs_erase(const FlashGeometry *g)
{
    return (g->caps & FLASH_CAP_NO_ERASE_NEEDED) == 0u;
}

/*===========================================================================
 * 后端 ops
 *
 * 后端契约（同步签名 + 让出硬纪律）：
 * - read   有界同步（XIP 直访 / 短总线事务），框架在调用者上下文直跑；
 * - write/erase 同步实现，框架同样在调用者上下文直跑；内部等待 BSY 必须让出
 *   （睡眠轮询 或 阻塞等硬件完成事件），禁止忙等占用 CPU。
 * 后端不感知设备锁，也不感知调用者上下文（何时被调、被谁调）。
 *===========================================================================*/

typedef struct FlashDev FlashDev;

typedef struct FlashOps {
    /** @brief 读：有界同步（任意偏移任意长度，框架已校验范围内） */
    OmRet (*read)(FlashDev *dev, uint32_t addr, void *buf, size_t len);

    /** @brief 写（program 语义）：同步实现 + 等待让出；addr/len 已对齐、目标已擦 */
    OmRet (*write)(FlashDev *dev, uint32_t addr, const void *data, size_t len);

    /** @brief 擦：同步实现 + 等待让出；addr/len 已校验为整扇区边界序列 */
    OmRet (*erase)(FlashDev *dev, uint32_t addr, size_t len);
} FlashOps;

/*===========================================================================
 * FlashDev —— flash 设备对象（每物理片一个实例；parent 必须首成员）
 *===========================================================================*/

typedef struct FlashDev {
    Device parent;             /* 标准 Device 外壳（type = DEVICE_TYPE_FLASH） */
    const FlashGeometry *geom; /* 指向适配器静态几何 */
    const FlashOps *ops;       /* 后端操作表 */
    void *hw;                  /* 适配器私有 */
    OsalMutex *lock;           /* 每设备睡眠互斥量：读/写/擦共用，register 期建立 */
} FlashDev;

/*===========================================================================
 * 生命周期 / 查找
 *===========================================================================*/

/**
 * @brief 注册 flash 设备（注册后可经 device_find/flash_find 查找）
 * @param dev  设备对象（静态或 BSS 分配；注册后由框架持有）
 * @param name 设备名（如 "flash0"），全局唯一
 * @param geom 静态几何（调用方持有，须存活至设备注销；本函数校验其基本合法性）
 * @param ops  后端操作表（read/write/erase 全必选）
 * @param hw   适配器私有指针
 * @retval OM_OK               成功
 * @retval OM_ERR_INVALID_ARG  参数为空 / 几何非法 / ops 缺项
 * @retval OM_ERR_CONFLICT     设备名已存在
 * @retval OM_ERR_NO_MEM       设备锁创建失败
 * @note 线程上下文；设备为永驻对象
 */
OmRet flash_register(FlashDev *dev, const char *name, const FlashGeometry *geom,
                     const FlashOps *ops, void *hw);

/**
 * @brief 按名字查找 flash 设备
 * @param name 设备名
 * @return 设备指针；不存在或类型不符返回 NULL
 */
FlashDev *flash_find(const char *name);

/** @brief 取设备静态几何（直接指针返回，无拷贝） */
const FlashGeometry *flash_geometry(FlashDev *dev);

/*===========================================================================
 * 核心 API
 *
 * 并发契约：
 * - read / write / erase 全部在调用者上下文同步执行并阻塞至完成；三者共用
 *   一把每设备睡眠互斥量，故同一设备上的操作天然互斥、逐次串行。
 * - 互斥量等待为无限阻塞：本层不因争用拒绝调用者；锁不可用属框架内部故障
 *   （注册后不应出现），按通用 IO 归因。
 * - 本层不持有线程。任何"提交即返回"的需求由上层自建线程承担。
 * - 禁止在 ISR 上下文调用本族 API（总线路径与互斥量等待均可阻塞）。
 * 地址语义：设备内偏移（0 起），越界返回 OM_ERR_RANGE，与未对齐、参数非法的
 * OM_ERR_INVALID_ARG 分开——越界改地址即可，未对齐要改长度或粒度，调用方行动
 * 不同，故不共用一个码。
 * 掉电语义：擦/写中途掉电目标区状态未定义（半擦/半写）——器件物理事实，
 * 本层不提供恢复原语；恢复由上层按自身事务规律处理。
 *===========================================================================*/

/**
 * @brief 读：同步直跑，有界快速
 * @param dev  设备对象
 * @param addr 设备内偏移
 * @param buf  输出缓冲
 * @param len  读取字节数
 * @retval OM_OK               成功（len 为 0 时直接成功，buf 可空）
 * @retval OM_ERR_INVALID_ARG  dev 为空 / buf 为空
 * @retval OM_ERR_RANGE        地址区间越出器件容量
 * @retval OM_ERR_FLASH_IO     后端物理读失败
 * @note 读无对齐限制；与写/擦经设备锁互斥；XIP 取指不经本 API，不受限制
 */
OmRet flash_read(FlashDev *dev, uint32_t addr, void *buf, size_t len);

/**
 * @brief 写（program 语义，不自动擦除）
 * @param dev  设备对象
 * @param addr 设备内偏移（须为 writeUnit 整数倍）
 * @param data 输入数据
 * @param len  写入字节数（须为 writeUnit 整数倍；0 为无操作）
 * @retval OM_OK               写入完成
 * @retval OM_ERR_INVALID_ARG  dev/data 为空 / 未对齐
 * @retval OM_ERR_RANGE        地址区间越出器件容量
 * @retval OM_ERR_FLASH_IO     后端物理写失败（含目标区不处于擦后态）
 */
OmRet flash_write(FlashDev *dev, uint32_t addr, const void *data, size_t len);

/**
 * @brief 擦（整扇区制）；返回值 = 擦除加擦后校验的合并结果
 * @param dev  设备对象
 * @param addr 设备内偏移（须为扇区边界）
 * @param len  擦除字节数（须为整扇区倍数；0 为无操作）
 * @retval OM_OK                 擦除完成，且已校验达擦后值
 * @retval OM_ERR_INVALID_ARG    dev 为空 / 非扇区边界
 * @retval OM_ERR_RANGE          地址区间越出器件容量
 * @retval OM_ERR_FLASH_IO       后端擦除失败——这一次操作失败（可能瞬态，可重试）
 * @retval OM_ERR_FLASH_UNUSABLE 擦后校验未达 erasedValue——该区结构性不可用
 * @note IO 与 UNUSABLE 刻意分开：前者重试同一区，后者跳过该区。合用一个码则
 *       消费者无从选择策略。
 */
OmRet flash_erase(FlashDev *dev, uint32_t addr, size_t len);

/*===========================================================================
 * 标准 Device 接口（read/write 薄转发到核心 API；write 通道 = 同步等待原语）
 * 注：device_read()/device_write() 通道要求设备先 device_open()（模型门控）
 *===========================================================================*/

OmRet flash_dev_init(Device *dev);
OmRet flash_dev_open(Device *dev, uint32_t oparam);
OmRet flash_dev_close(Device *dev);
size_t flash_dev_read(Device *dev, void *ctrl_info, void *data, size_t len);
size_t flash_dev_write(Device *dev, void *ctrl_info, void *data, size_t len);
OmRet flash_dev_control(Device *dev, size_t cmd, void *args);

#ifdef __cplusplus
}
#endif

#endif /* __PAL_FLASH_DEV_H__ */

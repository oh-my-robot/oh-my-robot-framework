/**
 * @file   pal_flash_dev.h
 * @brief  Flash 设备抽象层 v1（片内/外部 NOR 统一设备模型，异步写/擦）
 *
 * 设计思想：
 * - 介质无关单接口族：片内 flash 与外部 SPI NOR 对上层呈现同一语义空间
 *   （设备内偏移 + 读/写/擦 + 几何查询），介质差异收敛在后端。
 * - 最小 API = 同步 read / 异步 write(program) / 异步 erase / 几何查询。
 * - "写" = program 语义：目标区必须已擦（调用方义务），本层不自动擦。
 * - 擦除 = 整扇区制：addr 与 addr+len 都必须是扇区边界（框架强制校验），不静默扩擦。
 * - 执行模型：写/擦请求提交到设备所属"执行域"（每域一个 worker 线程）异步执行；
 *   完成经回调通知（worker 上下文）或同步等待原语。read 同步直跑（有界快速），
 *   与写/擦经设备 busy 协议互斥。
 * - 每物理片一个设备实例；多片逻辑与时序独立（各设备独立队列/忙状态）。
 * - 后端契约：write/erase 为同步实现但内部等待必须让出 CPU（睡眠或等硬件完成
 *   事件），禁止忙等；read 有界。
 *
 * 设计档案：docs/boot_ota/flash_dev_design.md（v1）/ flash_dev_impl_design.md
 */

#ifndef __PAL_FLASH_DEV_H__
#define __PAL_FLASH_DEV_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "async/workqueue.h"
#include "core/om_def.h"
#include "drivers/model/device.h"
#include "osal/osal_core.h"
#include "osal/osal_sem.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h" /* osal_sleep_ms（后端让出契约） */

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 错误码（模块别名 → 通用错误码，正值体系）
 *===========================================================================*/

#define OM_ERR_FLASH_INVALID_ARG   OM_ERR_INVALID_ARG   /* 越界/未对齐/参数非法 */
#define OM_ERR_FLASH_IO            OM_ERR_IO            /* 硬件/物理错误 */
#define OM_ERR_FLASH_BUSY          OM_ERR_BUSY          /* 设备忙/队列满/同域自锁 */
#define OM_ERR_FLASH_TIMEOUT       OM_ERR_TIMEOUT       /* 内部等待超时 */
#define OM_ERR_FLASH_NOT_SUPPORTED OM_ERR_NOT_SUPPORTED /* 不支持的操作（如无 OSAL 异步） */

/* 模块特有码段（0x1000+）预留 */
#define OM_ERR_FLASH_BASE ((OmRet)0x1000)

/* 请求队列深度（= 同设备并发等待者数上限；在途 1 + 重叠等待 1） */
#ifndef OM_FLASH_QUEUE_DEPTH
#define OM_FLASH_QUEUE_DEPTH 2u
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

typedef struct {
    uint32_t capacity;   /* 总字节 */
    uint8_t erasedValue; /* 擦后值（NOR 通常 = 0xFF）：器件物理特性，
                            擦除完成与否、区域是否空白皆以此为准 */
    uint16_t writeUnit;  /* 最小可编程单元（写对齐粒度，字节） */
    uint16_t pageSize;   /* 最优批量写页（0 = 无偏好） */
    uint32_t sectorSize; /* 均匀扇区大小；0 = 使用 sectorRegions */
    uint16_t sectorCount;
    const FlashSectorRegion *sectorRegions;
} FlashGeometry;

/*===========================================================================
 * 完成回调 / 后端 ops
 *
 * 后端契约（同步签名 + 让出硬纪律）：
 * - read   有界同步（XIP 直访 / 短总线事务），框架在调用者上下文直跑；
 * - write/erase 同步实现，由框架在域 worker 线程调用；内部等待 BSY 必须让出
 *   （睡眠轮询 或 阻塞等硬件完成事件），禁止忙等占用 CPU。
 * 后端不感知槽/队列/域。
 *===========================================================================*/

typedef struct FlashDev FlashDev;

/** @brief 异步操作完成回调（在域 worker 线程上下文执行；每请求至多一次） */
typedef void (*FlashDoneCb)(FlashDev *dev, OmRet status, void *param);

typedef struct FlashOps {
    /** @brief 读：有界同步（任意偏移任意长度，框架已校验范围内） */
    OmRet (*read)(FlashDev *dev, uint32_t addr, void *buf, size_t len);

    /** @brief 写（program 语义）：同步实现 + 等待让出；addr/len 已对齐、目标已擦 */
    OmRet (*write)(FlashDev *dev, uint32_t addr, const void *data, size_t len);

    /** @brief 擦：同步实现 + 等待让出；addr/len 已校验为整扇区边界序列 */
    OmRet (*erase)(FlashDev *dev, uint32_t addr, size_t len);
} FlashOps;

/*===========================================================================
 * FlashDomain —— 执行域（每个域 = 一个 worker 线程，串行执行域内设备请求）
 * 每物理片默认独立域（并行/等待期交错）；共享域 = 显式省线程选项。
 * 域是适配器接线概念：调用者（上层）不感知。
 *===========================================================================*/

typedef struct FlashDomain {
    Workqueue wq; /* worker 线程 + 请求 FIFO */
} FlashDomain;

/**
 * @brief 初始化执行域（= workqueue init + start；建 worker 线程）
 * @param dom   域对象（静态分配；须清零后传入）
 * @param name  域名（调试/线程名）
 * @param prio  worker 线程优先级（OSAL_PRIO_<band>_BASE + offset）
 * @param stack worker 线程栈大小（字节）
 * @retval OM_OK 成功
 * @note 线程上下文；无 OSAL 编译（OM_FLASH_SYNC_ONLY）返回 OM_ERR_NOT_SUPPORTED
 */
OmRet flash_domain_init(FlashDomain *dom, const char *name, uint32_t prio, uint32_t stack);

/*===========================================================================
 * FlashDev —— flash 设备对象（每物理片一个实例；parent 必须首成员）
 *===========================================================================*/

typedef struct FlashRequest FlashRequest;

/** 异步请求（嵌入设备定长槽；Work 嵌入使请求可入域队列）
 *  槽空闲判定 = work 状态 IDLE（workqueue 在 func 返回后置 IDLE——
 *  故完成回调（func 内）与同步等待（work_wait_idle）都不会提前复用同槽） */
struct FlashRequest {
    Work work; /* 域 workqueue 工作项；IDLE = 槽空闲 */
    FlashDev *dev;
    uint32_t type; /* FLASH_REQ_WRITE / FLASH_REQ_ERASE */
    uint32_t addr;
    size_t len;
    const void *data; /* write 数据（调用方缓冲须存活至完成） */
    FlashDoneCb done; /* 用户回调（可 NULL = 入队即忘） */
    void *param;
    OmRet result;
};

typedef struct FlashDev {
    Device parent;             /* 标准 Device 外壳（type = DEVICE_TYPE_FLASH） */
    const FlashGeometry *geom; /* 指向适配器静态几何 */
    const FlashOps *ops;       /* 后端操作表 */
    void *hw;                  /* 适配器私有 */
    /* 框架 v1 */
    FlashDomain *domain;                      /* 执行域；register 传 NULL 时用 autoDomain */
    FlashDomain autoDomain;                   /* 默认独立域（内嵌） */
    FlashRequest slots[OM_FLASH_QUEUE_DEPTH]; /* 定长请求槽（空闲槽才可提交） */
    volatile bool busy;                       /* 设备执行协议：写/擦执行中 or read 直跑中 */
    FlashDoneCb doneCb;                       /* 设备级完成通知（setter 设置；async 提交时快照到请求） */
    void *doneParam;                          /* 完成通知上下文 */
} FlashDev;

/*===========================================================================
 * 生命周期 / 查找
 *===========================================================================*/

/**
 * @brief 注册 flash 设备（注册后可经 device_find/flash_find 查找）
 * @param dev    设备对象（静态或 BSS 分配；注册后由框架持有）
 * @param name   设备名（如 "flash0"），全局唯一
 * @param geom   静态几何（调用方持有，须存活至设备注销；本函数校验其基本合法性）
 * @param ops    后端操作表（read/write/erase 全必选）
 * @param hw     适配器私有指针
 * @param domain 执行域；NULL = 框架为设备创建独立域（内嵌 autoDomain）
 * @retval OM_OK               成功
 * @retval OM_ERR_INVALID_ARG  参数为空 / 几何非法 / ops 缺项
 * @retval OM_ERR_CONFLICT     设备名已存在
 * @retval OM_ERR_NO_MEM       内部资源创建失败（独立域线程）
 * @note 线程上下文；设备为永驻对象（v0 无注销接口）
 */
OmRet flash_register(FlashDev *dev, const char *name, const FlashGeometry *geom,
                     const FlashOps *ops, void *hw, FlashDomain *domain);

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
 * - write/erase 为异步（提交请求队列即返回）；每设备定长队列（深度
 *   OM_FLASH_QUEUE_DEPTH），满时返回 OM_ERR_BUSY。
 * - 完成回调在设备所属域 worker 线程上下文执行，每请求至多一次；回调触发时
 *   请求槽已释放（回调内可安全再提交 async）。
 * - 回调内（域 worker 上下文）对**同域**设备调用同步等待原语 → 返回
 *   OM_ERR_BUSY（自锁拒绝）；对异域设备同步等待允许。
 * - read 同步直跑；设备有写/擦在执行或另一读在跑时返回 OM_ERR_BUSY（杜绝混合
 *   读）；XIP 取指不经本 API，不受限制。
 * - 禁止在 ISR 上下文调用本族 API（read 的总线路径与提交路径可阻塞）。
 * 地址语义：设备内偏移（0 起），越界返回 OM_ERR_INVALID_ARG。
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
 * @retval OM_ERR_INVALID_ARG  dev 为空 / buf 为空 / 越界
 * @retval OM_ERR_BUSY         设备有写/擦执行中或读在跑
 * @retval OM_ERR_FLASH_IO     后端物理读失败
 * @note 读无对齐限制；与写/擦经 busy 协议互斥；ISR 读走 XIP 直访通道
 */
OmRet flash_read(FlashDev *dev, uint32_t addr, void *buf, size_t len);

/**
 * @brief 设置设备级完成通知（框架风格：回调经 setter 注册到设备，async API 推导）
 * @param dev   设备对象
 * @param done  完成回调（NULL = 取消通知）；worker 上下文、每 async 请求至多一次
 * @param param 回调上下文
 * @note 回调在设备所属域 worker 线程执行；async 请求在**提交时**快照
 *       done/param（中途 setter 变更不影响已在途请求）；
 *       同步等待原语（flash_write/erase）不触发本通知（调用者自阻塞）
 */
void flash_set_done_cb(FlashDev *dev, FlashDoneCb done, void *param);

/**
 * @brief 异步写（program 语义，不自动擦除；完成经设备级通知 flash_set_done_cb）
 * @param dev  设备对象
 * @param addr 设备内偏移（须为 writeUnit 整数倍）
 * @param data 输入数据（调用方缓冲须存活至完成通知）
 * @param len  写入字节数（须为 writeUnit 整数倍）
 * @retval OM_OK               已提交
 * @retval OM_ERR_INVALID_ARG  dev/data 为空 / 越界 / 未对齐
 * @retval OM_ERR_BUSY         队列满
 * @retval OM_ERR_NOT_SUPPORTED 无 OSAL 编译（OM_FLASH_SYNC_ONLY）
 */
OmRet flash_write_async(FlashDev *dev, uint32_t addr, const void *data, size_t len);

/**
 * @brief 异步擦（整扇区制；完成经设备级通知 flash_set_done_cb）
 * @param dev  设备对象
 * @param addr 设备内偏移（须为扇区边界）
 * @param len  擦除字节数（须为整扇区倍数；0 为无操作，不触发通知）
 * @retval OM_OK               已提交
 * @retval OM_ERR_INVALID_ARG  dev 为空 / 越界 / 非扇区边界
 * @retval OM_ERR_BUSY         队列满
 */
OmRet flash_erase_async(FlashDev *dev, uint32_t addr, size_t len);

/**
 * @brief 同步写等待原语（提交 + 阻塞至完成；执行在域 worker，调用线程睡眠让出）
 * @note 域 worker 上下文（回调内）对同域设备调用 → 返回 OM_ERR_BUSY
 */
OmRet flash_write(FlashDev *dev, uint32_t addr, const void *data, size_t len);

/**
 * @brief 同步擦等待原语（同上）
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

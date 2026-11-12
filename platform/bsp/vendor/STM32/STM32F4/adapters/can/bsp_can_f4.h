/**
 * @file  bsp_can_f4.h
 * @brief STM32F4 家族 CAN BSP 共享适配层：类型 + 家族常量 + 板数据契约
 * @details 板瘦身（参考 Zephyr devicetree "板=数据、驱动=通用"）：F4 家族一份共享实现
 *          （bsp_can_f4.c / bsp_can_f4_it.c），各板只提供数据表（实例/波特率/引脚/中断）。
 *
 *          ── 板数据契约（板 opt-in 本适配层后必须提供）────────────────────
 *          板侧 include/bsp_can.h（shim，约 10 行）：
 *            #define USE_CAN1 / USE_CAN2     实例启用开关
 *            #define BSP_CAN_COUNT            实例数（= gBspCan 条目数）
 *            #include "can/bsp_can_f4.h"
 *          板侧 source/peripherals/can/bsp_can_data.c：
 *            BspCan_s gBspCan[BSP_CAN_COUNT]              实例表（顺序 = CanIdx_e 枚举）
 *            const CanTimeCfg BspCanBitTimeTable[]        波特率表（末尾须有 psc=0 哨兵项）
 *            const BspCanPinCfg BspCanPinTable[][2]       每实例 [RX, TX] 引脚
 *            const BspCanIrqCfg  BspCanIrqTable[][4]      每实例 [RX0, RX1, TX, SCE] 中断
 *          适配层文件由板 lua 显式引用（opt-in 铁律：永不进 vendor/chip sources）：
 *            selfreg_sources   += .../adapters/can/bsp_can_f4.c    （含 OM_INIT 自注册）
 *            override_sources  += .../adapters/can/bsp_can_f4_it.c  （强 ISR，覆盖启动文件 weak）
 *
 *          契约守卫：bsp_can_f4.c 侧 #ifndef BSP_CAN_COUNT #error（漏板宏编译期报错）；
 *          数据文件内 _Static_assert(BSP_CAN_COUNT == 数组条目数)（宏/数组漂移兜底）。
 */

#ifndef __BSP_CAN_F4_H__
#define __BSP_CAN_F4_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "drivers/peripheral/can/pal_can_dev.h"
#include "stm32f4xx_hal.h"

/*===========================================================================
 * 家族常量（STM32F4 bxCAN 双 CAN 共享 28 个 filter bank，分界 14）
 *===========================================================================*/

#define BSP_CAN_FILTER_SPLIT_BANK      (14U)
#define BSP_CAN_MAX_FILTER_BANK_COUNT  (28U)

/* REG_PARAMS 默认值（板可在 include 前 #define 覆盖） */
#ifndef CAN1_REG_PARAMS
#define CAN1_REG_PARAMS (CAN_REG_INT_TX | CAN_REG_INT_RX)
#endif
#ifndef CAN2_REG_PARAMS
#define CAN2_REG_PARAMS (CAN_REG_INT_TX | CAN_REG_INT_RX)
#endif

/*===========================================================================
 * 类型
 *===========================================================================*/

/** @brief CAN 实例索引（顺序必须与 gBspCan 数组条目一致） */
typedef enum {
#ifdef USE_CAN1
    BSP_CAN1_IDX,
#endif
#ifdef USE_CAN2
    BSP_CAN2_IDX
#endif
} CanIdx_e;

typedef struct BspCan *BspCan_t;

/**
 * @brief CAN 实例描述（板数据定义，共享实现消费）
 * @note  handle 必须为首字段：ISR 里以 (BspCan_t)hcan 反解（hcan 即 handle 首地址）。
 */
typedef struct BspCan {
    CAN_HandleTypeDef handle;   /**< HAL 句柄（首字段，ISR 反解用） */
    HalCanHandler parent;       /**< 框架侧 CAN 句柄 */
    char *name;                 /**< 设备名（如 "can1"） */
    uint32_t regparams;         /**< 注册 IO 能力标志 */
    const uint8_t *hwBankList;  /**< 本实例可用的 filter bank 列表（首项 = 起始 bank） */
    uint8_t hwBankCount;        /**< 可用 bank 数 */
} BspCan_s;

/* 注意：不能用 (BANK_LIST)[0] 做静态初始化（非编译期常量，armclang 拒绝）；
 * 起始 bank 在运行期以 hwBankList[0] 取。 */
#define BSP_CAN_STATIC_INIT(INSTANCE, NAME, REGPARAMS, BANK_LIST, BANK_CNT) \
    (BspCan_s)                                                              \
    {                                                                       \
        .handle.Instance = (INSTANCE), .name = (NAME),                      \
        .regparams = (REGPARAMS), .hwBankList = (BANK_LIST),                \
        .hwBankCount = (BANK_CNT),                                          \
    }

/** @brief 引脚配置（板数据表项） */
typedef struct BspCanPinCfg {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t af;
} BspCanPinCfg;

/** @brief 中断配置（板数据表项） */
typedef struct BspCanIrqCfg {
    IRQn_Type irqn;
    uint8_t preemptPrio;
} BspCanIrqCfg;

/*===========================================================================
 * 板数据契约声明（板 opt-in 适配层即承诺提供；不完整维度 extern）
 *===========================================================================*/

extern BspCan_s gBspCan[];                       /* 条目顺序 = CanIdx_e */
extern const CanTimeCfg BspCanBitTimeTable[];    /* 波特率表（末尾 psc=0 哨兵项） */
extern const BspCanPinCfg BspCanPinTable[][2];   /* [实例][RX, TX] */
extern const BspCanIrqCfg  BspCanIrqTable[][4];  /* [实例][RX0, RX1, TX, SCE] */

/* BSP_CAN_COUNT 由板 shim bsp_can.h 定义（编译期实例数） */

void bsp_can_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_CAN_F4_H__ */

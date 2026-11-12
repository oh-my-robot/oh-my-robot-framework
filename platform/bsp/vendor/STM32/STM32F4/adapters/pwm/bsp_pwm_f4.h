/**
 * @file  bsp_pwm_f4.h
 * @brief STM32F4 家族 PWM BSP 共享适配层：类型 + 家族常量 + 板数据契约
 * @details 板瘦身（参考 Zephyr devicetree "板=数据、驱动=通用"）：F4 家族一份共享实现
 *          （bsp_pwm_f4.c），各板只提供数据表（实例/能力/引脚）。
 *
 *          ── 语义对齐说明（两板原实现漂移的收敛决策）─────────────────────
 *          - MOE（高级定时器主输出使能）：由 HAL_TIM_PWM_Start 内部按
 *            IS_TIM_BREAK_INSTANCE 对高级定时器自动使能，BSP 不再手写特判
 *            （rm-a 的 Instance==TIM8 比较与 rm-c 的无条件使能均为冗余防御）。
 *          - BDTR 配置：无条件对所有实例执行——非高级定时器无 BDTR 寄存器，
 *            写入被硬件静默丢弃（Linux pwm-stm32 同款哲学）。
 *          - AOE 关：break 事件后禁止 PWM 自动恢复输出（Linux 2019 年移除
 *            AOE 位的安全对齐决策）。
 *          - timerHz：register 期预计算缓存，ISR 内 setPulse 直接读取，避免
 *            运行期查询 RCC 频率（rm-c 原实现，rm-a 缺失）。
 *          - counterWidth：能力表诚实声明 16——两板实现统一走 16-bit PSC/ARR
 *            路径（rm-a 原把 TIM2/TIM5 声明为 32 与实现矛盾，已修正）。
 *
 *          ── 板数据契约（板 opt-in 本适配层后必须提供）────────────────────
 *          板侧 include/bsp_pwm.h（shim，约 10 行）：
 *            #define BSP_PWM_COUNT            实例数（= gBspPwm 条目数）
 *            #include "pwm/bsp_pwm_f4.h"
 *          板侧 source/peripherals/pwm/bsp_pwm_data.c：
 *            BspPwm gBspPwm[BSP_PWM_COUNT]                 实例表（HAL 句柄 + 名称）
 *            const PwmCapability gBspPwmCap[BSP_PWM_COUNT] 能力表（与实例同序）
 *            const BspPwmPinCfg gBspPwmPinTable[BSP_PWM_COUNT][4] 引脚 AF 表（不足 4 通道补 {0} 哨兵）
 *          适配层文件由板 lua 显式引用（opt-in 铁律：永不进 vendor/chip sources）：
 *            selfreg_sources += .../adapters/pwm/bsp_pwm_f4.c  （含 OM_INIT 自注册）
 *
 *          契约守卫：bsp_pwm_f4.c 侧 #ifndef BSP_PWM_COUNT #error（漏板宏编译期报错）；
 *          数据文件内 typedef char 编译期校验（宏/数组漂移兜底）。
 */

#ifndef __BSP_PWM_F4_H__
#define __BSP_PWM_F4_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "drivers/peripheral/pwm/pal_pwm_dev.h"
#include "stm32f4xx_hal.h"

/*===========================================================================
 * 家族常量
 *===========================================================================*/

/** @brief 每控制器最大通道数（两板 TIM 均为 1-4 通道） */
#define BSP_PWM_MAX_CHANNELS (4U)

/*===========================================================================
 * 类型
 *===========================================================================*/

/**
 * @brief PWM 实例描述（板数据定义，共享实现消费）
 */
typedef struct BspPwm {
    TIM_HandleTypeDef  timHandle;        /**< HAL 定时器句柄 */
    PwmController      parent;           /**< 框架侧控制器 */
    const char        *name;             /**< 设备名（如 "pwm1"） */
    PwmChannelState    chState[BSP_PWM_MAX_CHANNELS]; /**< per-channel 状态，框架层读写 */
    uint32_t           timerHz;          /**< 预计算定时器时钟（register 期，ISR 直接读） */
} BspPwm;

/** @brief 引脚配置（板数据表项：GPIO AF 复用） */
typedef struct BspPwmPinCfg {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       af;
} BspPwmPinCfg;

/*===========================================================================
 * 板数据契约声明（板 opt-in 适配层即承诺提供；不完整维度 extern）
 *===========================================================================*/

extern BspPwm gBspPwm[];                        /* 实例表（条目顺序 = 控制器编号 pwm1..N） */
extern const PwmCapability gBspPwmCap[];        /* 能力表（与实例同序） */
extern const BspPwmPinCfg gBspPwmPinTable[][BSP_PWM_MAX_CHANNELS]; /* [实例][通道] */

/* BSP_PWM_COUNT 由板 shim bsp_pwm.h 定义（编译期实例数） */

void bsp_pwm_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_PWM_F4_H__ */

/**
 * @file    bsp_pwm_data.c
 * @brief   rm-a-board PWM 板数据（板瘦身：板=数据，实现见 pwm/bsp_pwm_f4.c）
 * @details RoboMaster A 板（STM32F427IIH6）：
 *          pwm1 (TIM2):  J30 A-D,  PA0-3,  APB1 84MHz
 *          pwm2 (TIM8):  J30 E-H,  PI5/6/7+PI2, APB2 168MHz（高级定时器）
 *          pwm3 (TIM4):  J29,      PD12-15, APB1 84MHz
 *          pwm4 (TIM5):  J29,      PH10/11/12+PI0, APB1 84MHz
 *          counterWidth 诚实声明 16：实现统一走 16-bit PSC/ARR 路径。
 */

#include "bsp_pwm.h"

/* ===== 实例表（顺序 = 控制器编号 pwm1..N） ===== */

BspPwm gBspPwm[BSP_PWM_COUNT] = {
    { .timHandle = { .Instance = TIM2 }, .name = "pwm1" },
    { .timHandle = { .Instance = TIM8 }, .name = "pwm2" },
    { .timHandle = { .Instance = TIM4 }, .name = "pwm3" },
    { .timHandle = { .Instance = TIM5 }, .name = "pwm4" },
};

/* ===== 能力表（与实例同序） ===== */

const PwmCapability gBspPwmCap[BSP_PWM_COUNT] = {
    /* pwm1: TIM2, APB1 84MHz, 4 通道 */
    [0] = { .numChannels = 4, .minPeriodNs = 1000, .maxPeriodNs = 1000000000,
            .resolutionHz = 84000000,
            .caps = PWM_CAP_POLARITY_NORMAL | PWM_CAP_POLARITY_INVERSED,
            .counterWidth = 16 },
    /* pwm2: TIM8, APB2 168MHz, 4 通道（高级定时器） */
    [1] = { .numChannels = 4, .minPeriodNs = 1000, .maxPeriodNs = 1000000000,
            .resolutionHz = 168000000,
            .caps = PWM_CAP_POLARITY_NORMAL | PWM_CAP_POLARITY_INVERSED,
            .counterWidth = 16 },
    /* pwm3: TIM4, APB1 84MHz, 4 通道 */
    [2] = { .numChannels = 4, .minPeriodNs = 1000, .maxPeriodNs = 1000000000,
            .resolutionHz = 84000000,
            .caps = PWM_CAP_POLARITY_NORMAL | PWM_CAP_POLARITY_INVERSED,
            .counterWidth = 16 },
    /* pwm4: TIM5, APB1 84MHz, 4 通道 */
    [3] = { .numChannels = 4, .minPeriodNs = 1000, .maxPeriodNs = 1000000000,
            .resolutionHz = 84000000,
            .caps = PWM_CAP_POLARITY_NORMAL | PWM_CAP_POLARITY_INVERSED,
            .counterWidth = 16 },
};

/* ===== 引脚 AF 表 [实例][通道]（不足 BSP_PWM_MAX_CHANNELS 补 {0} 哨兵，port=NULL 终止） ===== */

const BspPwmPinCfg gBspPwmPinTable[BSP_PWM_COUNT][BSP_PWM_MAX_CHANNELS] = {
    /* pwm1: TIM2 CH1-4, AF1 */
    [0] = { {GPIOA, GPIO_PIN_0, GPIO_AF1_TIM2}, {GPIOA, GPIO_PIN_1, GPIO_AF1_TIM2},
            {GPIOA, GPIO_PIN_2, GPIO_AF1_TIM2}, {GPIOA, GPIO_PIN_3, GPIO_AF1_TIM2} },
    /* pwm2: TIM8 CH1-4, AF3 */
    [1] = { {GPIOI, GPIO_PIN_5, GPIO_AF3_TIM8}, {GPIOI, GPIO_PIN_6, GPIO_AF3_TIM8},
            {GPIOI, GPIO_PIN_7, GPIO_AF3_TIM8}, {GPIOI, GPIO_PIN_2, GPIO_AF3_TIM8} },
    /* pwm3: TIM4 CH1-4, AF2 */
    [2] = { {GPIOD, GPIO_PIN_12, GPIO_AF2_TIM4}, {GPIOD, GPIO_PIN_13, GPIO_AF2_TIM4},
            {GPIOD, GPIO_PIN_14, GPIO_AF2_TIM4}, {GPIOD, GPIO_PIN_15, GPIO_AF2_TIM4} },
    /* pwm4: TIM5 CH1-4, AF2 */
    [3] = { {GPIOH, GPIO_PIN_10, GPIO_AF2_TIM5}, {GPIOH, GPIO_PIN_11, GPIO_AF2_TIM5},
            {GPIOH, GPIO_PIN_12, GPIO_AF2_TIM5}, {GPIOI, GPIO_PIN_0, GPIO_AF2_TIM5} },
};

/* ===== 契约校验：宏与数组条目数漂移编译期兜底（armclang 兼容的 C99 技巧） ===== */

typedef char bsp_pwm_count_ok[(BSP_PWM_COUNT == (sizeof(gBspPwm) / sizeof(gBspPwm[0]))) ? 1 : -1];
typedef char bsp_pwm_cap_count_ok[(BSP_PWM_COUNT == (sizeof(gBspPwmCap) / sizeof(gBspPwmCap[0]))) ? 1 : -1];

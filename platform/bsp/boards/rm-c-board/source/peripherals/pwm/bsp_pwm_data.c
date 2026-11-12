/**
 * @file    bsp_pwm_data.c
 * @brief   rm-c-board PWM 板数据（板瘦身：板=数据，实现见 pwm/bsp_pwm_f4.c）
 * @details RoboMaster C 板（STM32F407IGH6）：
 *          pwm1 (TIM1): PE9/11/13/14, AF1, APB2 168MHz（高级定时器）
 *          pwm2 (TIM8): PC6/PI6/PI7,  AF3, APB2 168MHz（高级定时器，3 通道）
 */

#include "bsp_pwm.h"

/* ===== 实例表（顺序 = 控制器编号 pwm1..N） ===== */

BspPwm gBspPwm[BSP_PWM_COUNT] = {
    { .timHandle = { .Instance = TIM1 }, .name = "pwm1" },
    { .timHandle = { .Instance = TIM8 }, .name = "pwm2" },
};

/* ===== 能力表（与实例同序） ===== */

const PwmCapability gBspPwmCap[BSP_PWM_COUNT] = {
    /* pwm1: TIM1, APB2 168MHz, 4 通道（高级定时器） */
    [0] = { .numChannels = 4, .minPeriodNs = 1000, .maxPeriodNs = 1000000000,
            .resolutionHz = 168000000,
            .caps = PWM_CAP_POLARITY_NORMAL | PWM_CAP_POLARITY_INVERSED,
            .counterWidth = 16 },
    /* pwm2: TIM8, APB2 168MHz, 3 通道（高级定时器） */
    [1] = { .numChannels = 3, .minPeriodNs = 1000, .maxPeriodNs = 1000000000,
            .resolutionHz = 168000000,
            .caps = PWM_CAP_POLARITY_NORMAL | PWM_CAP_POLARITY_INVERSED,
            .counterWidth = 16 },
};

/* ===== 引脚 AF 表 [实例][通道]（不足 BSP_PWM_MAX_CHANNELS 补 {0} 哨兵，port=NULL 终止） ===== */

const BspPwmPinCfg gBspPwmPinTable[BSP_PWM_COUNT][BSP_PWM_MAX_CHANNELS] = {
    /* pwm1: TIM1 CH1-4, AF1 */
    [0] = { {GPIOE, GPIO_PIN_9, GPIO_AF1_TIM1}, {GPIOE, GPIO_PIN_11, GPIO_AF1_TIM1},
            {GPIOE, GPIO_PIN_13, GPIO_AF1_TIM1}, {GPIOE, GPIO_PIN_14, GPIO_AF1_TIM1} },
    /* pwm2: TIM8 CH1-3, AF3（CH4 空） */
    [1] = { {GPIOC, GPIO_PIN_6, GPIO_AF3_TIM8}, {GPIOI, GPIO_PIN_6, GPIO_AF3_TIM8},
            {GPIOI, GPIO_PIN_7, GPIO_AF3_TIM8}, {0} },
};

/* ===== 契约校验：宏与数组条目数漂移编译期兜底（armclang 兼容的 C99 技巧） ===== */

typedef char bsp_pwm_count_ok[(BSP_PWM_COUNT == (sizeof(gBspPwm) / sizeof(gBspPwm[0]))) ? 1 : -1];
typedef char bsp_pwm_cap_count_ok[(BSP_PWM_COUNT == (sizeof(gBspPwmCap) / sizeof(gBspPwmCap[0]))) ? 1 : -1];

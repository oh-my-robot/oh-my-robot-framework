/**
 * @file    bsp_pwm_init.c
 * @brief   rm-a-board PWM GPIO AF 硬件预初始化
 * @details 为 PWM 控制器配置 GPIO 复用功能、使能 GPIO 时钟。
 *          在 bsp_pwm_register() 前调用，由 bsp_pwm_register() 内部调用。
 *
 *          RoboMaster A 板 PWM 引脚映射：
 *          ┌─────────────────────────────────────────────────────────┐
 *          │ J30 (PWM×8):                                           │
 *          │   标号 A→H:  PA0  PA1  PA2  PA3  PI5  PI6  PI7  PI2   │
 *          │   定时器:     TIM2_CH1-4 (AF1)     TIM8_CH1-4 (AF3)    │
 *          │                                                         │
 *          │ J29 (PWM×8):                                           │
 *          │   定时器:     TIM4_CH1-4 (AF2)     TIM5_CH1-4 (AF2)    │
 *          │   TIM4:       PD12, PD13, PD14, PD15                   │
 *          │   TIM5:       PH10, PH11, PH12, PI0                    │
 *          └─────────────────────────────────────────────────────────┘
 */

#include "bsp_pwm.h"

/**
 * @brief  配置单个 GPIO 为 AF 推挽输出
 */
static void bsp_pwm_config_af_pin(GPIO_TypeDef *port, uint16_t pin, uint8_t af)
{
    GPIO_InitTypeDef init = {0};
    init.Pin       = pin;
    init.Mode      = GPIO_MODE_AF_PP;
    init.Pull      = GPIO_NOPULL;
    init.Speed     = GPIO_SPEED_FREQ_HIGH;
    init.Alternate = af;
    HAL_GPIO_Init(port, &init);
}

/**
 * @brief  PWM GPIO AF 预初始化（bsp_pwm_register 内部调用）
 */
void bsp_pwm_init_gpio(void)
{
    /* === TIM2 (J30 A-D): PA0-PA3, AF1 === */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    bsp_pwm_config_af_pin(GPIOA, GPIO_PIN_0 | GPIO_PIN_1
                                | GPIO_PIN_2 | GPIO_PIN_3,
                           GPIO_AF1_TIM2);

    /* === TIM8 (J30 E-H): PI5-PI7 + PI2, AF3 === */
    __HAL_RCC_GPIOI_CLK_ENABLE();
    bsp_pwm_config_af_pin(GPIOI, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7, GPIO_AF3_TIM8);
    bsp_pwm_config_af_pin(GPIOI, GPIO_PIN_2, GPIO_AF3_TIM8);

    /* === TIM4 (J29): PD12-PD15, AF2 === */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    bsp_pwm_config_af_pin(GPIOD, GPIO_PIN_12 | GPIO_PIN_13
                                | GPIO_PIN_14 | GPIO_PIN_15,
                           GPIO_AF2_TIM4);

    /* === TIM5 (J29): PH10-PH12 + PI0, AF2 === */
    __HAL_RCC_GPIOH_CLK_ENABLE();
    bsp_pwm_config_af_pin(GPIOH, GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12,
                           GPIO_AF2_TIM5);
    __HAL_RCC_GPIOI_CLK_ENABLE();
    bsp_pwm_config_af_pin(GPIOI, GPIO_PIN_0, GPIO_AF2_TIM5);
}

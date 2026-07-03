/**
 * @file    bsp_pwm_init.c
 * @brief   rm-c-board PWM GPIO AF 预初始化
 * @details RoboMaster C 板 PWM 定时器通道↔引脚映射：
 *          TIM1_CH1: PE9   (AF1)
 *          TIM1_CH2: PE11  (AF1)
 *          TIM1_CH3: PE13  (AF1)
 *          TIM1_CH4: PE14  (AF1)
 *          TIM8_CH1: PC6   (AF3)
 *          TIM8_CH2: PI6   (AF3)
 *          TIM8_CH3: PI7   (AF3)
 */

#include "bsp_pwm.h"

static void bsp_pwm_config_af_pin(GPIO_TypeDef *port, uint16_t pin, uint8_t af)
{
    GPIO_InitTypeDef init = {0};
    init.Pin = pin; init.Mode = GPIO_MODE_AF_PP; init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH; init.Alternate = af;
    HAL_GPIO_Init(port, &init);
}

void bsp_pwm_init_gpio(void)
{
    /* TIM1: PE9, PE11, PE13, PE14, AF1 */
    __HAL_RCC_GPIOE_CLK_ENABLE();
    bsp_pwm_config_af_pin(GPIOE, GPIO_PIN_9,  GPIO_AF1_TIM1);
    bsp_pwm_config_af_pin(GPIOE, GPIO_PIN_11, GPIO_AF1_TIM1);
    bsp_pwm_config_af_pin(GPIOE, GPIO_PIN_13, GPIO_AF1_TIM1);
    bsp_pwm_config_af_pin(GPIOE, GPIO_PIN_14, GPIO_AF1_TIM1);

    /* TIM8: PC6, PI6, PI7, AF3 */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    bsp_pwm_config_af_pin(GPIOC, GPIO_PIN_6, GPIO_AF3_TIM8);
    __HAL_RCC_GPIOI_CLK_ENABLE();
    bsp_pwm_config_af_pin(GPIOI, GPIO_PIN_6, GPIO_AF3_TIM8);
    bsp_pwm_config_af_pin(GPIOI, GPIO_PIN_7, GPIO_AF3_TIM8);
}

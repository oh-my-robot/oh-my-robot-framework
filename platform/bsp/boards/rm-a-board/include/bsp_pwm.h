/**
 * @file    bsp_pwm.h
 * @brief   rm-a-board PWM BSP 配置
 * @details RoboMaster A 板（STM32F427IIH6）PWM 接口定义：
 *          J30 (PWM×8, Item 8):  TIM2_CH1-4 + TIM8_CH1-4
 *          J29 (PWM×8):          TIM4_CH1-4 + TIM5_CH1-4
 *
 *          APB1 定时器时钟 = 84MHz（TIM2/4/5），
 *          APB2 定时器时钟 = 168MHz（TIM8）。
 */

#ifndef __BSP_PWM_H__
#define __BSP_PWM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "drivers/peripheral/pwm/pal_pwm_dev.h"
#include "stm32f4xx_hal.h"

/** A 板启用的 PWM 控制器数量 */
#define BSP_PWM_COUNT   (4U)

/**
 * @brief  BSP PWM 控制器实例
 */
typedef struct {
    TIM_HandleTypeDef  timHandle;  /**< STM32 HAL 定时器句柄 */
    PwmController       parent;     /**< 框架控制器 */
    const char         *name;       /**< 控制器名称 */
    PwmChannelState     chState[4];     /**< per-channel 状态，框架层读写 */
} BspPwm;

/** GPIO AF 预初始化（bsp_pwm_register 内部调用） */
void bsp_pwm_init_gpio(void);

/** 注册所有 PWM 控制器到框架 */
void bsp_pwm_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_PWM_H__ */

/**
 * @file    bsp_pwm.h
 * @brief   rm-c-board PWM BSP 配置
 * @details RoboMaster C 板（STM32F407IGH6）PWM 接口：
 *          ┌──────┬────────┬───────┬──────┐
 *          │ 接口 │ 定时器 │ 通道  │ 引脚 │
 *          ├──────┼────────┼───────┼──────┤
 *          │  C1  │  TIM1  │  CH1  │ PE9  │  AF1, APB2 168MHz
 *          │  C2  │  TIM1  │  CH2  │ PE11 │  AF1, APB2 168MHz
 *          │  C3  │  TIM1  │  CH3  │ PE13 │  AF1, APB2 168MHz
 *          │  C4  │  TIM1  │  CH4  │ PE14 │  AF1, APB2 168MHz
 *          │  C5  │  TIM8  │  CH1  │ PC6  │  AF3, APB2 168MHz
 *          │  C6  │  TIM8  │  CH2  │ PI6  │  AF3, APB2 168MHz
 *          │  C7  │  TIM8  │  CH3  │ PI7  │  AF3, APB2 168MHz
 *          └──────┴────────┴───────┴──────┘
 */

#ifndef __BSP_PWM_H__
#define __BSP_PWM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "drivers/peripheral/pwm/pal_pwm_dev.h"
#include "stm32f4xx_hal.h"

#define BSP_PWM_COUNT   (2U)

typedef struct {
    TIM_HandleTypeDef  timHandle;
    PwmController       parent;
    const char         *name;
    PwmChannelState     chState[4];     /**< per-channel 状态，框架层读写 */
    uint32_t            timerHz;       /**< 预计算定时器时钟，ISR 直接读取 */
} BspPwm;

void bsp_pwm_init_gpio(void);
void bsp_pwm_register(void);

#ifdef __cplusplus
}
#endif

#endif

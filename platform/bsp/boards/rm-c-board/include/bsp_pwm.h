/**
 * @file  bsp_pwm.h
 * @brief rm-c-board PWM 板配置 shim（板瘦身：类型/宏/契约已上移 pwm/bsp_pwm_f4.h）
 * @details 本文件只留板配置；全部类型与板数据契约见共享适配层头。
 *          引脚映射：pwm1(TIM1)=PE9/11/13/14、pwm2(TIM8)=PC6/PI6/PI7（3 通道）。
 */
#ifndef __BSP_PWM_H__
#define __BSP_PWM_H__
#ifdef OM_USE_BOARDCFG
#include "om_boardcfg.h" /* 工程板级覆写（boardcfg 契约见 ADR-0017）——须在默认值定义之前 */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 实例数：必须等于 bsp_pwm_data.c 中 gBspPwm 条目数（数据文件内编译期校验） */
#define BSP_PWM_COUNT (2U)

#include "pwm/bsp_pwm_f4.h"

#ifdef __cplusplus
}
#endif

#endif /* __BSP_PWM_H__ */

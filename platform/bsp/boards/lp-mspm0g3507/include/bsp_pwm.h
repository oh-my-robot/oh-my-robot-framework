/**
 * @file bsp_pwm.h
 * @brief MSPM0G3507 PWM BSP — TIMG 控制器注册
 *
 * 控制器:
 *   pwm1 (TIMG7):  CCP0=PA28, CCP1=PA31
 *   pwm2 (TIMG12): CCP0=PB13
 *
 * 全部 BUSCLK 32MHz, 16-bit 边沿对齐 PWM。
 */

#ifndef __OM_BSP_PWM_H__
#define __OM_BSP_PWM_H__

#include "drivers/peripheral/pwm/pal_pwm_dev.h"
#include "ti/devices/msp/msp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_PWM_COUNT  2

typedef struct {
    GPTIMER_Regs    *handle;
    PwmController    parent;
    const char      *name;
    PwmChannelState  chState[2];
    uint32_t         savedPeriod;
    uint32_t         savedPulse[2];
    PwmPolarity      polarity[2];
} BspPwm;

void bsp_pwm_register(void);

#ifdef __cplusplus
}
#endif

#endif

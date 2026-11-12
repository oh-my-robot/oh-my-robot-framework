/**
 * @file bsp_tima_pwm.h
 * @brief MSPM0G3507 TIMA PWM BSP — 高级定时器 PWM 控制器
 *
 * 控制器:
 *   pwm3 (TIMA1): CCP0=PB4, CCP1=PB1
 *   pwm4 (TIMA0): CCP1=PB12, CCP2=PB20  (ch0→CCP1, ch1→CCP2 内部偏移)
 *
 * TIMA vs TIMG: API 相同 (均映射到 DL_Timer_* 公共层),
 * TIMA 额外支持互补输出/死区/故障保护, 但当前未启用。
 *
 * BUSCLK 32MHz, 16-bit 边沿对齐 PWM。
 */

#ifndef __OM_BSP_TIMA_PWM_H__
#define __OM_BSP_TIMA_PWM_H__

#include "drivers/peripheral/pwm/pal_pwm_dev.h"
#include "ti/devices/msp/msp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_TIMA_PWM_COUNT  2

typedef struct {
    GPTIMER_Regs    *handle;          /**< TIMA0 / TIMA1 */
    PwmController    parent;
    const char      *name;
    PwmChannelState  chState[2];
    uint32_t         savedPeriod;
    uint32_t         savedPulse[2];
    PwmPolarity      polarity[2];
    uint8_t          ch_offset;       /**< 框架 ch→硬件 CCP 偏移 (TIMA0=1, TIMA1=0) */
} BspTimaPwm;

void bsp_tima_pwm_register(void);

#ifdef __cplusplus
}
#endif

#endif

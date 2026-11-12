/**
 * @file bsp_tima_pwm_init.c
 * @brief MSPM0G3507 TIMA PWM 硬件初始化 (TIMA1 + TIMA0)
 *
 * 引脚:
 *   TIMA1: PB4=CCP0, PB1=CCP1
 *   TIMA0: PB12=CCP1, PB20=CCP2 (CCP0 无引出, 仅内部用作时基参考)
 */

#include "bsp_tima_pwm.h"
#include "ti/driverlib/dl_timera.h"
#include "ti/driverlib/dl_gpio.h"
#include "ti/driverlib/m0p/dl_core.h"

#define BSP_TIMA_MANUAL_PINMUX

/*---------------------------------------------------------------------------*/
/* Pinmux                                                                     */
/*---------------------------------------------------------------------------*/

static void bsp_tima_pinmux(GPTIMER_Regs *tim)
{
#ifdef BSP_TIMA_MANUAL_PINMUX
    if (tim == TIMA1) {
        /* PB4 → TIMA1 CCP0 */
        DL_GPIO_initPeripheralOutputFunction(
            IOMUX_PINCM17, IOMUX_PINCM17_PF_TIMA1_CCP0);
        DL_GPIO_enableOutput(GPIOB, DL_GPIO_PIN_4);

        /* PB1 → TIMA1 CCP1 */
        DL_GPIO_initPeripheralOutputFunction(
            IOMUX_PINCM13, IOMUX_PINCM13_PF_TIMA1_CCP1);
        DL_GPIO_enableOutput(GPIOB, DL_GPIO_PIN_1);
    }
    else if (tim == TIMA0) {
        /* PB12 → TIMA0 CCP1 */
        DL_GPIO_initPeripheralOutputFunction(
            IOMUX_PINCM29, IOMUX_PINCM29_PF_TIMA0_CCP1);
        DL_GPIO_enableOutput(GPIOB, DL_GPIO_PIN_12);

        /* PB20 → TIMA0 CCP2 */
        DL_GPIO_initPeripheralOutputFunction(
            IOMUX_PINCM48, IOMUX_PINCM48_PF_TIMA0_CCP2);
        DL_GPIO_enableOutput(GPIOB, DL_GPIO_PIN_20);
    }
#endif
}

/*---------------------------------------------------------------------------*/
/* 单实例初始化                                                               */
/*---------------------------------------------------------------------------*/

static void bsp_tima_init_one(GPTIMER_Regs *tim, bool is_four_cc)
{
    DL_TimerA_reset(tim);
    DL_TimerA_enablePower(tim);
    delay_cycles(16);

    bsp_tima_pinmux(tim);

    static const DL_Timer_ClockConfig clkCfg = {
        .clockSel = DL_TIMER_CLOCK_BUSCLK, .divideRatio = DL_TIMER_CLOCK_DIVIDE_1, .prescale = 0U };
    DL_TimerA_setClockConfig(tim, (DL_TimerA_ClockConfig *)&clkCfg);

    DL_TimerA_PWMConfig pwmCfg = {
        .pwmMode = DL_TIMER_PWM_MODE_EDGE_ALIGN, .period = 3200,
        .isTimerWithFourCC = is_four_cc, .startTimer = DL_TIMER_START };
    DL_TimerA_initPWMMode(tim, &pwmCfg);

    DL_TimerA_setCounterControl(tim,
        DL_TIMER_CZC_CCCTL0_ZCOND, DL_TIMER_CAC_CCCTL0_ACOND, DL_TIMER_CLC_CCCTL0_LCOND);

    /*
     * 配置所有 CCP 通道的输出方向。
     * TIMA1: CCP0+CCP1 均有引脚引出 → 两通道输出
     * TIMA0: CCP1+CCP2 有引脚, CCP0/CCP3 无引出但仍需输出方向使能
     */
    uint32_t ccp_dir = is_four_cc
        ? (DL_TIMER_CC0_OUTPUT | DL_TIMER_CC1_OUTPUT |
           DL_TIMER_CC2_OUTPUT | DL_TIMER_CC3_OUTPUT)
        : (DL_TIMER_CC0_OUTPUT | DL_TIMER_CC1_OUTPUT);
    DL_TimerA_setCCPDirection(tim, ccp_dir);

    /* 所有 CCP 通道统一配置 */
    for (int i = 0; i < (is_four_cc ? 4 : 2); i++) {
        DL_TimerA_setCaptureCompareOutCtl(tim,
            DL_TIMER_CC_OCTL_INIT_VAL_LOW,
            DL_TIMER_CC_OCTL_INV_OUT_DISABLED,
            DL_TIMER_CC_OCTL_SRC_FUNCVAL,
            (DL_TIMER_CC_INDEX)i);
        DL_TimerA_setCaptCompUpdateMethod(tim,
            DL_TIMER_CC_UPDATE_METHOD_IMMEDIATE,
            (DL_TIMER_CC_INDEX)i);
        DL_TimerA_setCaptureCompareValue(tim, 0, (DL_TIMER_CC_INDEX)i);
    }

    DL_TimerA_enableClock(tim);
}

/*---------------------------------------------------------------------------*/
/* Per-instance 入口                                                         */
/*---------------------------------------------------------------------------*/

void bsp_tima_pre_init_tima1(void) { bsp_tima_init_one(TIMA1, false); }
void bsp_tima_pre_init_tima0(void) { bsp_tima_init_one(TIMA0, true);  }

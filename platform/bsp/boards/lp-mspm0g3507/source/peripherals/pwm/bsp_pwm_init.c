/**
 * @file bsp_pwm_init.c
 * @brief MSPM0G3507 TIMG PWM 硬件初始化 (TIMG7 + TIMG12)
 *
 * 引脚:
 *   TIMG7:  PA28=CCP0, PA31=CCP1
 *   TIMG12: PB13=CCP0
 */

#include "bsp_pwm.h"
#include "ti/driverlib/dl_timerg.h"
#include "ti/driverlib/dl_gpio.h"
#include "ti/driverlib/m0p/dl_core.h"

#define BSP_PWM_MANUAL_PINMUX

static void bsp_pwm_pinmux(GPTIMER_Regs *tim)
{
#ifdef BSP_PWM_MANUAL_PINMUX
    if (tim == TIMG7) {
        DL_GPIO_initPeripheralOutputFunction(
            IOMUX_PINCM3, IOMUX_PINCM3_PF_TIMG7_CCP0);
        DL_GPIO_enableOutput(GPIOA, DL_GPIO_PIN_28);
        DL_GPIO_initPeripheralOutputFunction(
            IOMUX_PINCM6, IOMUX_PINCM6_PF_TIMG7_CCP1);
        DL_GPIO_enableOutput(GPIOA, DL_GPIO_PIN_31);
    }
    else if (tim == TIMG12) {
        DL_GPIO_initPeripheralOutputFunction(
            IOMUX_PINCM30, IOMUX_PINCM30_PF_TIMG12_CCP0);
        DL_GPIO_enableOutput(GPIOB, DL_GPIO_PIN_13);
    }
#endif
}

static void bsp_pwm_init_one(GPTIMER_Regs *tim, bool has_two_channels)
{
    DL_TimerG_reset(tim);
    DL_TimerG_enablePower(tim);
    delay_cycles(16);
    bsp_pwm_pinmux(tim);

    static const DL_TimerG_ClockConfig clkCfg = {
        .clockSel = DL_TIMER_CLOCK_BUSCLK, .divideRatio = DL_TIMER_CLOCK_DIVIDE_1, .prescale = 0U };
    DL_TimerG_setClockConfig(tim, (DL_TimerG_ClockConfig *)&clkCfg);

    DL_TimerG_PWMConfig pwmCfg = {
        .pwmMode = DL_TIMER_PWM_MODE_EDGE_ALIGN, .period = 3200,
        .isTimerWithFourCC = false, .startTimer = DL_TIMER_START };
    DL_TimerG_initPWMMode(tim, &pwmCfg);

    DL_TimerG_setCounterControl(tim,
        DL_TIMER_CZC_CCCTL0_ZCOND, DL_TIMER_CAC_CCCTL0_ACOND, DL_TIMER_CLC_CCCTL0_LCOND);
    DL_TimerG_setCCPDirection(tim,
        has_two_channels ? (DL_TIMER_CC0_OUTPUT | DL_TIMER_CC1_OUTPUT) : DL_TIMER_CC0_OUTPUT);

    DL_TimerG_setCaptureCompareOutCtl(tim,
        DL_TIMER_CC_OCTL_INIT_VAL_LOW, DL_TIMER_CC_OCTL_INV_OUT_DISABLED,
        DL_TIMER_CC_OCTL_SRC_FUNCVAL, DL_TIMERG_CAPTURE_COMPARE_0_INDEX);
    DL_TimerG_setCaptCompUpdateMethod(tim,
        DL_TIMER_CC_UPDATE_METHOD_IMMEDIATE, DL_TIMERG_CAPTURE_COMPARE_0_INDEX);
    DL_TimerG_setCaptureCompareValue(tim, 0, DL_TIMER_CC_0_INDEX);

    if (has_two_channels) {
        DL_TimerG_setCaptureCompareOutCtl(tim,
            DL_TIMER_CC_OCTL_INIT_VAL_LOW, DL_TIMER_CC_OCTL_INV_OUT_DISABLED,
            DL_TIMER_CC_OCTL_SRC_FUNCVAL, DL_TIMERG_CAPTURE_COMPARE_1_INDEX);
        DL_TimerG_setCaptCompUpdateMethod(tim,
            DL_TIMER_CC_UPDATE_METHOD_IMMEDIATE, DL_TIMERG_CAPTURE_COMPARE_1_INDEX);
        DL_TimerG_setCaptureCompareValue(tim, 0, DL_TIMER_CC_1_INDEX);
    }

    DL_TimerG_enableClock(tim);
}

void bsp_pwm_pre_init_timg7(void)  { bsp_pwm_init_one(TIMG7,  true);  }
void bsp_pwm_pre_init_timg12(void) { bsp_pwm_init_one(TIMG12, false); }

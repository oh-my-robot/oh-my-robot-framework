/**
 * @file bsp_pwm_impl.c
 * @brief MSPM0G3507 PWM BSP — PwmOps + 注册 (TIMG7 + TIMG12)
 */

#include "bsp_pwm.h"
#include "core/om_def.h"
#include "ti/driverlib/dl_timerg.h"

extern void bsp_pwm_pre_init_timg7(void);
extern void bsp_pwm_pre_init_timg12(void);

static const PwmCapability gPwmCap[BSP_PWM_COUNT] = {
    { .numChannels = 2, .minPeriodNs = 1000, .maxPeriodNs = 2048000,
      .resolutionHz = 32000000,
      .caps = PWM_CAP_POLARITY_NORMAL | PWM_CAP_POLARITY_INVERSED,
      .counterWidth = 16 },
    { .numChannels = 1, .minPeriodNs = 1000, .maxPeriodNs = 2048000,
      .resolutionHz = 32000000,
      .caps = PWM_CAP_POLARITY_NORMAL | PWM_CAP_POLARITY_INVERSED,
      .counterWidth = 16 },
};

static inline BspPwm *self_from_ctrl(PwmController *ctrl) { return (BspPwm *)ctrl->parent.handle; }

static inline uint32_t calc_compare(uint32_t period, uint32_t pulse, PwmPolarity pol)
{
    if (pulse > period) pulse = period;
    if (pol == PWM_POLARITY_INVERSED) {
        if (pulse >= period) return 0;
        return period - pulse;
    }
    return pulse;
}

static OmRet bsp_pwm_channel_config(PwmController *ctrl, uint8_t ch,
    uint32_t pcyc, uint32_t pucyc, PwmPolarity pol)
{
    BspPwm *b = self_from_ctrl(ctrl);
    DL_TimerG_setLoadValue(b->handle, pcyc - 1U);
    b->savedPeriod = pcyc; b->savedPulse[ch] = pucyc; b->polarity[ch] = pol;
    DL_TimerG_setCaptureCompareValue(b->handle, calc_compare(pcyc, pucyc, pol), (DL_TIMER_CC_INDEX)ch);
    return OM_OK;
}

static OmRet bsp_pwm_channel_enable(PwmController *ctrl, uint8_t ch)
{
    BspPwm *b = self_from_ctrl(ctrl);
    DL_TimerG_setCaptureCompareValue(b->handle,
        calc_compare(b->savedPeriod, b->savedPulse[ch], b->polarity[ch]), (DL_TIMER_CC_INDEX)ch);
    DL_TimerG_startCounter(b->handle);
    return OM_OK;
}

static OmRet bsp_pwm_channel_disable(PwmController *ctrl, uint8_t ch)
{
    DL_TimerG_setCaptureCompareValue(self_from_ctrl(ctrl)->handle, 0, (DL_TIMER_CC_INDEX)ch);
    return OM_OK;
}

static OmRet bsp_pwm_channel_set_pulse(PwmController *ctrl, uint8_t ch, uint32_t pucyc)
{
    BspPwm *b = self_from_ctrl(ctrl);
    b->savedPulse[ch] = pucyc;
    DL_TimerG_setCaptureCompareValue(b->handle,
        calc_compare(b->savedPeriod, pucyc, b->polarity[ch]), (DL_TIMER_CC_INDEX)ch);
    return OM_OK;
}

static const PwmOps gPwmOps = {
    .channelConfig = bsp_pwm_channel_config, .channelEnable = bsp_pwm_channel_enable,
    .channelDisable = bsp_pwm_channel_disable, .channelSetPulse = bsp_pwm_channel_set_pulse,
};

static BspPwm gBspPwm[BSP_PWM_COUNT] = {
    { .handle = TIMG7,  .name = "pwm1" },
    { .handle = TIMG12, .name = "pwm2" },
};

void bsp_pwm_register(void)
{
    bsp_pwm_pre_init_timg7();
    pwm_controller_register(&gBspPwm[0].parent, gBspPwm[0].name, &gPwmCap[0], &gPwmOps, &gBspPwm[0], gBspPwm[0].chState);
    bsp_pwm_pre_init_timg12();
    pwm_controller_register(&gBspPwm[1].parent, gBspPwm[1].name, &gPwmCap[1], &gPwmOps, &gBspPwm[1], gBspPwm[1].chState);
}

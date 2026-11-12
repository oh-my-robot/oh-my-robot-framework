/**
 * @file bsp_tima_pwm_impl.c
 * @brief MSPM0G3507 TIMA PWM BSP — PwmOps + 注册
 *
 * pwm3 (TIMA1): CCP0=PB4, CCP1=PB1 (2ch, 无偏移)
 * pwm4 (TIMA0): CCP1=PB12, CCP2=PB20 (2ch, ch_offset=1 映射)
 *
 * PwmOps 实现与 TIMG 完全一致 (均调用 DL_Timer_* 公共 API),
 * 唯一差异: 通过 ch_offset 将框架通道号映射到实际 CCP 索引。
 */

#include "bsp_tima_pwm.h"
#include "core/om_def.h"
#include "ti/driverlib/dl_timera.h"

extern void bsp_tima_pre_init_tima1(void);
extern void bsp_tima_pre_init_tima0(void);

/*----------------------------------------------------------------------------*/
/* 能力声明                                                                   */
/*----------------------------------------------------------------------------*/

static const PwmCapability gTimaCap[BSP_TIMA_PWM_COUNT] = {
    /* pwm3: TIMA1, 2ch */
    { .numChannels = 2, .minPeriodNs = 1000, .maxPeriodNs = 2048000,
      .resolutionHz = 32000000,
      .caps = PWM_CAP_POLARITY_NORMAL | PWM_CAP_POLARITY_INVERSED,
      .counterWidth = 16 },
    /* pwm4: TIMA0, 2ch (CCP1=ch0, CCP2=ch1) */
    { .numChannels = 2, .minPeriodNs = 1000, .maxPeriodNs = 2048000,
      .resolutionHz = 32000000,
      .caps = PWM_CAP_POLARITY_NORMAL | PWM_CAP_POLARITY_INVERSED,
      .counterWidth = 16 },
};

/*----------------------------------------------------------------------------*/
/* 内部辅助                                                                   */
/*----------------------------------------------------------------------------*/

static inline BspTimaPwm *self(PwmController *c) { return (BspTimaPwm *)c->parent.handle; }

/** 框架 ch → 硬件 CCP 索引 */
static inline DL_TIMER_CC_INDEX hw_ch(BspTimaPwm *b, uint8_t ch)
    { return (DL_TIMER_CC_INDEX)(ch + b->ch_offset); }

static inline uint32_t calc_cmp(uint32_t p, uint32_t d, PwmPolarity pol)
{
    if (d > p) d = p;
    if (pol == PWM_POLARITY_INVERSED) { if (d >= p) return 0; return p - d; }
    return d;
}

/*----------------------------------------------------------------------------*/
/* PwmOps                                                                     */
/*----------------------------------------------------------------------------*/

static OmRet tima_config(PwmController *c, uint8_t ch,
    uint32_t pcyc, uint32_t dcyc, PwmPolarity pol)
{
    BspTimaPwm *b = self(c);
    DL_TimerA_setLoadValue(b->handle, pcyc - 1U);
    b->savedPeriod = pcyc; b->savedPulse[ch] = dcyc; b->polarity[ch] = pol;
    DL_TimerA_setCaptureCompareValue(b->handle, calc_cmp(pcyc, dcyc, pol), hw_ch(b, ch));
    return OM_OK;
}

static OmRet tima_enable(PwmController *c, uint8_t ch)
{
    BspTimaPwm *b = self(c);
    DL_TimerA_setCaptureCompareValue(b->handle,
        calc_cmp(b->savedPeriod, b->savedPulse[ch], b->polarity[ch]), hw_ch(b, ch));
    DL_TimerA_startCounter(b->handle);
    return OM_OK;
}

static OmRet tima_disable(PwmController *c, uint8_t ch)
{
    DL_TimerA_setCaptureCompareValue(self(c)->handle, 0, hw_ch(self(c), ch));
    return OM_OK;
}

static OmRet tima_set_pulse(PwmController *c, uint8_t ch, uint32_t dcyc)
{
    BspTimaPwm *b = self(c);
    b->savedPulse[ch] = dcyc;
    DL_TimerA_setCaptureCompareValue(b->handle,
        calc_cmp(b->savedPeriod, dcyc, b->polarity[ch]), hw_ch(b, ch));
    return OM_OK;
}

static const PwmOps gTimaOps = {
    .channelConfig = tima_config, .channelEnable = tima_enable,
    .channelDisable = tima_disable, .channelSetPulse = tima_set_pulse,
};

/*----------------------------------------------------------------------------*/
/* 实例 + 注册                                                                */
/*----------------------------------------------------------------------------*/

static BspTimaPwm gBspTima[BSP_TIMA_PWM_COUNT] = {
    { .handle = TIMA1, .name = "pwm3", .ch_offset = 0 },  /* CCP0,1 → ch0,1 */
    { .handle = TIMA0, .name = "pwm4", .ch_offset = 1 },  /* CCP1,2 → ch0,1 */
};

void bsp_tima_pwm_register(void)
{
    bsp_tima_pre_init_tima1();
    pwm_controller_register(&gBspTima[0].parent, gBspTima[0].name,
        &gTimaCap[0], &gTimaOps, &gBspTima[0], gBspTima[0].chState);

    bsp_tima_pre_init_tima0();
    pwm_controller_register(&gBspTima[1].parent, gBspTima[1].name,
        &gTimaCap[1], &gTimaOps, &gBspTima[1], gBspTima[1].chState);
}

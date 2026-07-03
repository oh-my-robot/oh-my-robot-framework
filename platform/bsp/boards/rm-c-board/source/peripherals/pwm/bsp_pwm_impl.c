/**
 * @file    bsp_pwm_impl.c
 * @brief   rm-c-board PWM BSP 实现（STM32F407）
 * @details TIM8 on APB2 168MHz, PC6-9 (AF3), 7-pin PWM header.
 *          固定使用 TIM_OCMODE_PWM1，极性通过 OCPolarity 控制。
 */

#include "bsp_pwm.h"
#include "core/om_def.h"

static const PwmCapability gPwmCap[BSP_PWM_COUNT] = {
    /* pwm1: TIM1, APB2 168MHz, 16-bit advanced */
    [0] = { .numChannels = 4, .minPeriodNs = 1000, .maxPeriodNs = 1000000000,
            .resolutionHz = 168000000,
            .caps = PWM_CAP_POLARITY_NORMAL | PWM_CAP_POLARITY_INVERSED,
            .counterWidth = 16 },
    /* pwm2: TIM8, APB2 168MHz, 16-bit advanced */
    [1] = { .numChannels = 3, .minPeriodNs = 1000, .maxPeriodNs = 1000000000,
            .resolutionHz = 168000000,
            .caps = PWM_CAP_POLARITY_NORMAL | PWM_CAP_POLARITY_INVERSED,
            .counterWidth = 16 },
};

static inline BspPwm *bsp_pwm_get_priv(PwmController *ctrl)
{ return (BspPwm *)ctrl->parent.handle; }

static uint32_t bsp_pwm_get_timer_clock(TIM_TypeDef *instance)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
    uint32_t hclk  = HAL_RCC_GetHCLKFreq();
    uint32_t tim_apb1 = (pclk1 < hclk) ? (pclk1 * 2) : pclk1;
    uint32_t tim_apb2 = (pclk2 < hclk) ? (pclk2 * 2) : pclk2;
    return ((uint32_t)instance >= APB2PERIPH_BASE && (uint32_t)instance < AHB1PERIPH_BASE)
           ? tim_apb2 : tim_apb1;
}

static void bsp_pwm_calc_psc_arr(uint32_t timer_hz, uint32_t period_cycles, uint32_t *psc, uint32_t *arr)
{
    if (period_cycles <= 0xFFFFU) { *psc = 0; *arr = (period_cycles > 0) ? (period_cycles - 1) : 0; return; }
    uint32_t psc_val = 0;
    while (psc_val < 0xFFFFU) {
        uint32_t e = period_cycles / (psc_val + 1);
        if (e <= 0xFFFFU) { *psc = psc_val; *arr = (e > 0) ? (e - 1) : 0; return; }
        psc_val++;
    }
    *psc = 0xFFFFU; *arr = 0xFFFFU;
}

static uint32_t bsp_pwm_to_timer_cycles(uint32_t timer_hz, uint32_t period_cycles, uint32_t cap_hz)
{ return (uint32_t)((uint64_t)period_cycles * timer_hz / cap_hz); }

static uint32_t bsp_pwm_hal_channel(uint8_t channel)
{ return (channel == 0) ? TIM_CHANNEL_1 : (channel == 1) ? TIM_CHANNEL_2
       : (channel == 2) ? TIM_CHANNEL_3 : TIM_CHANNEL_4; }

static OmRet bsp_pwm_channel_config(PwmController *ctrl, uint8_t channel,
                                     uint32_t period_cycles, uint32_t pulse_cycles,
                                     PwmPolarity polarity)
{
    BspPwm *bsp = bsp_pwm_get_priv(ctrl);
    TIM_HandleTypeDef *htim = &bsp->timHandle;
    uint32_t timer_hz = bsp_pwm_get_timer_clock(htim->Instance);
    uint32_t cap_hz   = ctrl->cap->resolutionHz;
    uint32_t tim_period = bsp_pwm_to_timer_cycles(timer_hz, period_cycles, cap_hz);
    uint32_t tim_pulse  = bsp_pwm_to_timer_cycles(timer_hz, pulse_cycles, cap_hz);
    uint32_t psc, arr;
    bsp_pwm_calc_psc_arr(timer_hz, tim_period, &psc, &arr);

    /* 仅更新时基寄存器（不调 HAL_TIM_PWM_Init——它写 TIM_EGR_UG 会复位计数器，
     * 在定时器运行时调用会导致其他通道输出毛刺）。预装载机制保证原子切换。 */
    htim->Instance->PSC = psc;
    htim->Instance->ARR = arr;
    htim->Instance->CR1 |= TIM_CR1_ARPE;  /* 确保预装载启用 */

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = tim_pulse;
    oc.OCPolarity = (polarity == PWM_POLARITY_INVERSED) ? TIM_OCPOLARITY_LOW : TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(htim, &oc, bsp_pwm_hal_channel(channel));
    return OM_OK;
}

static OmRet bsp_pwm_channel_enable(PwmController *ctrl, uint8_t channel)
{
    BspPwm *bsp = bsp_pwm_get_priv(ctrl);
    HAL_TIM_PWM_Start(&bsp->timHandle, bsp_pwm_hal_channel(channel));
    /* TIM1/TIM8 are advanced timers — must enable Main Output (MOE) */
    __HAL_TIM_MOE_ENABLE(&bsp->timHandle);
    return OM_OK;
}

static OmRet bsp_pwm_channel_disable(PwmController *ctrl, uint8_t channel)
{ BspPwm *bsp = bsp_pwm_get_priv(ctrl); HAL_TIM_PWM_Stop(&bsp->timHandle, bsp_pwm_hal_channel(channel)); return OM_OK; }

static OmRet bsp_pwm_channel_set_pulse(PwmController *ctrl, uint8_t channel, uint32_t pulse_cycles)
{
    BspPwm *bsp = bsp_pwm_get_priv(ctrl);
    uint32_t timer_hz = bsp_pwm_get_timer_clock(bsp->timHandle.Instance);
    uint32_t cap_hz   = ctrl->cap->resolutionHz;
    uint32_t tim_pulse = bsp_pwm_to_timer_cycles(timer_hz, pulse_cycles, cap_hz);
    __HAL_TIM_SET_COMPARE(&bsp->timHandle, bsp_pwm_hal_channel(channel), tim_pulse);
    return OM_OK;
}

static const PwmOps gPwmOps = {
    .channelConfig   = bsp_pwm_channel_config,
    .channelEnable   = bsp_pwm_channel_enable,
    .channelDisable  = bsp_pwm_channel_disable,
    .channelSetPulse = bsp_pwm_channel_set_pulse,
};

static BspPwm gBspPwm[BSP_PWM_COUNT] = {
    { .timHandle = { .Instance = TIM1 }, .name = "pwm1" },
    { .timHandle = { .Instance = TIM8 }, .name = "pwm2" },
};

void bsp_pwm_register(void)
{
    bsp_pwm_init_gpio();
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM8_CLK_ENABLE();

    /* 高级定时器（TIM1/TIM8）：一次性配置 BDTR */
    TIM_BreakDeadTimeConfigTypeDef bdtr = {0};
    bdtr.OffStateRunMode  = TIM_OSSR_ENABLE;
    bdtr.OffStateIDLEMode = TIM_OSSI_ENABLE;
    bdtr.AutomaticOutput  = TIM_AUTOMATICOUTPUT_ENABLE;
    HAL_TIMEx_ConfigBreakDeadTime(&gBspPwm[0].timHandle, &bdtr);  /* TIM1 */
    HAL_TIMEx_ConfigBreakDeadTime(&gBspPwm[1].timHandle, &bdtr);  /* TIM8 */
    for (uint8_t i = 0; i < BSP_PWM_COUNT; i++) {
        /* 一次性初始化定时器时基——channelConfig 中不再调 HAL_TIM_PWM_Init */
        gBspPwm[i].timHandle.Init.Prescaler         = 0;
        gBspPwm[i].timHandle.Init.Period            = 0xFFFF;
        gBspPwm[i].timHandle.Init.CounterMode       = TIM_COUNTERMODE_UP;
        gBspPwm[i].timHandle.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
        gBspPwm[i].timHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
        HAL_TIM_PWM_Init(&gBspPwm[i].timHandle);
        pwm_controller_register(&gBspPwm[i].parent, gBspPwm[i].name,
                                 &gPwmCap[i], &gPwmOps, &gBspPwm[i],
                                 gBspPwm[i].chState);
    }
}

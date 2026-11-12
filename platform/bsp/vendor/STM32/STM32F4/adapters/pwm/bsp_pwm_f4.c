/**
 * @file  bsp_pwm_f4.c
 * @brief STM32F4 家族 PWM BSP 共享适配层（板瘦身：板=数据、驱动=通用）
 * @details 由 rm-a（TIM2/8/4/5）/rm-c（TIM1/8）两板 bsp_pwm_impl.c 提炼；板特有数据
 *          （实例/能力/引脚）全部外置到板侧 bsp_pwm_data.c，经 f4.h 契约 extern 引用。
 *          语义对齐决策（两板漂移的收敛，详见 f4.h）：
 *          - MOE 去特判：HAL_TIM_PWM_Start 内部按 IS_TIM_BREAK_INSTANCE 自动使能，
 *            删除两板手写代码（rm-a 的 Instance==TIM8 比较 / rm-c 的无条件使能）；
 *          - BDTR 无条件：对所有实例配置（非高级定时器写入为硬件 no-op）；
 *          - AOE 关：break 事件后不自动恢复输出（Linux 安全对齐）；
 *          - timerHz 预计算：ISR 内 setPulse 直接读取（rm-c 方案，统一 rm-a）；
 *          - counterWidth 诚实声明：能力表一律 16（实现统一走 16-bit PSC/ARR 路径）。
 *          启用外设 = 板 lua selfreg_sources 引用本文件（含 OM_INIT 自注册）。
 */
#include "bsp_pwm.h"
#include "core/om_init.h"
#include "f4_clk.h"

/* 契约守卫：板 shim bsp_pwm.h 必须定义实例数，否则编译期报错（防漏板宏静默漂移） */
#ifndef BSP_PWM_COUNT
#error "bsp_pwm_f4.c requires BSP_PWM_COUNT (define in board bsp_pwm.h shim)"
#endif

/* 分散加载自注册：把 bsp_pwm_register 挂到 .om_init 段（BOARD 级，由 om_do_initcalls 自动调用） */
static OmRet bsp_pwm_self_init(void)
{
    bsp_pwm_register();
    return OM_OK;
}
OM_INIT_BOARD(bsp_pwm_self_init);

/* ===== 内部辅助 ===== */

static inline BspPwm *bsp_pwm_get_priv(PwmController *ctrl)
{
    return (BspPwm *)ctrl->parent.handle;
}

/**
 * @brief 使能 TIM 外设时钟（先查后开）
 * @details 时钟使能宏无法数据化（是宏不是对象），家族 if-else 链收敛
 *          （f4_clk.h 的 GPIO 同款做法；F4 常用 PWM 定时器 TIM1-8，TIM9+ 未启用）。
 */
static void bsp_pwm_enable_tim_clk(TIM_TypeDef *instance)
{
    if (instance == TIM1)
    {
        if (__HAL_RCC_TIM1_IS_CLK_DISABLED()) __HAL_RCC_TIM1_CLK_ENABLE();
    }
    else if (instance == TIM2)
    {
        if (__HAL_RCC_TIM2_IS_CLK_DISABLED()) __HAL_RCC_TIM2_CLK_ENABLE();
    }
    else if (instance == TIM3)
    {
        if (__HAL_RCC_TIM3_IS_CLK_DISABLED()) __HAL_RCC_TIM3_CLK_ENABLE();
    }
    else if (instance == TIM4)
    {
        if (__HAL_RCC_TIM4_IS_CLK_DISABLED()) __HAL_RCC_TIM4_CLK_ENABLE();
    }
    else if (instance == TIM5)
    {
        if (__HAL_RCC_TIM5_IS_CLK_DISABLED()) __HAL_RCC_TIM5_CLK_ENABLE();
    }
    else if (instance == TIM6)
    {
        if (__HAL_RCC_TIM6_IS_CLK_DISABLED()) __HAL_RCC_TIM6_CLK_ENABLE();
    }
    else if (instance == TIM7)
    {
        if (__HAL_RCC_TIM7_IS_CLK_DISABLED()) __HAL_RCC_TIM7_CLK_ENABLE();
    }
    else if (instance == TIM8)
    {
        if (__HAL_RCC_TIM8_IS_CLK_DISABLED()) __HAL_RCC_TIM8_CLK_ENABLE();
    }
}

/**
 * @brief 获取定时器输入时钟（含 APB ×2 规则）
 */
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

/**
 * @brief 计算 PSC + ARR 值
 * @details 找到最小 PSC 使 ARR <= 0xFFFF（16-bit 安全）。
 *          TIM2/TIM5 虽是 32-bit 定时器，但统一走 16-bit 路径保证通用性
 *          （能力表 counterWidth 相应诚实声明 16）。
 */
static void bsp_pwm_calc_psc_arr(uint32_t period_cycles,
                                 uint32_t *psc, uint32_t *arr)
{
    if (period_cycles <= 0xFFFFU)
    {
        *psc = 0;
        *arr = (period_cycles > 0) ? (period_cycles - 1) : 0;
        return;
    }

    uint32_t psc_val = 0;
    while (psc_val < 0xFFFFU)
    {
        uint32_t effective = period_cycles / (psc_val + 1);
        if (effective <= 0xFFFFU)
        {
            *psc = psc_val;
            *arr = (effective > 0) ? (effective - 1) : 0;
            return;
        }
        psc_val++;
    }

    *psc = 0xFFFFU;
    *arr = 0xFFFFU;
}

/**
 * @brief 框架 capability 时钟域 → 实际 TIM 时钟域
 */
static uint32_t bsp_pwm_to_timer_cycles(uint32_t timer_hz, uint32_t period_cycles,
                                        uint32_t cap_hz)
{
    return (uint32_t)((uint64_t)period_cycles * timer_hz / cap_hz);
}

/**
 * @brief channel 号 → HAL TIM_CHANNEL 宏
 */
static uint32_t bsp_pwm_hal_channel(uint8_t channel)
{
    return (channel == 0) ? TIM_CHANNEL_1
         : (channel == 1) ? TIM_CHANNEL_2
         : (channel == 2) ? TIM_CHANNEL_3
         :                   TIM_CHANNEL_4;
}

/* ===== 板级预初始化（数据驱动：TIM 时钟 + GPIO AF 复用，逐实例） ===== */

static void bsp_pwm_pre_init(void)
{
    GPIO_InitTypeDef init = {0};
    init.Mode      = GPIO_MODE_AF_PP;
    init.Pull      = GPIO_NOPULL;
    init.Speed     = GPIO_SPEED_FREQ_HIGH;

    for (uint8_t i = 0; i < BSP_PWM_COUNT; i++)
    {
        bsp_pwm_enable_tim_clk(gBspPwm[i].timHandle.Instance);

        /* 引脚表行 = 能力表 numChannels；不足 BSP_PWM_MAX_CHANNELS 时补 {0} 哨兵（port=NULL） */
        for (uint8_t ch = 0; ch < gBspPwmCap[i].numChannels; ch++)
        {
            const BspPwmPinCfg *pin = &gBspPwmPinTable[i][ch];
            if (pin->port == NULL)
                break;
            bsp_f4_enable_gpio_clk(pin->port);
            init.Pin       = pin->pin;
            init.Alternate = pin->af;
            HAL_GPIO_Init(pin->port, &init);
        }
    }
}

/* ===== PwmOps 实现 ===== */

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
    bsp_pwm_calc_psc_arr(tim_period, &psc, &arr);

    /* 仅更新时基寄存器（不调 HAL_TIM_PWM_Init——它写 TIM_EGR_UG 会复位计数器，
     * 在定时器运行时调用会导致其他通道输出毛刺）。预装载机制保证原子切换。 */
    htim->Instance->PSC = psc;
    htim->Instance->ARR = arr;
    htim->Instance->CR1 |= TIM_CR1_ARPE;  /* 确保预装载启用 */

    /* 固定 TIM_OCMODE_PWM1：PWM1 + OCPolarity_HIGH 等价于 PWM2 + OCPolarity_LOW，
     * 用户经 PwmPolarity 枚举即可覆盖全部需求。 */
    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = tim_pulse;
    oc.OCPolarity = (polarity == PWM_POLARITY_INVERSED)
                        ? TIM_OCPOLARITY_LOW : TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;

    HAL_TIM_PWM_ConfigChannel(htim, &oc, bsp_pwm_hal_channel(channel));
    return OM_OK;
}

static OmRet bsp_pwm_channel_enable(PwmController *ctrl, uint8_t channel)
{
    BspPwm *bsp = bsp_pwm_get_priv(ctrl);
    HAL_TIM_PWM_Start(&bsp->timHandle, bsp_pwm_hal_channel(channel));
    /* 高级定时器（TIM1/TIM8）的 MOE 由 HAL_TIM_PWM_Start 内部按 IS_TIM_BREAK_INSTANCE
     * 自动使能，BSP 无需特判（语义对齐，见 f4.h）。 */
    return OM_OK;
}

static OmRet bsp_pwm_channel_disable(PwmController *ctrl, uint8_t channel)
{
    BspPwm *bsp = bsp_pwm_get_priv(ctrl);
    HAL_TIM_PWM_Stop(&bsp->timHandle, bsp_pwm_hal_channel(channel));
    return OM_OK;
}

static OmRet bsp_pwm_channel_set_pulse(PwmController *ctrl, uint8_t channel,
                                       uint32_t pulse_cycles)
{
    BspPwm *bsp = bsp_pwm_get_priv(ctrl);
    uint32_t cap_hz    = ctrl->cap->resolutionHz;
    uint32_t tim_pulse = bsp_pwm_to_timer_cycles(bsp->timerHz, pulse_cycles, cap_hz);
    __HAL_TIM_SET_COMPARE(&bsp->timHandle, bsp_pwm_hal_channel(channel), tim_pulse);
    return OM_OK;
}

static const PwmOps gPwmOps = {
    .channelConfig   = bsp_pwm_channel_config,
    .channelEnable   = bsp_pwm_channel_enable,
    .channelDisable  = bsp_pwm_channel_disable,
    .channelSetPulse = bsp_pwm_channel_set_pulse,
};

/* ===== 注册入口 ===== */

void bsp_pwm_register(void)
{
    bsp_pwm_pre_init();

    /* BDTR 无条件配置所有实例：非高级定时器无 BDTR 寄存器，写入为硬件 no-op
     * （Linux pwm-stm32 同款哲学，无需"哪些实例需要"判定）。
     * AOE 保持关闭：break 事件后 PWM 不得自动恢复输出（Linux 2019 年移除 AOE
     * 的安全决策，原 rm-a/rm-c 均开 AOE，此处收敛为关）。 */
    TIM_BreakDeadTimeConfigTypeDef bdtr = {0};
    bdtr.OffStateRunMode  = TIM_OSSR_ENABLE;
    bdtr.OffStateIDLEMode = TIM_OSSI_ENABLE;
    bdtr.AutomaticOutput  = TIM_AUTOMATICOUTPUT_DISABLE;

    for (uint8_t i = 0; i < BSP_PWM_COUNT; i++)
    {
        HAL_TIMEx_ConfigBreakDeadTime(&gBspPwm[i].timHandle, &bdtr);

        /* 一次性初始化定时器时基——channelConfig 中不再调 HAL_TIM_PWM_Init */
        gBspPwm[i].timHandle.Init.Prescaler         = 0;
        gBspPwm[i].timHandle.Init.Period            = 0xFFFF;
        gBspPwm[i].timHandle.Init.CounterMode       = TIM_COUNTERMODE_UP;
        gBspPwm[i].timHandle.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
        gBspPwm[i].timHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
        HAL_TIM_PWM_Init(&gBspPwm[i].timHandle);

        /* 预计算定时器时钟：ISR 内 setPulse 直接读取，避免运行期调 RCC 频率查询 */
        gBspPwm[i].timerHz = bsp_pwm_get_timer_clock(gBspPwm[i].timHandle.Instance);

        pwm_controller_register(&gBspPwm[i].parent, gBspPwm[i].name,
                                 &gBspPwmCap[i], &gPwmOps, &gBspPwm[i],
                                 gBspPwm[i].chState);
    }
}

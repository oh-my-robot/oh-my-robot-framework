/**
 * @file    hal_pwm.c
 * @ingroup PWM
 * @brief   PWM 框架层实现
 * @details
 * 通道状态由 PwmChannelState 统一管理，框架是唯一真相源。
 *
 * 并发约定：
 * - chState 写入受 osal_irq_lock 保护，ISR 中的 set_pulse 读到一致状态。
 * - 线程间并发无框架级保护——调用者自行串行化。
 * - 多通道间并行操作安全。
 */

#include "drivers/peripheral/pwm/pal_pwm_dev.h"
#include "osal/osal_core.h"

/* ===== ns -> cycles 转换 ===== */

static OmRet ns_to_cycles(const PwmCapability *cap, uint32_t period_ns,
                          uint32_t *period_cycles)
{
    uint64_t cycles = (uint64_t)period_ns * cap->resolutionHz / 1000000000ULL;
    if (cycles > UINT32_MAX)
        return OM_ERR_OVERFLOW;
    *period_cycles = (uint32_t)cycles;
    return OM_OK;
}

/* ===== Device 接口实现 ===== */

static OmRet pwm_dev_init(Device *dev)
{
    PwmController *ctrl = (PwmController *)dev;
    if (!ctrl->chState)
        return OM_ERR_INVALID_ARG;
    return OM_OK;
}

static OmRet pwm_dev_open(Device *dev, uint32_t oparam)
{
    /* 控制器级打开：声明使用权限。通道启停由 pwm_channel_enable/disable 控制。 */
    (void)dev;
    (void)oparam;
    return OM_OK;
}

static OmRet pwm_dev_close(Device *dev)
{
    /* 控制器级关闭：停止全部正在输出的通道，保护硬件安全 */
    PwmController *ctrl = (PwmController *)dev;
    OsalIrqIsrState key;
    osal_irq_lock(&key);
    for (uint8_t i = 0; i < ctrl->cap->numChannels; i++)
    {
        if (ctrl->chState[i].enabled)
        {
            ctrl->ops->channelDisable(ctrl, i);
            ctrl->chState[i].enabled = false;
        }
    }
    osal_irq_unlock(key);
    return OM_OK;
}

static OmRet pwm_dev_control(Device *dev, size_t cmd, void *arg)
{
    PwmController *ctrl = (PwmController *)dev;

    switch (cmd)
    {
    case PWM_CMD_GET_CAPABILITY:
        if (!arg)
            return OM_ERR_INVALID_ARG;
        *(const PwmCapability **)arg = ctrl->cap;
        return OM_OK;

    case PWM_CMD_SUSPEND:
        for (uint8_t i = 0; i < ctrl->cap->numChannels; i++)
        {
            if (ctrl->chState[i].enabled)
            {
                ctrl->ops->channelDisable(ctrl, i);
                /* enabled 保持 true，标记"待恢复" */
            }
        }
        return OM_OK;

    case PWM_CMD_RESUME:
        for (uint8_t i = 0; i < ctrl->cap->numChannels; i++)
        {
            if (ctrl->chState[i].enabled)
            {
                ctrl->ops->channelEnable(ctrl, i);
            }
        }
        return OM_OK;

    default:
        return OM_ERR_NOT_SUPPORTED;
    }
}

static DevInterface pwm_dev_interface = {
    .init = pwm_dev_init,
    .open = pwm_dev_open,
    .close = pwm_dev_close,
    .read = NULL,  /* PWM 不是数据流外设 */
    .write = NULL, /* 用 pwm_channel_set_pulse 直接 API */
    .control = pwm_dev_control,
};

/* ===== 控制器注册 ===== */

OmRet pwm_controller_register(PwmController *ctrl, const char *name,
                              const PwmCapability *cap,
                              const PwmOps *ops, void *priv,
                              PwmChannelState *chState)
{
    if (!ctrl || !name || !cap || !ops || !chState)
        return OM_ERR_INVALID_ARG;

    if (!ops->channelConfig || !ops->channelEnable || !ops->channelDisable || !ops->channelSetPulse)
        return OM_ERR_INVALID_ARG;

    if (cap->numChannels == 0 || cap->resolutionHz == 0)
        return OM_ERR_INVALID_ARG;

    if (cap->minPeriodNs > cap->maxPeriodNs)
        return OM_ERR_INVALID_ARG;

    ctrl->parent.type = DEVICE_TYPE_PWM;
    ctrl->parent.handle = priv;
    ctrl->parent.interface = &pwm_dev_interface;
    ctrl->ops = ops;
    ctrl->cap = cap;
    ctrl->chState = chState;

    /* 初始化所有通道状态为"未配置" */
    for (uint8_t i = 0; i < cap->numChannels; i++)
    {
        chState[i].periodNs = 0;
        chState[i].periodCycles = 0;
        chState[i].pulseNs = 0;
        chState[i].polarity = PWM_POLARITY_NORMAL;
        chState[i].enabled = false;
    }

    OmRet ret = device_register(&ctrl->parent, (char *)name, 0);
    if (ret != OM_OK)
        return ret;

    return OM_OK;
}

/* ===== 内部辅助 ===== */

/** 校验通道句柄有效性（ctrl 非空 + channel 在范围内） */
static inline OmRet pwm_channel_validate(PwmChannel ch)
{
    if (!ch.ctrl)
        return OM_ERR_INVALID_ARG;
    if (ch.channel >= ch.ctrl->cap->numChannels)
        return OM_ERR_RANGE;
    return OM_OK;
}

/* ===== 通道句柄解析 ===== */

OmRet pwm_channel_get(const PwmChannelSpec *spec, PwmChannel *ch)
{
    if (!spec || !ch)
        return OM_ERR_INVALID_ARG;
    if (!spec->controller)
        return OM_ERR_INVALID_ARG;

    Device *dev = device_find((char *)spec->controller);
    if (!dev)
        return OM_ERR_NOT_FOUND;

    PwmController *ctrl = (PwmController *)dev;
    if (spec->channel >= ctrl->cap->numChannels)
        return OM_ERR_RANGE;

    ch->ctrl = ctrl;
    ch->channel = spec->channel;
    return OM_OK;
}

/* ===== 通道配置 ===== */

OmRet pwm_channel_config(PwmChannel ch, const PwmChannelConfig *cfg)
{
    OmRet v = pwm_channel_validate(ch);
    if (v != OM_OK)
        return v;
    if (!cfg)
        return OM_ERR_INVALID_ARG;

    const PwmCapability *cap = ch.ctrl->cap;

    if (cfg->periodNs < cap->minPeriodNs || cfg->periodNs > cap->maxPeriodNs)
        return OM_ERR_RANGE;
    if (cfg->pulseNs > cfg->periodNs)
        return OM_ERR_RANGE;

    switch (cfg->polarity)
    {
    case PWM_POLARITY_NORMAL:
        if (!(cap->caps & PWM_CAP_POLARITY_NORMAL))
            return OM_ERR_NOT_SUPPORTED;
        break;
    case PWM_POLARITY_INVERSED:
        if (!(cap->caps & PWM_CAP_POLARITY_INVERSED))
            return OM_ERR_NOT_SUPPORTED;
        break;
    default:
        return OM_ERR_INVALID_ARG;
    }

    uint32_t period_cycles, pulse_cycles;
    OmRet ret;
    ret = ns_to_cycles(cap, cfg->periodNs, &period_cycles);
    if (ret != OM_OK)
        return ret;
    ret = ns_to_cycles(cap, cfg->pulseNs, &pulse_cycles);
    if (ret != OM_OK)
        return ret;

    ret = ch.ctrl->ops->channelConfig(ch.ctrl, ch.channel,
                                      period_cycles, pulse_cycles,
                                      cfg->polarity);
    if (ret != OM_OK)
        return ret;

    /* 更新通道状态（osal_irq_lock 保护 ISR 并发读） */
    PwmChannelState *s = &ch.ctrl->chState[ch.channel];
    OsalIrqIsrState key;
    osal_irq_lock(&key);
    s->periodNs = cfg->periodNs;
    s->periodCycles = period_cycles;
    s->pulseNs = cfg->pulseNs;
    s->polarity = cfg->polarity;
    osal_irq_unlock(key);

    return OM_OK;
}

/* ===== 通道启停（框架级幂等） ===== */

OmRet pwm_channel_enable(PwmChannel ch)
{
    OmRet v = pwm_channel_validate(ch);
    if (v != OM_OK)
        return v;

    PwmChannelState *s = &ch.ctrl->chState[ch.channel];

    /* 未 config → 拒绝 */
    if (s->periodNs == 0)
        return OM_ERR_CONFLICT;

    /* 框架级幂等 */
    if (s->enabled)
        return OM_OK;

    OmRet ret = ch.ctrl->ops->channelEnable(ch.ctrl, ch.channel);
    if (ret == OM_OK)
        s->enabled = true;
    return ret;
}

OmRet pwm_channel_disable(PwmChannel ch)
{
    OmRet v = pwm_channel_validate(ch);
    if (v != OM_OK)
        return v;

    PwmChannelState *s = &ch.ctrl->chState[ch.channel];

    /* 框架级幂等 */
    if (!s->enabled)
        return OM_OK;

    OmRet ret = ch.ctrl->ops->channelDisable(ch.ctrl, ch.channel);
    if (ret == OM_OK)
        s->enabled = false;
    return ret;
}

/* ===== 运行时脉宽更新 ===== */

OmRet pwm_channel_set_pulse(PwmChannel ch, uint32_t pulse_ns)
{
    OmRet v = pwm_channel_validate(ch);
    if (v != OM_OK)
        return v;

    PwmChannelState *s = &ch.ctrl->chState[ch.channel];

    /* 未 config → 拒绝 */
    if (s->periodNs == 0)
        return OM_ERR_CONFLICT;

    /* 范围校验——始终可用（框架知道当前 period） */
    if (pulse_ns > s->periodNs)
        return OM_ERR_RANGE;

    /* 使用缓存的 ns->cycles 比例（ISR 快速路径） */
    uint32_t pulse_cycles;
    if (pulse_ns == 0)
    {
        pulse_cycles = 0;
    }
    else if (pulse_ns == s->periodNs)
    {
        pulse_cycles = s->periodCycles;
    }
    else
    {
        pulse_cycles = (uint32_t)((uint64_t)pulse_ns * s->periodCycles / s->periodNs);
    }

    return ch.ctrl->ops->channelSetPulse(ch.ctrl, ch.channel, pulse_cycles);
}

/* ===== 状态查询 ===== */

const PwmChannelState *pwm_channel_get_state(PwmChannel ch)
{
    if (pwm_channel_validate(ch) != OM_OK)
        return NULL;
    return &ch.ctrl->chState[ch.channel];
}

/* ===== 能力查询 ===== */

const PwmCapability *pwm_channel_get_capability(PwmChannel ch)
{
    if (!ch.ctrl)
        return NULL;
    return ch.ctrl->cap;
}

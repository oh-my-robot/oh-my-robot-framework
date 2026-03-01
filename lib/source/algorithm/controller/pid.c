#include "core/algorithm/controller/pid.h"
#include <math.h>
#include <string.h>

/**
 * @brief 初始化PID控制器
 * @param pid PID控制器实例
 * @param mode PID模式(位置式/增量式)
 * @param kp 比例系数
 * @param ki 积分系数
 * @param kd 微分系数
 * @return 初始化结果，true成功，false失败
 */
bool pid_init(PidController_t pid, PidMode_e mode, float kp, float ki, float kd)
{
    // 参数检查
    if (pid == NULL)
    {
        return false;
    }

    // 清零结构体
    memset(pid, 0, sizeof(PidController_s));

    // 设置基本参数
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->mode = mode;

    // 初始化优化功能配置为全0(全部禁用)
    pid->improvementConfig.allSettings = 0;

    // 初始化优化参数为默认值
    pid->improvementParams.outputLimitMax = 0.0f;
    pid->improvementParams.outputLimitMin = 0.0f;

    pid->improvementParams.integralLimit = 0.0f;
    pid->improvementParams.deadBand = 0.0f;
    pid->improvementParams.derivativeFilterCoeff = 0.0f;
    pid->improvementParams.variableIntegralThresholdA = 0.0f;
    pid->improvementParams.variableIntegralThresholdB = 0.0f;

    return true;
}

/**
 * @brief 设置PID基本参数
 * @param pid PID控制器实例
 * @param kp 比例系数
 * @param ki 积分系数
 * @param kd 微分系数
 */
void pid_set_params(PidController_t pid, float kp, float ki, float kd)
{
    // 参数检查
    if (pid == NULL)
    {
        return;
    }

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

/**
 * @brief 设置输出限幅
 * @param pid PID控制器实例
 * @param limit 限幅值
 */
void pid_set_output_limit(PidController_t pid, float min_limit, float max_limit)
{
    // 参数检查
    if (pid == NULL)
    {
        return;
    }
    pid->improvementConfig.settings.outputLimitEnable = 1;
    pid->improvementParams.outputLimitMin = min_limit;
    pid->improvementParams.outputLimitMax = max_limit;
}

/**
 * @brief 设置积分限幅
 * @param pid PID控制器实例
 * @param limit 限幅值
 */
void pid_set_integral_limit(PidController_t pid, float limit)
{
    // 参数检查
    if (pid == NULL)
    {
        return;
    }
    pid->improvementConfig.settings.integralLimitEnable = 1;
    pid->improvementParams.integralLimit = limit;
}

/**
 * @brief 设置死区宽度
 * @param pid PID控制器实例
 * @param deadBand 死区宽度
 */
void pid_set_dead_band(PidController_t pid, float dead_band)
{
    // 参数检查
    if (pid == NULL)
    {
        return;
    }
    pid->improvementConfig.settings.deadBandEnable = 1;
    pid->improvementParams.deadBand = dead_band;
}

/**
 * @brief 设置微分滤波器系数
 * @param pid PID控制器实例
 * @param coeff 滤波系数(0-1)
 */
void pid_set_derivative_filter_coeff(PidController_t pid, float coeff)
{
    // 参数检查
    if (pid == NULL)
    {
        return;
    }

    // 限制系数范围
    if (coeff < 0.0f)
        coeff = 0.0f;
    if (coeff > 1.0f)
        coeff = 1.0f;
    pid->improvementConfig.settings.derivativeFilterEnable = 1;
    pid->improvementParams.derivativeFilterCoeff = coeff;
}

/**
 * @brief 设置变速积分阈值
 * @param pid PID控制器实例
 * @param thresholdA 阈值A
 * @param thresholdB 阈值B
 */
void pid_set_variable_integral_thresholds(PidController_t pid, float threshold_a, float threshold_b)
{
    // 参数检查
    if (pid == NULL)
    {
        return;
    }
    pid->improvementConfig.settings.variableIntegralEnable = 1;
    pid->improvementParams.variableIntegralThresholdA = threshold_a;
    pid->improvementParams.variableIntegralThresholdB = threshold_b;
}

/**
 * @brief 重置PID控制器内部状态
 * @param pid PID控制器实例
 */
void pid_reset(PidController_t pid)
{
    // 参数检查
    if (pid == NULL)
    {
        return;
    }

    // 重置内部状态变量
    pid->err = 0.0f;
    pid->prevErr = 0.0f;
    pid->prevErr2 = 0.0f;
    pid->prevOut = 0.0f;
    pid->iTerm = 0.0f;
    pid->prevMeas = 0.0f;

    // 重置各分量输出
    pid->pOut = 0.0f;
    pid->iOut = 0.0f;
    pid->dOut = 0.0f;
    pid->prevDOut = 0.0f;

    // 重置时间戳
    pid->lastTick = 0.0f;
    pid->dt = 0.0f;
}

/**
 * @brief 计算变速积分系数
 * @param pid PID控制器实例
 * @param error 当前误差
 * @return 变速积分系数
 */
static float pid_calc_variable_integral_factor(PidController_t pid, float error)
{
    float abs_error = fabsf(error);
    float a = pid->improvementParams.variableIntegralThresholdA;
    float b = pid->improvementParams.variableIntegralThresholdB;

    // 当误差大于A+B时，积分比例为0
    if (abs_error > (a + b))
    {
        return 0.0f;
    }
    // 当误差在B和A+B之间，积分比例f(e(t)) = (a + b - |e(t)|) / a
    else if (abs_error > b)
    {
        return (a + b - abs_error) / a;
    }
    // 当误差小于B时，积分比例为1
    else
    {
        return 1.0f;
    }
}

/**
 * @brief 死区控制处理
 * @param pid PID控制器实例
 * @return 是否在死区内
 */
static bool pid_dead_band_process(PidController_t pid)
{
    if (fabsf(pid->err) < pid->improvementParams.deadBand)
    {
        return true; // 在死区内
    }
    return false; // 不在死区内
}

/**
 * @brief 积分限幅处理（位置式PID）
 * @param pid PID控制器实例
 */
static void pid_integral_limit_process_positional(PidController_t pid)
{
    if (pid->iOut > pid->improvementParams.integralLimit)
    {
        pid->iOut = pid->improvementParams.integralLimit;
    }
    else if (pid->iOut < -pid->improvementParams.integralLimit)
    {
        pid->iOut = -pid->improvementParams.integralLimit;
    }
}

/**
 * @brief 积分限幅处理（增量式PID）
 * @param pid PID控制器实例
 */
static void pid_integral_limit_process_incremental(PidController_t pid)
{
    if (pid->iOut > pid->improvementParams.integralLimit)
    {
        pid->iOut = pid->improvementParams.integralLimit;
    }
    else if (pid->iOut < -pid->improvementParams.integralLimit)
    {
        pid->iOut = -pid->improvementParams.integralLimit;
    }
}

/**
 * @brief 微分滤波处理
 * @param pid PID控制器实例
 */
static void pid_derivative_filter_process(PidController_t pid)
{
    float alpha = pid->improvementParams.derivativeFilterCoeff;
    pid->dOut = alpha * pid->dOut + (1.0f - alpha) * pid->prevDOut;
}

/**
 * @brief 设置微分先行使能
 *
 * @param pid PID控制器实例
 * @param enable 是否开启微分先行使能
 */
void pid_set_derivative_first_enable(PidController_t pid, bool enable)
{
    pid->improvementConfig.settings.derivativeFirstEnable = (enable == true);
}

/**
 * @brief 输出限幅处理
 * @param pid PID控制器实例
 * @note 本函数抗饱和积分的方法为clamping，其基本思想是：
 * 当执行器处于饱和、且误差信号与控制信号同方向（同号）时，积分器停止更新（其值保持不变），
 * 除此之外，积分器正常工作。即，在饱和情况下，只进行有助于削弱饱和程度的积分运算。
 * @todo: 也许可以引入反馈抑制抗饱和算法，以实现更智能的抗饱和控制
 */
static void pid_output_limit_process(PidController_t pid)
{
    if (pid->out > pid->improvementParams.outputLimitMax)
    {
        // 输出达到上限
        pid->out = pid->improvementParams.outputLimitMax;

        // 抗积分饱和：只有当误差为正（需要继续增加输出）时，才停止积分累积
        if (pid->err > 0.0f)
        {
            pid->iOut = pid->iOut - pid->iTerm;
            pid->iTerm = 0.0f;
        }
    }
    else if (pid->out < pid->improvementParams.outputLimitMin)
    {
        // 输出达到下限
        pid->out = pid->improvementParams.outputLimitMin;

        // 抗积分饱和：只有当误差为负（需要继续减少输出）时，才停止积分累积
        if (pid->err < 0.0f)
        {
            pid->iOut = pid->iOut - pid->iTerm;
            pid->iTerm = 0.0f;
        }
    }
}

/**
 * @brief 位置式PID计算
 * @param pid PID控制器实例
 * @return PID输出值
 */
static float pid_positional_compute(PidController_t pid)
{
    float error = pid->err;

    // 死区控制
    if (pid->improvementConfig.settings.deadBandEnable)
    {
        if (pid_dead_band_process(pid))
        {
            error = 0.0;
        }
    }

    // 计算比例项
    pid->pOut = pid->kp * error;

    // 计算微分项
    if (pid->improvementConfig.settings.derivativeFirstEnable)
    {
        // 基于测量值的微分，即微分先行
        if (pid->dt > 0)
        {
            pid->dOut = -pid->kd * (pid->measure - pid->prevMeas) / pid->dt;
        }
        else
        {
            pid->dOut = 0.0f;
        }
    }
    else
    {
        // 基于误差的微分
        if (pid->dt > 0)
        {
            pid->dOut = pid->kd * (error - pid->prevErr) / pid->dt;
        }
        else
        {
            pid->dOut = 0.0f;
        }
    }

    // 微分滤波
    if (pid->improvementConfig.settings.derivativeFilterEnable)
    {
        pid_derivative_filter_process(pid);
    }

    // 计算积分项
    // 变速积分
    float integral_factor = 1.0f;
    if (pid->improvementConfig.settings.variableIntegralEnable)
    {
        integral_factor = pid_calc_variable_integral_factor(pid, error);
    }

    // 计算本次运算的待积分值
    pid->iTerm = pid->ki * error * integral_factor * pid->dt;

    // 累积到总积分项
    pid->iOut += pid->iTerm;

    // 积分限幅（对累积的积分值进行限幅）
    if (pid->improvementConfig.settings.integralLimitEnable)
    {
        pid_integral_limit_process_positional(pid);
    }

    // 计算总输出
    pid->out = pid->pOut + pid->iOut + pid->dOut;

    // 输出限幅
    if (pid->improvementConfig.settings.outputLimitEnable)
    {
        pid_output_limit_process(pid);
    }

    return pid->out;
}

/**
 * @brief 增量式PID计算
 * @param pid PID控制器实例
 * @return PID输出值
 */
static float pid_incremental_compute(PidController_t pid)
{
    if (pid->improvementConfig.settings.deadBandEnable && pid_dead_band_process(pid))
    {
        pid->pOut = 0.0f;
        pid->iOut = 0.0f;
        pid->dOut = 0.0f;
        return pid->prevOut;
    }

    float error = pid->err;

    // 计算比例项
    pid->pOut = pid->kp * (error - pid->prevErr);

    // 计算积分项
    pid->iTerm = pid->ki * error * pid->dt;
    pid->iOut = pid->iTerm;
    if (pid->improvementConfig.settings.integralLimitEnable)
    {
        pid_integral_limit_process_incremental(pid);
    }

    // 计算微分项
    if (pid->dt > 0)
    {
        pid->dOut = pid->kd * (error - 2.0f * pid->prevErr + pid->prevErr2) / pid->dt;
    }
    else
    {
        pid->dOut = 0.0f;
    }
    // 微分滤波
    if (pid->improvementConfig.settings.derivativeFilterEnable)
    {
        pid_derivative_filter_process(pid);
    }

    // 计算总输出
    float output = pid->prevOut + pid->pOut + pid->iOut + pid->dOut;

    // 输出限幅
    if (pid->improvementConfig.settings.outputLimitEnable)
    {
        pid->out = output;
        pid_output_limit_process(pid);
        output = pid->out;
    }

    return output;
}

/**
 * @brief PID计算函数
 * @param pid PID控制器实例
 * @param setpoint 设定值
 * @param measurement 测量值
 * @return PID输出值
 */
float pid_compute(PidController_t pid, float setpoint, float measurement, pidtime_s current_tick)
{
    // 参数检查
    if (pid == NULL)
    {
        while (1)
            ; // TODO: assert
    }

    // 计算时间差
    if (pid->lastTick != 0)
    {
        pid->dt = current_tick - pid->lastTick;
        pid->lastTick = current_tick;
    }
    else
    {
        // 第一次调用，初始化lastTick为当前时间戳
        pid->lastTick = current_tick;
        pid->dt = 0.0f; // 第一次调用，时间差为0
    }

    // 更新状态
    pid->err = setpoint - measurement;
    pid->set = setpoint;
    pid->measure = measurement;

    // 根据PID模式调用相应的计算函数
    float output;
    if (pid->mode == PID_POSITIONAL_MODE)
    {
        output = pid_positional_compute(pid);
    }
    else
    { // 增量式PID
        output = pid_incremental_compute(pid);
    }

    // 更新内部状态
    pid->prevErr2 = pid->prevErr;
    pid->prevErr = pid->err;
    pid->prevOut = output;
    pid->prevMeas = measurement;
    pid->prevDOut = pid->dOut;

    return output;
}

/**
 * @brief 获取PID各分量输出值
 * @param pid PID控制器实例
 * @param pOut 比例项输出指针
 * @param iOut 积分项输出指针
 * @param dOut 微分项输出指针
 */
void pid_get_component_outputs(PidController_t pid, float *p_out, float *i_out, float *d_out)
{
    // 参数检查
    if (pid == NULL)
    {
        return;
    }

    if (p_out != NULL)
    {
        *p_out = pid->pOut;
    }

    if (i_out != NULL)
    {
        *i_out = pid->iOut;
    }

    if (d_out != NULL)
    {
        *d_out = pid->dOut;
    }
}

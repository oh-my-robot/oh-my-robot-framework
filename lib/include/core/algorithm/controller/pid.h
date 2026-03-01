/**
 * @file pid.h
 * @author Yuhao
 * @brief PID控制器头文件
 * @date 2025-12-04
 *
 */

#ifndef __OM_PID_H__
#define OM_PID_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef uint32_t pidtime_cnt; // PID控制器时间计数器类型，由此计数类型，通过换算关系计算得到以下时间类型
    typedef float pidtime_s;      // PID控制器时间类型，单位：s

    // PID模式定义
    typedef enum
    {
        PID_POSITIONAL_MODE = 0, // 位置式PID
        PID_INCREMENTAL_MODE = 1 // 增量式PID
    } PidMode_e;

    // PID优化功能位域定义
    typedef union
    {
        struct
        {
            uint16_t outputLimitEnable : 1;      // 输出限幅使能
            uint16_t integralLimitEnable : 1;    // 积分限幅使能
            uint16_t deadBandEnable : 1;         // 死区控制使能
            uint16_t derivativeFirstEnable : 1;  // 微分先行使能
            uint16_t derivativeFilterEnable : 1; // 微分滤波器使能
            uint16_t variableIntegralEnable : 1; // 变速积分使能
            uint16_t reserved : 10;              // 保留位
        } settings;
        uint16_t allSettings; // 整体访问
    } PidImprovementConfig_u;

    // PID控制器结构体
    typedef struct PidController* PidController_t;
    typedef struct PidController
    {
        // 基本PID参数
        float kp; // 比例系数
        float ki; // 积分系数
        float kd; // 微分系数

        // PID模式
        PidMode_e mode; // PID模式选择

        // 优化功能配置
        PidImprovementConfig_u improvementConfig;

        // 优化参数
        struct
        {
            float outputLimitMin;             // 输出限幅值下限
            float outputLimitMax;             // 输出限幅值上限
            float integralLimit;              // 积分限幅值
            float deadBand;                   // 死区宽度
            float derivativeFilterCoeff;      // 微分滤波器系数(0-1)
            float variableIntegralThresholdA; // 变速积分阈值A
            float variableIntegralThresholdB; // 变速积分阈值B
        } improvementParams;

        // 以下参数主要用于调试和监控

        // 内部状态变量
        float set;      // 设置值
        float out;      // 输出值
        float measure;  // 观测值
        float prevMeas; // 上一次测量值

        float err;      // 当前误差
        float prevErr;  // 上一次误差
        float prevErr2; // 上上次误差(用于增量式PID)

        float iTerm; // 本次计算的积分值

        // 各分量输出
        float pOut;     // 比例项输出
        float iOut;     // 积分项输出
        float dOut;     // 微分项输出
        float prevDOut; // 上一次微分项输出(用于微分滤波)
        float prevOut;  // 上一次输出

        // 时间相关
        pidtime_s lastTick; // 上一次计算的时间戳，单位s
        pidtime_s dt;       // 时间间隔，单位s
    } PidController_s;

    // 函数声明
    /**
     * @brief 初始化PID控制器
     * @param pid PID控制器实例
     * @param mode PID模式(位置式/增量式)
     * @param kp 比例系数
     * @param ki 积分系数
     * @param kd 微分系数
     * @return 初始化结果，true成功，false失败
     */
    bool pid_init(PidController_t pid, PidMode_e mode, float kp, float ki, float kd);

    /**
     * @brief 设置PID基本参数
     * @param pid PID控制器实例
     * @param kp 比例系数
     * @param ki 积分系数
     * @param kd 微分系数
     */
    void pid_set_params(PidController_t pid, float kp, float ki, float kd);

    /**
     * @brief 设置输出限幅
     * @param pid PID控制器实例
     * @param limit 限幅值
     */
    void pid_set_output_limit(PidController_t pid, float min_limit, float max_limit);

    /**
     * @brief 设置积分限幅
     * @param pid PID控制器实例
     * @param limit 限幅值
     */
    void pid_set_integral_limit(PidController_t pid, float limit);

    /**
     * @brief 设置死区宽度
     * @param pid PID控制器实例
     * @param deadBand 死区宽度
     */
    void pid_set_dead_band(PidController_t pid, float dead_band);

    /**
     * @brief 设置微分滤波器系数
     * @param pid PID控制器实例
     * @param coeff 滤波系数(0-1)
     */
    void pid_set_derivative_filter_coeff(PidController_t pid, float coeff);

    /**
     * @brief 设置微分先行使能
     *
     * @param pid
     * @param enable
     */
    void pid_set_derivative_first_enable(PidController_t pid, bool enable);

    /**
     * @brief 设置变速积分阈值
     * @param pid PID控制器实例
     * @param thresholdA 阈值A
     * @param thresholdB 阈值B
     */
    void pid_set_variable_integral_thresholds(PidController_t pid, float threshold_a, float threshold_b);

    /**
     * @brief 重置PID控制器内部状态
     * @param pid PID控制器实例
     */
    void pid_reset(PidController_t pid);

    /**
     * @brief PID计算函数
     * @param pid PID控制器实例
     * @param setpoint 设定值
     * @param measurement 测量值
     * @param currentTick 当前时间戳
     * @return PID输出值
     */
    float pid_compute(PidController_t pid, float setpoint, float measurement, pidtime_s current_tick);

    /**
     * @brief 获取PID各分量输出值
     * @param pid PID控制器实例
     * @param pOut 比例项输出指针
     * @param iOut 积分项输出指针
     * @param dOut 微分项输出指针
     */
    void pid_get_component_outputs(PidController_t pid, float* p_out, float* i_out, float* d_out);

#ifdef __cplusplus
}
#endif

#endif

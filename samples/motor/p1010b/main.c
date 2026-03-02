/**
 * @file main.c
 * @brief P1010B 同步路径速度环测试样例
 * @details
 * 关键流程：
 * 1) 初始化 CAN 设备、P1010B 总线与电机驱动；
 * 2) 通过宏选择电流模式或电压模式；
 * 3) 使用速度反馈做单环 PID，PID 输出作为电机目标值下发；
 * 4) 电机 ID、目标转速、PID 参数、控制模式均支持宏配置。
 */

#include "drivers/motor/vendors/direct_drive/P1010B.h"
#include "drivers/peripheral/can/pal_can_dev.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h"
#include "core/aw_cpu.h"
#include "core/algorithm/controller/pid.h"
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* 用户宏配置                                                                 */
/* -------------------------------------------------------------------------- */

#define P1010B_TEST_CAN_NAME "can1"
#define P1010B_TEST_MOTOR_ID (1U)

/* 对外暴露“电流/电压”两种可选模式，内部映射到驱动枚举。 */
#define P1010B_TEST_MODE_CURRENT (P1010B_MODE_CURRENT)
#define P1010B_TEST_MODE_VOLTAGE (P1010B_MODE_OPEN_LOOP)
#define P1010B_TEST_CONTROL_MODE (P1010B_TEST_MODE_CURRENT)

#define P1010B_TEST_TARGET_SPEED_RPM (65.0f)

#define P1010B_TEST_SPEED_PID_KP (0.45f)
#define P1010B_TEST_SPEED_PID_KI (0.06f)
#define P1010B_TEST_SPEED_PID_KD (0.0f)
#define P1010B_TEST_PID_OUT_MIN  (-75.0f)
#define P1010B_TEST_PID_OUT_MAX  (75.0f)

#define P1010B_TEST_LOOP_PERIOD_MS   (2U)
#define P1010B_TEST_RETRY_DELAY_MS   (50U)
#define P1010B_TEST_THREAD_PRIORITY  (6U)
#define P1010B_TEST_THREAD_STACK     (1024U)

#if (P1010B_TEST_CONTROL_MODE != P1010B_TEST_MODE_CURRENT) && (P1010B_TEST_CONTROL_MODE != P1010B_TEST_MODE_VOLTAGE)
#error "P1010B_TEST_CONTROL_MODE 仅支持 P1010B_TEST_MODE_CURRENT 或 P1010B_TEST_MODE_VOLTAGE"
#endif

/* PID 数组索引：便于调试器按连续内存块观察与在线修改。 */
#define P1010B_PID_GAIN_KP_INDEX            (0U)
#define P1010B_PID_GAIN_KI_INDEX            (1U)
#define P1010B_PID_GAIN_KD_INDEX            (2U)
#define P1010B_PID_GAIN_COUNT               (3U)
#define P1010B_PID_OUTPUT_LIMIT_MIN_INDEX   (0U)
#define P1010B_PID_OUTPUT_LIMIT_MAX_INDEX   (1U)
#define P1010B_PID_OUTPUT_LIMIT_COUNT       (2U)
#define P1010B_FLOAT_COMPARE_EPSILON        (1e-6f)
#define P1010B_TEST_SOFTSTART_STEP_RPM      (1.0f)

/* -------------------------------------------------------------------------- */
/* 测试节点定义                              */
/* -------------------------------------------------------------------------- */

typedef struct
{
    float kp;
    float ki;
    float kd;
    float outputMin;
    float outputMax;
} P1010BSpeedPidConfig_s;

typedef struct
{
    uint8_t motorId;
    P1010BMode_e mode;
    float speedTargetRpm;
    P1010BSpeedPidConfig_s speedPidConfig;

    /* 运行态对象 */
    P1010BDriver_s handle;
    PidController_s speedPid;
} P1010BSpeedLoopNode_s;

/**
 * @brief 在线调参块（可直接在调试器里改写）
 * @details
 * - `controlMode` 使用驱动枚举值：`P1010B_MODE_CURRENT` 或 `P1010B_MODE_OPEN_LOOP`；
 * - `speedPidGain[]` 按 `Kp/Ki/Kd` 顺序存放；
 * - `speedPidOutputLimit[]` 按 `Min/Max` 顺序存放。
 */
typedef struct
{
    volatile uint8_t controlMode;
    volatile uint8_t reserved[3];
    volatile float speedTargetRpm;
    volatile float speedPidGain[P1010B_PID_GAIN_COUNT];
    volatile float speedPidOutputLimit[P1010B_PID_OUTPUT_LIMIT_COUNT];
} P1010BDebugTuneBlock_s;

/**
 * @brief 运行观测块（只读观测）
 * @details 用于可视化查看当前控制环状态，定位“参数变更是否生效”。
 */
typedef struct
{
    volatile float speedTargetRpm;
    volatile float speedCommandRpm;
    volatile float speedFeedbackRpm;
    volatile float pidOutput;
    volatile uint8_t activeMode;
    volatile uint8_t online;
    volatile AwlfRet_e lastLoopRet;
    volatile P1010BRejectReason_e lastRejectReason;
} P1010BDebugRuntimeBlock_s;

#define P1010B_SPEED_LOOP_NODE_INIT(_motorId, _mode, _targetRpm, _kp, _ki, _kd, _outMin, _outMax) \
    {                                                                                                 \
        .motorId = (_motorId),                                                                        \
        .mode = (_mode),                                                                              \
        .speedTargetRpm = (_targetRpm),                                                               \
        .speedPidConfig = {(_kp), (_ki), (_kd), (_outMin), (_outMax)},                               \
    }

/* -------------------------------------------------------------------------- */
/* 全局对象                                                                   */
/* -------------------------------------------------------------------------- */

static Device_t g_canDevice = NULL;
static P1010BBus_s g_p1010bBus;
static osal_thread_t g_speedLoopThread = NULL;

static P1010BSpeedLoopNode_s g_motorNode = P1010B_SPEED_LOOP_NODE_INIT(
    P1010B_TEST_MOTOR_ID,
    P1010B_TEST_CONTROL_MODE,
    P1010B_TEST_TARGET_SPEED_RPM,
    P1010B_TEST_SPEED_PID_KP,
    P1010B_TEST_SPEED_PID_KI,
    P1010B_TEST_SPEED_PID_KD,
    P1010B_TEST_PID_OUT_MIN,
    P1010B_TEST_PID_OUT_MAX);

/* 在线调参入口：默认值与宏配置一致。 */
volatile P1010BDebugTuneBlock_s g_p1010b_debug_tune_block = {
    .controlMode = (uint8_t)P1010B_TEST_CONTROL_MODE,
    .reserved = {0U, 0U, 0U},
    .speedTargetRpm = P1010B_TEST_TARGET_SPEED_RPM,
    .speedPidGain = {
        [P1010B_PID_GAIN_KP_INDEX] = P1010B_TEST_SPEED_PID_KP,
        [P1010B_PID_GAIN_KI_INDEX] = P1010B_TEST_SPEED_PID_KI,
        [P1010B_PID_GAIN_KD_INDEX] = P1010B_TEST_SPEED_PID_KD,
    },
    .speedPidOutputLimit = {
        [P1010B_PID_OUTPUT_LIMIT_MIN_INDEX] = P1010B_TEST_PID_OUT_MIN,
        [P1010B_PID_OUTPUT_LIMIT_MAX_INDEX] = P1010B_TEST_PID_OUT_MAX,
    },
};

/* 运行态观测出口。 */
volatile P1010BDebugRuntimeBlock_s g_p1010b_debug_runtime_block = {0};

/* -------------------------------------------------------------------------- */
/* 辅助函数                                                                   */
/* -------------------------------------------------------------------------- */

static float p1010b_sample_time_s(void)
{
    return (float)osal_time_now_monotonic() / 1000.0f;
}

static float p1010b_sample_absf(float value)
{
    if (value < 0.0f)
    {
        return -value;
    }
    return value;
}

static float p1010b_sample_ramp_to_target(float currentValue, float targetValue, float maxStep)
{
    if (maxStep <= 0.0f)
    {
        return targetValue;
    }
    if (currentValue < (targetValue - maxStep))
    {
        return currentValue + maxStep;
    }
    if (currentValue > (targetValue + maxStep))
    {
        return currentValue - maxStep;
    }
    return targetValue;
}

static uint8_t p1010b_sample_is_float_changed(float lhs, float rhs)
{
    return (p1010b_sample_absf(lhs - rhs) > P1010B_FLOAT_COMPARE_EPSILON) ? 1U : 0U;
}

static uint8_t p1010b_sample_is_supported_control_mode(uint8_t controlMode)
{
    if (controlMode == (uint8_t)P1010B_MODE_CURRENT)
    {
        return 1U;
    }
    if (controlMode == (uint8_t)P1010B_MODE_OPEN_LOOP)
    {
        return 1U;
    }
    return 0U;
}

static void p1010b_sample_release_resources(void)
{
    if (g_p1010bBus.filterAllocated)
    {
        (void)p1010b_bus_deinit(&g_p1010bBus);
    }

    if (g_canDevice != NULL)
    {
        (void)device_close(g_canDevice);
        g_canDevice = NULL;
    }
}

static AwlfRet_e p1010b_sample_init_bus_and_motor(void)
{
    AwlfRet_e awlfRet;
    P1010BConfig_s driverConfig = P1010B_DEFAULT_CONFIG(P1010B_TEST_MOTOR_ID);

    driverConfig.defaultMode = P1010B_TEST_CONTROL_MODE;
    driverConfig.activeReport.enable = false;

    g_canDevice = device_find((char *)P1010B_TEST_CAN_NAME);
    if (g_canDevice == NULL)
    {
        return AWLF_ERROR_NULL;
    }

    awlfRet = device_open(g_canDevice, CAN_O_INT_RX | CAN_O_INT_TX);
    if (awlfRet != AWLF_OK)
    {
        return awlfRet;
    }

    awlfRet = p1010b_bus_init(&g_p1010bBus, g_canDevice);
    if (awlfRet != AWLF_OK)
    {
        return awlfRet;
    }

    /*
     * 当前驱动不会在 register 自动下发主动上报配置。
     * 仍建议先启动 CAN，再执行后续控制序列，避免首批同步事务丢帧。
     */
    awlfRet = device_ctrl(g_canDevice, CAN_CMD_START, NULL);
    if (awlfRet != AWLF_OK)
    {
        return awlfRet;
    }
    
    awlfRet = p1010b_register(&g_p1010bBus, &g_motorNode.handle, &driverConfig);
    if (awlfRet != AWLF_OK)
    {
        return awlfRet;
    }

    return AWLF_OK;
}

static AwlfRet_e p1010b_sample_init_speed_pid(P1010BSpeedLoopNode_s *node)
{
    if (node == NULL)
    {
        return AWLF_ERROR_PARAM;
    }

    if (!pid_init(&node->speedPid,
                  PID_POSITIONAL_MODE,
                  node->speedPidConfig.kp,
                  node->speedPidConfig.ki,
                  node->speedPidConfig.kd))
    {
        return AWLF_ERROR;
    }

    pid_set_output_limit(&node->speedPid, node->speedPidConfig.outputMin, node->speedPidConfig.outputMax);
    return AWLF_OK;
}

static AwlfRet_e p1010b_sample_prepare_motor(P1010BSpeedLoopNode_s *node)
{
    AwlfRet_e awlfRet;
    P1010BResponse_s response = {0};

    if (node == NULL)
    {
        return AWLF_ERROR_PARAM;
    }

    awlfRet = p1010b_disable(&node->handle, 0U, &response);
    if (awlfRet != AWLF_OK)
    {
        return awlfRet;
    }

    awlfRet = p1010b_set_mode(&node->handle, node->mode, 0U, &response);
    if (awlfRet != AWLF_OK)
    {
        return awlfRet;
    }

    awlfRet = p1010b_set_active_report(&node->handle, &node->handle.runtime.activeReport, 0U, &response);
    if (awlfRet != AWLF_OK)
    {
        return awlfRet;
    }

    awlfRet = p1010b_enable(&node->handle, 0U, &response);
    if (awlfRet != AWLF_OK)
    {
        return awlfRet;
    }

    return AWLF_OK;
}

/**
 * @brief 将调参块同步到电机节点
 * @details
 * - 目标速度每周期直接同步；
 * - PID 参数或输出限幅变化后重建 PID；
 * - 模式变化后执行一次 disable->set_mode->enable。
 */
static AwlfRet_e p1010b_sample_sync_debug_tuning(P1010BSpeedLoopNode_s *node)
{
    uint8_t requestedMode;
    float requestedKp;
    float requestedKi;
    float requestedKd;
    float requestedOutMin;
    float requestedOutMax;
    uint8_t needReloadPid = 0U;
    uint8_t needSwitchMode = 0U;

    if (node == NULL)
    {
        return AWLF_ERROR_PARAM;
    }

    node->speedTargetRpm = g_p1010b_debug_tune_block.speedTargetRpm;

    requestedMode = g_p1010b_debug_tune_block.controlMode;
    if (p1010b_sample_is_supported_control_mode(requestedMode) &&
        (requestedMode != (uint8_t)node->mode))
    {
        node->mode = (P1010BMode_e)requestedMode;
        needSwitchMode = 1U;
    }

    requestedKp = g_p1010b_debug_tune_block.speedPidGain[P1010B_PID_GAIN_KP_INDEX];
    requestedKi = g_p1010b_debug_tune_block.speedPidGain[P1010B_PID_GAIN_KI_INDEX];
    requestedKd = g_p1010b_debug_tune_block.speedPidGain[P1010B_PID_GAIN_KD_INDEX];
    requestedOutMin = g_p1010b_debug_tune_block.speedPidOutputLimit[P1010B_PID_OUTPUT_LIMIT_MIN_INDEX];
    requestedOutMax = g_p1010b_debug_tune_block.speedPidOutputLimit[P1010B_PID_OUTPUT_LIMIT_MAX_INDEX];

    if (requestedOutMin < requestedOutMax)
    {
        if (p1010b_sample_is_float_changed(node->speedPidConfig.kp, requestedKp))
        {
            node->speedPidConfig.kp = requestedKp;
            needReloadPid = 1U;
        }
        if (p1010b_sample_is_float_changed(node->speedPidConfig.ki, requestedKi))
        {
            node->speedPidConfig.ki = requestedKi;
            needReloadPid = 1U;
        }
        if (p1010b_sample_is_float_changed(node->speedPidConfig.kd, requestedKd))
        {
            node->speedPidConfig.kd = requestedKd;
            needReloadPid = 1U;
        }
        if (p1010b_sample_is_float_changed(node->speedPidConfig.outputMin, requestedOutMin))
        {
            node->speedPidConfig.outputMin = requestedOutMin;
            needReloadPid = 1U;
        }
        if (p1010b_sample_is_float_changed(node->speedPidConfig.outputMax, requestedOutMax))
        {
            node->speedPidConfig.outputMax = requestedOutMax;
            needReloadPid = 1U;
        }
    }

    if (needReloadPid != 0U)
    {
        AwlfRet_e awlfRet = p1010b_sample_init_speed_pid(node);
        if (awlfRet != AWLF_OK)
        {
            return awlfRet;
        }
    }

    if (needSwitchMode != 0U)
    {
        AwlfRet_e awlfRet = p1010b_sample_prepare_motor(node);
        if (awlfRet != AWLF_OK)
        {
            return awlfRet;
        }
    }

    return AWLF_OK;
}

static void p1010b_sample_update_debug_observer(P1010BSpeedLoopNode_s *node, float speedCommandRpm, float speedFeedbackRpm,
                                                 float pidOutput, AwlfRet_e loopRet)
{
    if (node == NULL)
    {
        return;
    }

    g_p1010b_debug_runtime_block.speedTargetRpm = node->speedTargetRpm;
    g_p1010b_debug_runtime_block.speedCommandRpm = speedCommandRpm;
    g_p1010b_debug_runtime_block.speedFeedbackRpm = speedFeedbackRpm;
    g_p1010b_debug_runtime_block.pidOutput = pidOutput;
    g_p1010b_debug_runtime_block.activeMode = (uint8_t)node->mode;
    g_p1010b_debug_runtime_block.online = p1010b_is_online(&node->handle) ? 1U : 0U;
    g_p1010b_debug_runtime_block.lastLoopRet = loopRet;
    g_p1010b_debug_runtime_block.lastRejectReason = p1010b_get_last_reject_reason(&node->handle);
}

/* -------------------------------------------------------------------------- */
/* 速度环线程                                                                 */
/* -------------------------------------------------------------------------- */

static void p1010b_speed_loop_thread_entry(void *argument)
{
    osal_time_ms_t deadlineMs = 0U;
    float speedCommandRpm = 0.0f;
    uint8_t speedCommandInitialized = 0U;

    (void)argument;

    /* 上电后先完成 disable -> set_mode -> set_active_report -> enable 同步序列。 */
    for (;;)
    {
        if (p1010b_sample_prepare_motor(&g_motorNode) == AWLF_OK)
        {
            break;
        }
        (void)osal_sleep_ms(P1010B_TEST_RETRY_DELAY_MS);
    }

    for (;;)
    {
        const P1010BFeedback_s *feedback;
        float speedFeedbackRpm = 0.0f;
        float pidOutput;
        AwlfRet_e awlfRet;
        AwlfRet_e tuneRet;

        tuneRet = p1010b_sample_sync_debug_tuning(&g_motorNode);
        if (tuneRet != AWLF_OK)
        {
            (void)osal_sleep_ms(P1010B_TEST_RETRY_DELAY_MS);
            continue;
        }

        feedback = p1010b_get_feedback(&g_motorNode.handle);
        if (feedback != NULL)
        {
            speedFeedbackRpm = feedback->speedRpm;
        }

        if (speedCommandInitialized == 0U)
        {
            speedCommandRpm = speedFeedbackRpm;
            speedCommandInitialized = 1U;
        }

        speedCommandRpm = p1010b_sample_ramp_to_target(speedCommandRpm, g_motorNode.speedTargetRpm, P1010B_TEST_SOFTSTART_STEP_RPM);

        /* PID 输出单位由控制模式决定：电流模式=安培，电压模式=伏特。 */
        pidOutput = pid_compute(
            &g_motorNode.speedPid,
            speedCommandRpm,
            speedFeedbackRpm,
            p1010b_sample_time_s());

        awlfRet = p1010b_set_target(&g_motorNode.handle, pidOutput);
        p1010b_sample_update_debug_observer(
            &g_motorNode,
            speedCommandRpm,
            speedFeedbackRpm,
            pidOutput,
            awlfRet);
        if (awlfRet != AWLF_OK)
        {
            (void)osal_sleep_ms(P1010B_TEST_RETRY_DELAY_MS);
            continue;
        }

        if (osal_delay_until(&deadlineMs, P1010B_TEST_LOOP_PERIOD_MS, NULL) != OSAL_OK)
        {
            (void)osal_sleep_ms(P1010B_TEST_LOOP_PERIOD_MS);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 主函数                                                                     */
/* -------------------------------------------------------------------------- */

int main(void)
{
    AwlfRet_e awlfRet;
    osal_status_t osalRet;
    osal_thread_attr_t speedLoopThreadAttr = {0};

    aw_board_init();
    aw_core_init();

    awlfRet = p1010b_sample_init_bus_and_motor();
    if (awlfRet != AWLF_OK)
    {
        p1010b_sample_release_resources();
        return -1;
    }

    awlfRet = p1010b_sample_init_speed_pid(&g_motorNode);
    if (awlfRet != AWLF_OK)
    {
        p1010b_sample_release_resources();
        return -1;
    }

    speedLoopThreadAttr.name = "p1010b_speed";
    speedLoopThreadAttr.priority = P1010B_TEST_THREAD_PRIORITY;
    speedLoopThreadAttr.stack_size = P1010B_TEST_THREAD_STACK;

    osalRet = osal_thread_create(&g_speedLoopThread, &speedLoopThreadAttr, p1010b_speed_loop_thread_entry, NULL);
    if (osalRet != OSAL_OK)
    {
        p1010b_sample_release_resources();
        return -1;
    }

    return osal_kernel_start();
}

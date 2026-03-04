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

#include "core/algorithm/controller/pid.h"
#include "core/om_cpu.h"
#include "core/om_def.h"
#include "drivers/motor/vendors/direct_drive/P1010B.h"
#include "drivers/peripheral/can/pal_can_dev.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h"
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
#define P1010B_TEST_PID_OUT_MIN (-75.0f)
#define P1010B_TEST_PID_OUT_MAX (75.0f)

#define P1010B_TEST_LOOP_PERIOD_MS (2U)
#define P1010B_TEST_RETRY_DELAY_MS (50U)
#define P1010B_TEST_THREAD_PRIORITY (6U)
#define P1010B_TEST_THREAD_STACK (1024U)

#if (P1010B_TEST_CONTROL_MODE != P1010B_TEST_MODE_CURRENT) && (P1010B_TEST_CONTROL_MODE != P1010B_TEST_MODE_VOLTAGE)
#error "P1010B_TEST_CONTROL_MODE 仅支持 P1010B_TEST_MODE_CURRENT 或 P1010B_TEST_MODE_VOLTAGE"
#endif

/* PID 数组索引：便于调试器按连续内存块观察与在线修改。 */
#define P1010B_PID_GAIN_KP_INDEX (0U)
#define P1010B_PID_GAIN_KI_INDEX (1U)
#define P1010B_PID_GAIN_KD_INDEX (2U)
#define P1010B_PID_GAIN_COUNT (3U)
#define P1010B_PID_OUTPUT_LIMIT_MIN_INDEX (0U)
#define P1010B_PID_OUTPUT_LIMIT_MAX_INDEX (1U)
#define P1010B_PID_OUTPUT_LIMIT_COUNT (2U)
#define P1010B_FLOAT_COMPARE_EPSILON (1e-6f)
#define P1010B_TEST_SOFTSTART_STEP_RPM (1.0f)

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
} P1010BSpeedPidConfig;

typedef struct
{
    uint8_t motorId;
    P1010BMode mode;
    float speedTargetRpm;
    P1010BSpeedPidConfig speedPidConfig;

    /* 运行态对象 */
    P1010BDriver handle;
    PidController speedPid;
} P1010BSpeedLoopNode;

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
} P1010BDebugTuneBlock;

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
    volatile OmRet lastLoopRet;
    volatile P1010BRejectReason lastRejectReason;
} P1010BDebugRuntimeBlock;

#define P1010B_SPEED_LOOP_NODE_INIT(_motorId, _mode, _targetRpm, _kp, _ki, _kd, _outMin, _outMax) \
    {                                                                                             \
        .motorId = (_motorId),                                                                    \
        .mode = (_mode),                                                                          \
        .speedTargetRpm = (_targetRpm),                                                           \
        .speedPidConfig = {(_kp), (_ki), (_kd), (_outMin), (_outMax)},                            \
    }

/* -------------------------------------------------------------------------- */
/* 全局对象                                                                   */
/* -------------------------------------------------------------------------- */

static Device *g_can_device = NULL;
static P1010BBus g_p1010b_bus;
static OsalThread *g_speed_loop_thread = NULL;

static P1010BSpeedLoopNode g_motor_node = P1010B_SPEED_LOOP_NODE_INIT(
    P1010B_TEST_MOTOR_ID,
    P1010B_TEST_CONTROL_MODE,
    P1010B_TEST_TARGET_SPEED_RPM,
    P1010B_TEST_SPEED_PID_KP,
    P1010B_TEST_SPEED_PID_KI,
    P1010B_TEST_SPEED_PID_KD,
    P1010B_TEST_PID_OUT_MIN,
    P1010B_TEST_PID_OUT_MAX);

/* 在线调参入口：默认值与宏配置一致。 */
volatile P1010BDebugTuneBlock g_p1010b_debug_tune_block = {
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
volatile P1010BDebugRuntimeBlock g_p1010b_debug_runtime_block = {0};

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

static float p1010b_sample_ramp_to_target(float current_value, float target_value, float max_step)
{
    if (max_step <= 0.0f)
    {
        return target_value;
    }
    if (current_value < (target_value - max_step))
    {
        return current_value + max_step;
    }
    if (current_value > (target_value + max_step))
    {
        return current_value - max_step;
    }
    return target_value;
}

static uint8_t p1010b_sample_is_float_changed(float lhs, float rhs)
{
    return (p1010b_sample_absf(lhs - rhs) > P1010B_FLOAT_COMPARE_EPSILON) ? 1U : 0U;
}

static uint8_t p1010b_sample_is_supported_control_mode(uint8_t control_mode)
{
    if (control_mode == (uint8_t)P1010B_MODE_CURRENT)
    {
        return 1U;
    }
    if (control_mode == (uint8_t)P1010B_MODE_OPEN_LOOP)
    {
        return 1U;
    }
    return 0U;
}

static void p1010b_sample_release_resources(void)
{
    if (g_p1010b_bus.filterAllocated)
    {
        (void)p1010b_bus_deinit(&g_p1010b_bus);
    }

    if (g_can_device != NULL)
    {
        (void)device_close(g_can_device);
        g_can_device = NULL;
    }
}

static OmRet p1010b_sample_init_bus_and_motor(void)
{
    OmRet awlf_ret;
    P1010BConfig driver_config = P1010B_DEFAULT_CONFIG(P1010B_TEST_MOTOR_ID);

    driver_config.defaultMode = P1010B_TEST_CONTROL_MODE;
    driver_config.activeReport.enable = false;

    g_can_device = device_find((char *)P1010B_TEST_CAN_NAME);
    if (g_can_device == NULL)
    {
        return OM_ERROR_NULL;
    }

    awlf_ret = device_open(g_can_device, CAN_O_INT_RX | CAN_O_INT_TX);
    if (awlf_ret != OM_OK)
    {
        return awlf_ret;
    }

    awlf_ret = p1010b_bus_init(&g_p1010b_bus, g_can_device);
    if (awlf_ret != OM_OK)
    {
        return awlf_ret;
    }

    /*
     * 当前驱动不会在 register 自动下发主动上报配置。
     * 仍建议先启动 CAN，再执行后续控制序列，避免首批同步事务丢帧。
     */
    awlf_ret = device_ctrl(g_can_device, CAN_CMD_START, NULL);
    if (awlf_ret != OM_OK)
    {
        return awlf_ret;
    }

    awlf_ret = p1010b_register(&g_p1010b_bus, &g_motor_node.handle, &driver_config);
    if (awlf_ret != OM_OK)
    {
        return awlf_ret;
    }

    return OM_OK;
}

static OmRet p1010b_sample_init_speed_pid(P1010BSpeedLoopNode *node)
{
    if (node == NULL)
    {
        return OM_ERROR_PARAM;
    }

    if (!pid_init(&node->speedPid,
                  PID_POSITIONAL_MODE,
                  node->speedPidConfig.kp,
                  node->speedPidConfig.ki,
                  node->speedPidConfig.kd))
    {
        return OM_ERROR_PARAM;
    }

    pid_set_output_limit(&node->speedPid, node->speedPidConfig.outputMin, node->speedPidConfig.outputMax);
    return OM_OK;
}

static OmRet p1010b_sample_prepare_motor(P1010BSpeedLoopNode *node)
{
    OmRet awlf_ret;
    P1010BResponse response = {0};

    if (node == NULL)
    {
        return OM_ERROR_PARAM;
    }

    awlf_ret = p1010b_disable(&node->handle, 0U, &response);
    if (awlf_ret != OM_OK)
    {
        return awlf_ret;
    }

    awlf_ret = p1010b_set_mode(&node->handle, node->mode, 0U, &response);
    if (awlf_ret != OM_OK)
    {
        return awlf_ret;
    }

    awlf_ret = p1010b_set_active_report(&node->handle, &node->handle.runtime.activeReport, 0U, &response);
    if (awlf_ret != OM_OK)
    {
        return awlf_ret;
    }

    awlf_ret = p1010b_enable(&node->handle, 0U, &response);
    if (awlf_ret != OM_OK)
    {
        return awlf_ret;
    }

    return OM_OK;
}

/**
 * @brief 将调参块同步到电机节点
 * @details
 * - 目标速度每周期直接同步；
 * - PID 参数或输出限幅变化后重建 PID；
 * - 模式变化后执行一次 disable->set_mode->enable。
 */
static OmRet p1010b_sample_sync_debug_tuning(P1010BSpeedLoopNode *node)
{
    uint8_t requested_mode;
    float requested_kp;
    float requested_ki;
    float requested_kd;
    float requested_out_min;
    float requested_out_max;
    uint8_t need_reload_pid = 0U;
    uint8_t need_switch_mode = 0U;

    if (node == NULL)
    {
        return OM_ERROR_PARAM;
    }

    node->speedTargetRpm = g_p1010b_debug_tune_block.speedTargetRpm;

    requested_mode = g_p1010b_debug_tune_block.controlMode;
    if (p1010b_sample_is_supported_control_mode(requested_mode) &&
        (requested_mode != (uint8_t)node->mode))
    {
        node->mode = (P1010BMode)requested_mode;
        need_switch_mode = 1U;
    }

    requested_kp = g_p1010b_debug_tune_block.speedPidGain[P1010B_PID_GAIN_KP_INDEX];
    requested_ki = g_p1010b_debug_tune_block.speedPidGain[P1010B_PID_GAIN_KI_INDEX];
    requested_kd = g_p1010b_debug_tune_block.speedPidGain[P1010B_PID_GAIN_KD_INDEX];
    requested_out_min = g_p1010b_debug_tune_block.speedPidOutputLimit[P1010B_PID_OUTPUT_LIMIT_MIN_INDEX];
    requested_out_max = g_p1010b_debug_tune_block.speedPidOutputLimit[P1010B_PID_OUTPUT_LIMIT_MAX_INDEX];

    if (requested_out_min < requested_out_max)
    {
        if (p1010b_sample_is_float_changed(node->speedPidConfig.kp, requested_kp))
        {
            node->speedPidConfig.kp = requested_kp;
            need_reload_pid = 1U;
        }
        if (p1010b_sample_is_float_changed(node->speedPidConfig.ki, requested_ki))
        {
            node->speedPidConfig.ki = requested_ki;
            need_reload_pid = 1U;
        }
        if (p1010b_sample_is_float_changed(node->speedPidConfig.kd, requested_kd))
        {
            node->speedPidConfig.kd = requested_kd;
            need_reload_pid = 1U;
        }
        if (p1010b_sample_is_float_changed(node->speedPidConfig.outputMin, requested_out_min))
        {
            node->speedPidConfig.outputMin = requested_out_min;
            need_reload_pid = 1U;
        }
        if (p1010b_sample_is_float_changed(node->speedPidConfig.outputMax, requested_out_max))
        {
            node->speedPidConfig.outputMax = requested_out_max;
            need_reload_pid = 1U;
        }
    }

    if (need_reload_pid != 0U)
    {
        OmRet awlf_ret = p1010b_sample_init_speed_pid(node);
        if (awlf_ret != OM_OK)
        {
            return awlf_ret;
        }
    }

    if (need_switch_mode != 0U)
    {
        OmRet awlf_ret = p1010b_sample_prepare_motor(node);
        if (awlf_ret != OM_OK)
        {
            return awlf_ret;
        }
    }

    return OM_OK;
}

static void p1010b_sample_update_debug_observer(P1010BSpeedLoopNode *node, float speed_command_rpm, float speed_feedback_rpm,
                                                float pid_output, OmRet loop_ret)
{
    if (node == NULL)
    {
        return;
    }

    g_p1010b_debug_runtime_block.speedTargetRpm = node->speedTargetRpm;
    g_p1010b_debug_runtime_block.speedCommandRpm = speed_command_rpm;
    g_p1010b_debug_runtime_block.speedFeedbackRpm = speed_feedback_rpm;
    g_p1010b_debug_runtime_block.pidOutput = pid_output;
    g_p1010b_debug_runtime_block.activeMode = (uint8_t)node->mode;
    g_p1010b_debug_runtime_block.online = p1010b_is_online(&node->handle) ? 1U : 0U;
    g_p1010b_debug_runtime_block.lastLoopRet = loop_ret;
    g_p1010b_debug_runtime_block.lastRejectReason = p1010b_get_last_reject_reason(&node->handle);
}

/* -------------------------------------------------------------------------- */
/* 速度环线程                                                                 */
/* -------------------------------------------------------------------------- */

static void p1010b_speed_loop_thread_entry(void *argument)
{
    OsalTimeMs deadline_ms = 0U;
    float speed_command_rpm = 0.0f;
    uint8_t speed_command_initialized = 0U;

    (void)argument;

    /* 上电后先完成 disable -> set_mode -> set_active_report -> enable 同步序列。 */
    for (;;)
    {
        if (p1010b_sample_prepare_motor(&g_motor_node) == OM_OK)
        {
            break;
        }
        (void)osal_sleep_ms(P1010B_TEST_RETRY_DELAY_MS);
    }

    for (;;)
    {
        const P1010BFeedback *feedback;
        float speed_feedback_rpm = 0.0f;
        float pid_output;
        OmRet awlf_ret;
        OmRet tune_ret;

        tune_ret = p1010b_sample_sync_debug_tuning(&g_motor_node);
        if (tune_ret != OM_OK)
        {
            (void)osal_sleep_ms(P1010B_TEST_RETRY_DELAY_MS);
            continue;
        }

        feedback = p1010b_get_feedback(&g_motor_node.handle);
        if (feedback != NULL)
        {
            speed_feedback_rpm = feedback->speedRpm;
        }

        if (speed_command_initialized == 0U)
        {
            speed_command_rpm = speed_feedback_rpm;
            speed_command_initialized = 1U;
        }

        speed_command_rpm = p1010b_sample_ramp_to_target(speed_command_rpm, g_motor_node.speedTargetRpm, P1010B_TEST_SOFTSTART_STEP_RPM);

        /* PID 输出单位由控制模式决定：电流模式=安培，电压模式=伏特。 */
        pid_output = pid_compute(
            &g_motor_node.speedPid,
            speed_command_rpm,
            speed_feedback_rpm,
            p1010b_sample_time_s());

        awlf_ret = p1010b_set_target(&g_motor_node.handle, pid_output);
        p1010b_sample_update_debug_observer(
            &g_motor_node,
            speed_command_rpm,
            speed_feedback_rpm,
            pid_output,
            awlf_ret);
        if (awlf_ret != OM_OK)
        {
            (void)osal_sleep_ms(P1010B_TEST_RETRY_DELAY_MS);
            continue;
        }

        if (osal_delay_until(&deadline_ms, P1010B_TEST_LOOP_PERIOD_MS, NULL) != OSAL_OK)
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
    OmRet awlf_ret;
    OsalStatus osal_ret;
    OsalThreadAttr speed_loop_thread_attr = {0};

    om_board_init();
    om_core_init();

    awlf_ret = p1010b_sample_init_bus_and_motor();
    if (awlf_ret != OM_OK)
    {
        p1010b_sample_release_resources();
        return -1;
    }

    awlf_ret = p1010b_sample_init_speed_pid(&g_motor_node);
    if (awlf_ret != OM_OK)
    {
        p1010b_sample_release_resources();
        return -1;
    }

    speed_loop_thread_attr.name = "p1010b_speed";
    speed_loop_thread_attr.priority = P1010B_TEST_THREAD_PRIORITY;
    speed_loop_thread_attr.stackSize = P1010B_TEST_THREAD_STACK;

    osal_ret = osal_thread_create(&g_speed_loop_thread, &speed_loop_thread_attr, p1010b_speed_loop_thread_entry, NULL);
    if (osal_ret != OSAL_OK)
    {
        p1010b_sample_release_resources();
        return -1;
    }

    return osal_kernel_start();
}

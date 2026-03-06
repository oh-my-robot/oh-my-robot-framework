/**
 * @file main.c
 * @brief DJI 电机通用集成测试用例
 * @note  功能：驱动总线上所有已配置电机，每 1 秒执行一次 90 度阶跃
 */
#include "drivers/motor/vendors/dji/dji_motor_drv.h"
#include "drivers/peripheral/can/pal_can_dev.h"

/* Framework Includes */
#include "core/algorithm/controller/pid.h"
#include "core/om_cpu.h"
#include "osal/osal.h"
#include "osal/osal_time.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "osal/osal_config.h"

/* --- 1. 用户配置（USER CONFIGURATION） --- */

/* 任务优先级配置（数值越大优先级越高，具体取决于 OSALConfig.h） */
#define TASK_PRIO_CONTROL 7 // 控制任务高优先级
#define TASK_PRIO_LOGIC 4   // 逻辑任务普通优先级

/* 任务堆栈大小（字节，按平台 word 大小显式换算） */
#define TASK_STACK_CONTROL (2048u * OSAL_STACK_WORD_BYTES)
#define TASK_STACK_LOGIC (1024u * OSAL_STACK_WORD_BYTES)

/* 控制参数 */
#define CONTROL_FREQ_HZ 1000  /* 1 kHz 控制频率 */
#define STEP_INTERVAL_MS 1000 /* 阶跃间隔 1000 ms */
#define STEP_ANGLE_DEG 90.0f  /* 每次阶跃角度 */

/* 位置/速度环共用 PID 配置 */
typedef struct
{
    float kp;
    float ki;
    float kd;
    float outputLimit;
} MotorPidConfig;

/* 测试电机配置结构 */
typedef struct
{
    /* 用户配置 */
    DJIMotorType motorType;             // 电机型号
    uint8_t motorId;                    // 电机 ID
    DJIMotorCtrlMode controlMode;       // 控制模式
    MotorPidConfig positionPidConfig;   // 位置环 PID 参数
    MotorPidConfig speedPidConfig;      // 速度环 PID 参数

    /* --- 运行时对象（系统自动维护） --- */
    DJIMotorDrv driverHandle;
    PidController positionPid;
    PidController speedPid;
    float targetAngleDeg;
} MotorTestNode;

#define MOTOR_TEST_NODE_INIT(_motorType, _motorId, _controlMode, _positionPidConfig, _speedPidConfig)                                     \
    {                                                                                                                                      \
        .motorType = (_motorType),                                                                                                         \
        .motorId = (_motorId),                                                                                                             \
        .controlMode = (_controlMode),                                                                                                     \
        .positionPidConfig = (_positionPidConfig),                                                                                         \
        .speedPidConfig = (_speedPidConfig),                                                                                               \
    }

/**
 * @brief 待测电机列表（配置区）
 * @note  在此处添加任意数量的电机
 */
static MotorTestNode g_motor_configs[] = {
    /* [Case 1] GM6020 ID:1 (电压模式) */
    {
        .motorType = DJI_MOTOR_TYPE_GM6020,
        .motorId = 1,
        .controlMode = DJI_CTRL_MODE_VOLTAGE,
        .positionPidConfig = {32.0f, 1.3f, 0.0f, 320.0f},
        .speedPidConfig = {50.0f, 500.0f, 0.0f, 25000.0f},
    },
    /* [Case 3] GM6020 ID:2 (电压模式) */
    // {
    //     .motorType = DJI_MOTOR_TYPE_GM6020,
    //     .motorId = 2,
    //     .controlMode = DJI_CTRL_MODE_VOLTAGE,
    //     .positionPidConfig = {32.0f, 1.3f, 0.0f, 320.0f},
    //     .speedPidConfig = {50.0f, 500.0f, 0.0f, 25000.0f},
    // },
    // /* [Case 4] GM6020 ID:3 (电压模式) */
    // {
    //     .motorType = DJI_MOTOR_TYPE_GM6020,
    //     .motorId = 3,
    //     .controlMode = DJI_CTRL_MODE_VOLTAGE,
    //     .positionPidConfig = {32.0f, 1.3f, 0.0f, 320.0f},
    //     .speedPidConfig = {50.0f, 500.0f, 0.0f, 25000.0f},
    // },
};

#define TEST_MOTOR_CNT (sizeof(g_motor_configs) / sizeof(MotorTestNode))

/* --- 2. 全局对象 --- */

static DJIMotorBus g_can1_bus;
static Device* g_can1_device_handle;

/* 任务句柄 */
static OsalThread* g_control_thread = NULL;
static OsalThread* g_logic_thread = NULL;

/* --- 3. 辅助函数 --- */

/**
 * @brief 获取系统时间（毫秒）
 * @note  基于 OSAL Tick
 */
static uint32_t get_time_ms(void)
{
    return (uint32_t)osal_time_now_monotonic();
}

static void sleep_until_ms(OsalTimeMs* last_wake_time_ms, uint32_t sleep_period_ms)
{
    if (!last_wake_time_ms)
        return;
    if (*last_wake_time_ms == 0U)
        *last_wake_time_ms = get_time_ms();
    (void)osal_delay_until(last_wake_time_ms, sleep_period_ms, NULL);
}

static float get_time_s(void)
{
    return (float)get_time_ms() / 1000.0f;
}

/**
 * @brief 初始化单个电机的 PID 与驱动对象
 */
static void motor_node_init(MotorTestNode* node)
{
    /* 1. 注册电机 */
    dji_motor_register(&g_can1_bus, &node->driverHandle, node->motorType, node->motorId, node->controlMode);

    /* 2. 初始化位置环 PID */
    pid_init(
        &node->positionPid,
        PID_POSITIONAL_MODE,
        node->positionPidConfig.kp,
        node->positionPidConfig.ki,
        node->positionPidConfig.kd);
    pid_set_output_limit(
        &node->positionPid,
        -node->positionPidConfig.outputLimit,
        node->positionPidConfig.outputLimit);
    // 自动使能输出（依赖 PID 库实现，若库未自动使能需手动设置 settings）

    /* 3. 初始化速度环 PID */
    pid_init(
        &node->speedPid,
        PID_POSITIONAL_MODE,
        node->speedPidConfig.kp,
        node->speedPidConfig.ki,
        node->speedPidConfig.kd);
    pid_set_output_limit(
        &node->speedPid,
        -node->speedPidConfig.outputLimit,
        node->speedPidConfig.outputLimit);

    /* 4. 初始目标置零，避免上电瞬时突变 */
    node->targetAngleDeg = 0.0f;
}

/* --- 4. 任务函数 --- */

/**
 * @brief 逻辑任务 (1Hz)
 * @note  负责更新目标角度
 */
void logic_task_func(void* arg)
{
    uint32_t loop_period_ms = STEP_INTERVAL_MS;
    OsalTimeMs last_wake_time_ms = get_time_ms();
    (void)arg;

    /* 延时 1 秒，等待系统稳定 */
    osal_sleep_ms(1000);

    /* 读取当前角度作为初始目标 */
    for (int i = 0; i < TEST_MOTOR_CNT; i++)
    {
        g_motor_configs[i].targetAngleDeg = dji_motor_get_total_angle(&g_motor_configs[i].driverHandle);
    }

    for (;;)
    {
        /* 更新所有电机目标角度 */
        for (int i = 0; i < TEST_MOTOR_CNT; i++)
        {
            g_motor_configs[i].targetAngleDeg += STEP_ANGLE_DEG;
        }
        sleep_until_ms(&last_wake_time_ms, loop_period_ms);
    }
}

/**
 * @brief 控制任务 (1kHz)
 * @note  负责 PID 计算与 CAN 下发
 */
void control_task_func(void* arg)
{
    uint32_t loop_period_ms = 1000 / CONTROL_FREQ_HZ; // 1ms
    OsalTimeMs last_wake_time_ms = get_time_ms();
    (void)arg;
    g_can1_device_handle = device_find("can1");
    device_open(g_can1_device_handle, CAN_O_INT_RX | CAN_O_INT_TX);
    /* 2. 驱动层初始化 */
    dji_motor_bus_init(&g_can1_bus, g_can1_device_handle);
    device_ctrl(g_can1_device_handle, CAN_CMD_START, NULL);

    /* 3. 初始化配置表中的所有电机 */
    for (int i = 0; i < TEST_MOTOR_CNT; i++)
    {
        motor_node_init(&g_motor_configs[i]);
    }
    for (;;)
    {
        /* tick handled by OSAL */
        /* 绝对延时，确保 1 kHz 频率稳定 */
        float current_time_s = get_time_s();
        /* 1. 遍历计算 PID */
        for (int i = 0; i < TEST_MOTOR_CNT; i++)
        {
            MotorTestNode* node = &g_motor_configs[i];

            /* 获取反馈 */
            float current_angle_deg = dji_motor_get_total_angle(&node->driverHandle);
            float current_speed = dji_motor_get_velocity(&node->driverHandle);

            /* 位置环计算 */
            float target_speed = pid_compute(&node->positionPid, node->targetAngleDeg, current_angle_deg, current_time_s);

            /* 速度环计算 */
            float output = pid_compute(&node->speedPid, target_speed, current_speed, current_time_s);

            /* 写入驱动缓存 */
            dji_motor_set_output(&node->driverHandle, (int16_t)output);
        }

        /* 2. 同步发送 CAN 报文 */
        dji_motor_bus_sync(&g_can1_bus);
        sleep_until_ms(&last_wake_time_ms, loop_period_ms);
    }
}

/* --- 5. 主函数 --- */

/**
 * @brief 硬件初始化与任务创建
 */
int main(void)
{
    om_board_init();
    om_core_init();
    /* 4. 创建 OSAL 任务 */
    OsalThreadAttr control_thread_attr = {0};
    OsalThreadAttr logic_thread_attr = {0};
    control_thread_attr.name = "ControlTask";
    logic_thread_attr.name = "LogicTask";
    control_thread_attr.stackSize = TASK_STACK_CONTROL;
    logic_thread_attr.stackSize = TASK_STACK_LOGIC;
    control_thread_attr.priority = TASK_PRIO_CONTROL;
    logic_thread_attr.priority = TASK_PRIO_LOGIC;
    int create_ret = osal_thread_create(&g_control_thread, &control_thread_attr, control_task_func, NULL);
    while (create_ret != OSAL_OK)
    {
    }
    create_ret = osal_thread_create(&g_logic_thread, &logic_thread_attr, logic_task_func, NULL);
    while (create_ret != OSAL_OK)
    {
    };

    osal_kernel_start();
    /* 正常情况不会运行到这里 */
    for (;;)
        ;
    return 0;
}

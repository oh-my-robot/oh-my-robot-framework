/**
 * @file main.c
 * @brief DJI 鐢垫満閫氱敤闆嗘垚娴嬭瘯鐢ㄤ緥
 * @note  鍔熻兘锛氶┍鍔ㄦ€荤嚎涓婃墍鏈夐厤缃殑鐢垫満锛屼娇鍏舵瘡闅?1 绉掕浆鍔?90 搴?(闃惰穬鍝嶅簲娴嬭瘯)
 */
#include "drivers/motor/vendors/dji/dji_motor_drv.h"
#include "drivers/peripheral/can/pal_can_dev.h"

/* Framework Includes */
#include "awlib.h"
#include "core/algorithm/controller/pid.h"
#include "core/om_cpu.h"
#include "osal/osal.h"
#include "osal/osal_time.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "osal/osal_config.h"

/* --- 1. 鐢ㄦ埛閰嶇疆鍖?(USER CONFIGURATION) --- */

/* 浠诲姟浼樺厛绾ч厤缃?(鏁板€艰秺澶т紭鍏堢骇瓒婇珮锛屽彇鍐充簬 OSALConfig.h) */
#define TASK_PRIO_CONTROL 7 // 鏈€楂樹紭鍏堢骇
#define TASK_PRIO_LOGIC 4   // 鏅€氫紭鍏堢骇

/* 浠诲姟鍫嗘爤澶у皬锛堝瓧鑺傦紝鎸夊巻鍙?word 閰嶇疆鏄惧紡鎹㈢畻锛?*/
#define TASK_STACK_CONTROL (2048u * OSAL_STACK_WORD_BYTES)
#define TASK_STACK_LOGIC (1024u * OSAL_STACK_WORD_BYTES)

/* 鎺у埗鍙傛暟 */
#define CONTROL_FREQ_HZ 1000  /* 1kHz 鎺у埗棰戠巼 */
#define STEP_INTERVAL_MS 1000 /* 鍔ㄤ綔闂撮殧 1000ms */
#define STEP_ANGLE_DEG 90.0f  /* 姣忔杞姩瑙掑害 */

/* 瀹氫箟娴嬭瘯鐢垫満閰嶇疆缁撴瀯浣?*/
typedef struct
{
    /* 鐢ㄦ埛閰嶇疆椤?*/
    DJIMotorType_e type;     // 鐢垫満鍨嬪彿
    uint8_t id;              // 鐢垫満 ID
    DJIMotorCtrlMode_e mode; // 鎺у埗妯″紡

    /* 浣嶇疆鐜?PID 鍙傛暟 */
    struct
    {
        float kp, ki, kd;
        float max_out; // 杈撳嚭闄愬箙
    } pos_pid_cfg;

    /* 閫熷害鐜?PID 鍙傛暟 */
    struct
    {
        float kp, ki, kd;
        float max_out; // 杈撳嚭闄愬箙
    } spd_pid_cfg;

    /* --- 杩愯鏃跺璞?(绯荤粺鑷姩缁存姢) --- */
    DJIMotorDrv_s handle;
    PidController_s pid_pos;
    PidController_s pid_spd;
    float target_deg;
} MotorTestNode_s;

#define MOTOR_TEST_NODE_INIT(_type, _id, _mode, _pos_pid_cfg, _spd_pid_cfg)                                                                \
    {                                                                                                                                      \
        .type = (_type),                                                                                                                   \
        .id = (_id),                                                                                                                       \
        .mode = (_mode),                                                                                                                   \
        .pos_pid_cfg = (_pos_pid_cfg),                                                                                                     \
        .spd_pid_cfg = (_spd_pid_cfg),                                                                                                     \
    }

/**
 * @brief 寰呮祴鐢垫満鍒楄〃 (閰嶇疆琛?
 * @note  鍦ㄦ澶勬坊鍔犱换鎰忔暟閲忕殑鐢垫満
 */
static MotorTestNode_s g_motor_configs[] = {
    /* [Case 1] GM6020 ID:1 (鐢靛帇妯″紡) */
    {
        .type = DJI_MOTOR_TYPE_GM6020,
        .id = 1,
        .mode = DJI_CTRL_MODE_VOLTAGE,
        .pos_pid_cfg = {32.0f, 1.3f, 0.0f, 320.0f},
        .spd_pid_cfg = {50.0f, 500.0f, 0.0f, 25000.0f},
    },
    /* [Case 3] GM6020 ID:2 (鐢靛帇妯″紡) */
    // {
    //     .type = DJI_MOTOR_TYPE_GM6020,
    //     .id = 2,
    //     .mode = DJI_CTRL_MODE_VOLTAGE,
    //     .pos_pid_cfg = {32.0f, 1.3f, 0.0f, 320.0f},
    //     .spd_pid_cfg = {50.0f, 500.0f, 0.0f, 25000.0f},
    // },
    // /* [Case 4] GM6020 ID:3 (鐢靛帇妯″紡) */
    // {
    //     .type = DJI_MOTOR_TYPE_GM6020,
    //     .id = 3,
    //     .mode = DJI_CTRL_MODE_VOLTAGE,
    //     .pos_pid_cfg = {32.0f, 1.3f, 0.0f, 320.0f},
    //     .spd_pid_cfg = {50.0f, 500.0f, 0.0f, 25000.0f},
    // },
};

#define TEST_MOTOR_CNT (sizeof(g_motor_configs) / sizeof(MotorTestNode_s))

/* --- 2. 鍏ㄥ眬瀵硅薄 --- */

static DJIMotorBus_s g_can1_bus;
static Device_t g_can1_device_handle;

/* 浠诲姟鍙ユ焺 */
OsalThread_t g_control_task_handler;
OsalThread_t g_logic_task_handler;

/* --- 3. 杈呭姪鍑芥暟 --- */

/**
 * @brief 鑾峰彇绯荤粺鏃堕棿 (绉?
 * @note  鍩轰簬 OSAL Tick
 */
static uint32_t get_time_ms(void)
{
    return (uint32_t)osal_time_now_monotonic();
}

static void sleep_until_ms(OsalTimeMs_t* last_ms, uint32_t period_ms)
{
    if (!last_ms)
        return;
    if (*last_ms == 0U)
        *last_ms = get_time_ms();
    (void)osal_delay_until(last_ms, period_ms, NULL);
}

static float get_time_s(void)
{
    return (float)get_time_ms() / 1000.0f;
}

/**
 * @brief 鍒濆鍖栧崟涓數鏈虹殑 PID 鍜岄┍鍔?
 */
static void motor_node_init(MotorTestNode_s* node)
{
    /* 1. 娉ㄥ唽鐢垫満 */
    dji_motor_register(&g_can1_bus, &node->handle, node->type, node->id, node->mode);

    /* 2. 鍒濆鍖栦綅缃幆 PID */
    pid_init(&node->pid_pos, PID_POSITIONAL_MODE, node->pos_pid_cfg.kp, node->pos_pid_cfg.ki, node->pos_pid_cfg.kd);
    pid_set_output_limit(&node->pid_pos, -node->pos_pid_cfg.max_out, node->pos_pid_cfg.max_out);
    // 鑷姩缃綅鍔熻兘浣胯兘(渚濊禆 PID 搴撳疄鐜帮紝鑻ュ簱鏈嚜鍔ㄧ疆浣嶉渶鎵嬪姩璁剧疆 settings)

    /* 3. 鍒濆鍖栭€熷害鐜?PID */
    pid_init(&node->pid_spd, PID_POSITIONAL_MODE, node->spd_pid_cfg.kp, node->spd_pid_cfg.ki, node->spd_pid_cfg.kd);
    pid_set_output_limit(&node->pid_spd, -node->spd_pid_cfg.max_out, node->spd_pid_cfg.max_out);

    /* 4. 鍒濆鐩爣璁句负褰撳墠瑙掑害锛岄槻姝笂鐢甸杞?*/
    node->target_deg = 0.0f;
}

/* --- 4. 浠诲姟鍑芥暟 --- */

/**
 * @brief 閫昏緫浠诲姟 (1Hz)
 * @note  璐熻矗鏇存柊鐩爣瑙掑害
 */
void logic_task_func(void* pvParameters)
{
    uint32_t period_ms = STEP_INTERVAL_MS;
    OsalTimeMs_t last_ms = get_time_ms();
    (void)pvParameters;

    /* 寤舵椂 1s 绛夊緟绯荤粺绋冲畾 */
    osal_sleep_ms(1000);

    /* 璇诲彇褰撳墠鐢垫満瑙掑害浣滀负鍒濆鐩爣(鍙€? */
    for (int i = 0; i < TEST_MOTOR_CNT; i++)
    {
        g_motor_configs[i].target_deg = dji_motor_get_total_angle(&g_motor_configs[i].handle);
    }

    for (;;)
    {
        /* 鏇存柊鎵€鏈夌數鏈虹殑鐩爣瑙掑害 */
        for (int i = 0; i < TEST_MOTOR_CNT; i++)
        {
            g_motor_configs[i].target_deg += STEP_ANGLE_DEG;
        }
        sleep_until_ms(&last_ms, period_ms);
    }
}

/**
 * @brief 鎺у埗浠诲姟 (1kHz)
 * @note  璐熻矗 PID 璁＄畻鍜?CAN 鍙戦€?
 */
void control_task_func(void* pvParameters)
{
    uint32_t period_ms = 1000 / CONTROL_FREQ_HZ; // 1ms
    OsalTimeMs_t last_ms = get_time_ms();
    (void)pvParameters;
    g_can1_device_handle = device_find("can1");
    device_open(g_can1_device_handle, CAN_O_INT_RX | CAN_O_INT_TX);
    /* 2. 椹卞姩灞傚垵濮嬪寲 */
    dji_motor_bus_init(&g_can1_bus, g_can1_device_handle);
    device_ctrl(g_can1_device_handle, CAN_CMD_START, NULL);

    /* 3. 鍒濆鍖栭厤缃〃涓殑鎵€鏈夌數鏈?*/
    for (int i = 0; i < TEST_MOTOR_CNT; i++)
    {
        motor_node_init(&g_motor_configs[i]);
    }
    for (;;)
    {
        /* tick handled by OSAL */
        /* 缁濆寤舵椂锛岀‘淇?1kHz 棰戠巼绋冲畾 */
        float current_time = get_time_s();
        /* 1. 閬嶅巻璁＄畻 PID */
        for (int i = 0; i < TEST_MOTOR_CNT; i++)
        {
            MotorTestNode_s* node = &g_motor_configs[i];

            /* 鑾峰彇鍙嶉 */
            float now_deg = dji_motor_get_total_angle(&node->handle);
            float now_spd = dji_motor_get_velocity(&node->handle);

            /* 浣嶇疆鐜绠?*/
            float tgt_spd = pid_compute(&node->pid_pos, node->target_deg, now_deg, current_time);

            /* 閫熷害鐜绠?*/
            float output = pid_compute(&node->pid_spd, tgt_spd, now_spd, current_time);

            /* 鍐欏叆椹卞姩缂撳瓨 */
            dji_motor_set_output(&node->handle, (int16_t)output);
        }

        /* 2. 鍚屾鍙戦€?CAN 鎶ユ枃 */
        dji_motor_bus_sync(&g_can1_bus);
        sleep_until_ms(&last_ms, period_ms);
    }
}

/* --- 5. 涓诲嚱鏁?--- */

/**
 * @brief 纭欢鍒濆鍖栦笌浠诲姟鍒涘缓
 */
int main(void)
{
    om_board_init();
    om_core_init();
    /* 4. 鍒涘缓 OSAL 浠诲姟 */
    OsalThreadAttr_s control_attr = {0};
    OsalThreadAttr_s logic_attr = {0};
    control_attr.name = "ControlTask";
    logic_attr.name = "LogicTask";
    control_attr.stackSize = TASK_STACK_CONTROL;
    logic_attr.stackSize = TASK_STACK_LOGIC;
    control_attr.priority = TASK_PRIO_CONTROL;
    logic_attr.priority = TASK_PRIO_LOGIC;
    int ret = osal_thread_create(&g_control_task_handler, &control_attr, control_task_func, NULL);
    while (ret != OSAL_OK)
    {
    }
    ret = osal_thread_create(&g_logic_task_handler, &logic_attr, logic_task_func, NULL);
    while (ret != OSAL_OK)
    {
    };

    osal_kernel_start();
    /* 姝ｅ父鎯呭喌涓嶄細杩愯鍒拌繖閲?*/
    for (;;)
        ;
    return 0;
}


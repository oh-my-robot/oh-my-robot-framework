/**
 * @file dji_motor_drv.h
 * @brief RoboMaster DJI 电机驱动接口
 * @author Yuhao
 * @date 2025/12/22
 * @details
 * 该模块面向 C610/C620/GM6020 系列 DJI 电机，提供 O(1) 电机 ID 路由。
 * 采用“先写目标，再统一总线同步”的方式，兼顾实时性与总线负载可控性。
 */

#ifndef __DJI_MOTOR_DRIVER_H__
#define __DJI_MOTOR_DRIVER_H__

#include "dji_motor_conf.h"
#include "drivers/model/device.h"
#include "drivers/peripheral/can/pal_can_dev.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- 电机 ID 映射配置 --- */
/** @brief 接收 ID 映射起始值 */
#define DJI_RX_ID_START DJI_MOTOR_RX_ID_MIN
/** @brief 接收 ID 映射结束值 */
#define DJI_RX_ID_END DJI_MOTOR_RX_ID_MAX
/** @brief 直接映射表长度 */
#define DJI_MOTOR_RX_MAP_SIZE (DJI_RX_ID_END - DJI_RX_ID_START + 1)

/* --- 协议常量定义 --- */
/** @brief C6x0（ID 1~4）控制帧 ID */
#define DJI_TX_ID_C6X0_1_4 0x200
/** @brief 混合控制帧 ID（C6x0 ID 5~8 / GM6020 电压模式 ID 1~4） */
#define DJI_TX_ID_MIX_1_FF 0x1FF
/** @brief GM6020 电压模式（ID 5~7）控制帧 ID */
#define DJI_TX_ID_GM6020_V_5_7 0x2FF
/** @brief GM6020 电流模式（ID 1~4）控制帧 ID */
#define DJI_TX_ID_GM6020_C_1_4 0x1FE
/** @brief GM6020 电流模式（ID 5~7）控制帧 ID */
#define DJI_TX_ID_GM6020_C_5_7 0x2FE

/** @brief 无故障 */
#define DJI_ERR_NONE 0x00
/** @brief 内存异常 */
#define DJI_ERR_MEM 0x01
/** @brief 电压异常 */
#define DJI_ERR_VOLT 0x02
/** @brief 相位异常 */
#define DJI_ERR_PHASE 0x03
/** @brief 传感器异常 */
#define DJI_ERR_SENSOR 0x04
/** @brief 过温预警 */
#define DJI_ERR_TEMP_HIGH 0x05
/** @brief 堵转 */
#define DJI_ERR_STALL 0x06
/** @brief 标定异常 */
#define DJI_ERR_CALIB 0x07
/** @brief 过热保护 */
#define DJI_ERR_OVERHEAT 0x08

/**
 * @brief DJI 电机型号
 */
typedef enum {
    DJI_MOTOR_TYPE_C610 = 0, /**< C610，对应 M2006 */
    DJI_MOTOR_TYPE_C620,     /**< C620，对应 M3508 */
    DJI_MOTOR_TYPE_GM6020,   /**< GM6020 云台电机 */
    DJI_MOTOR_TYPE_UNKNOWN   /**< 未知类型/占位 */
} DJIMotorType;

/**
 * @brief DJI 控制模式
 */
typedef enum {
    DJI_CTRL_MODE_CURRENT = 0, /**< 电流控制模式 */
    DJI_CTRL_MODE_VOLTAGE      /**< 电压控制模式，仅 GM6020 支持 */
} DJIMotorCtrlMode;

/* 前置声明 */
typedef struct DJIMotorTxUnit DJIMotorTxUnit;
typedef struct DJIMotorDrv DJIMotorDrv;

/**
 * @brief 错误状态回调
 * @param motor 电机句柄
 * @param error_code 当前错误码，0 表示恢复正常
 */
typedef void (*DJIMotorErrorCallback)(DJIMotorDrv *motor, uint8_t error_code);

/**
 * @brief DJI 发送单元（1 个 TxUnit 对应 1 帧 CAN 控制帧）
 */
struct DJIMotorTxUnit {
    ListHead list; /**< 发送链表节点 */
    uint32_t canId;        /**< 该发送单元绑定的标准帧 ID */
    uint8_t txBuffer[8];   /**< 8 字节控制缓存，每 2 字节对应一台电机 */
    uint8_t isDirty;       /**< 脏标记，1 表示缓存已更新 */
    uint8_t usageMask;     /**< 槽位占用位图，bit0~bit3 对应 4 台电机 */
};

/**
 * @brief DJI 电机驱动句柄
 */
struct DJIMotorDrv {
    /**
     * @brief 静态路由信息（注册时维护）
     */
    struct
    {
        uint16_t rxId;            /**< 反馈帧 ID */
        uint8_t txBufIdx;         /**< 在 txBuffer 中的起始字节下标（0/2/4/6） */
        DJIMotorTxUnit *txUnit; /**< 对应发送单元 */
        DJIMotorType type;      /**< 电机型号 */
    } link;

    /**
     * @brief 运行时测量值（通过 API 读取）
     */
    struct
    {
        int16_t angle;    /**< 单圈角度值，范围 0~8191 */
        int16_t velocity; /**< 转速，单位 RPM */
        int16_t current;  /**< 原始电流/电压反馈值 */
        uint8_t temp;     /**< 温度，单位摄氏度 */

        uint8_t errorCode;     /**< 当前错误码 */
        uint8_t lastErrorCode; /**< 上一帧错误码（用于边沿检测） */

        int32_t totalAngle; /**< 多圈累计角度原始值 */
        uint16_t lastAngle; /**< 上一帧单圈角度 */
        int32_t roundCount; /**< 累计圈数 */
    } measure;

    int16_t targetOutput;            /**< 当前目标输出值 */
    DJIMotorCtrlMode mode;         /**< 控制模式 */
    float scale;                     /**< 反馈值缩放系数 */
    DJIMotorErrorCallback errorCallback; /**< 错误状态回调 */
};

/**
 * @brief DJI 总线对象（一个 CAN 设备对应一个总线实例）
 */
typedef struct
{
    Device* canDev;                                /**< 绑定的 CAN 设备 */
    CanFilterHandle filterHandle;                 /**< 接收过滤器句柄 */
    DJIMotorDrv *rxMap[DJI_MOTOR_RX_MAP_SIZE];   /**< O(1) 的接收 ID 映射表 */
    ListHead txList;                        /**< 待发送控制帧链表 */
} DJIMotorBus;

/* --- Core API --- */

/**
 * @brief 初始化 DJI 电机总线
 * @param bus 总线实例
 * @param can_dev 已打开的 CAN 设备
 * @return `OM_OK` 表示成功，其他值表示初始化失败原因
 */
OmRet dji_motor_bus_init(DJIMotorBus *bus, Device* can_dev);

/**
 * @brief 注册单个电机到指定总线
 * @param bus 总线实例
 * @param motor 电机实例
 * @param type 电机型号
 * @param id 电机 ID，范围约束为 1~8
 * @param mode 控制模式（电流/电压）
 * @return `OM_OK` 表示成功，`OM_ERR_CONFLICT` 表示 ID 或槽位冲突，
 *         `OM_ERROR_MEMORY` 表示发送单元资源不足，其他值表示参数非法
 */
OmRet dji_motor_register(DJIMotorBus *bus, DJIMotorDrv *motor, DJIMotorType type, uint8_t id, DJIMotorCtrlMode mode);

/**
 * @brief 设置目标输出值（仅写缓存，不立即发 CAN）
 * @param motor 电机实例
 * @param output 目标输出原始值
 */
void dji_motor_set_output(DJIMotorDrv *motor, int16_t output);

/**
 * @brief 同步发送当前周期的所有控制帧
 * @param bus 总线实例
 */
void dji_motor_bus_sync(DJIMotorBus *bus);

/* --- Error Handling API --- */

/**
 * @brief 配置错误状态回调
 * @param motor 电机实例
 * @param callback 回调函数指针
 * @note 回调在错误码发生变化时触发
 */
static inline void dji_motor_config_error_callback(DJIMotorDrv *motor, DJIMotorErrorCallback callback)
{
    if (motor)
        motor->errorCallback = callback;
}

/**
 * @brief 清空软件侧错误记录
 * @param motor 电机实例
 * @note 该接口只清理软件缓存，不会改变电机硬件状态
 */
static inline void dji_motor_clear_error(DJIMotorDrv *motor)
{
    if (motor) {
        motor->measure.errorCode = 0;
        motor->measure.lastErrorCode = 0;
    }
}

/* --- Data Access API --- */

/**
 * @brief 获取多圈累计角度
 * @param motor 电机实例
 * @return 角度值，单位：度
 */
static inline float dji_motor_get_total_angle(DJIMotorDrv *motor)
{
    if (!motor)
        return 0.0f;
    return (float)motor->measure.totalAngle * (360.0f / 8192.0f);
}

/**
 * @brief 获取单圈角度
 * @param motor 电机实例
 * @return 角度值，单位：度
 */
static inline float dji_motor_get_singgle_angle(DJIMotorDrv *motor)
{
    if (!motor)
        return 0.0f;
    return (float)motor->measure.angle * (360.0f / 8192.0f);
}

/**
 * @brief 获取当前转速
 * @param motor 电机实例
 * @return 转速，单位：RPM
 */
static inline float dji_motor_get_velocity(DJIMotorDrv *motor)
{
    if (!motor)
        return 0.0f;
    return (float)motor->measure.velocity;
}

/**
 * @brief 获取当前电流反馈（按比例缩放后）
 * @param motor 电机实例
 * @return 电流值，单位：A
 */
static inline float dji_motor_get_current(DJIMotorDrv *motor)
{
    if (!motor || motor->link.type >= DJI_MOTOR_TYPE_UNKNOWN)
        return 0.0f;
    return (float)motor->measure.current * motor->scale;
}

/**
 * @brief 获取当前温度
 * @param motor 电机实例
 * @return 温度值，单位：摄氏度
 */
static inline float dji_motor_get_temp(DJIMotorDrv *motor)
{
    if (!motor)
        return 0.0f;
    return (float)motor->measure.temp;
}

/**
 * @brief 获取当前错误码
 * @param motor 电机实例
 * @return 原始错误码
 */
static inline uint8_t dji_motor_get_error_code(DJIMotorDrv *motor)
{
    if (!motor)
        return 0;
    return motor->measure.errorCode;
}

/**
 * @brief 重置反馈累计状态（角度/圈数）
 * @param motor 电机实例
 * @note `lastAngle` 会在下一帧反馈处理中重新建立
 */
static inline void dji_motor_reset_feedback(DJIMotorDrv *motor)
{
    if (motor) {
        motor->measure.totalAngle = 0;
        motor->measure.roundCount = 0;
        motor->measure.angle = 0;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* __DJI_MOTOR_DRIVER_H__ */


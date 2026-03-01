/**
 * @file dji_motor_drv.h
 * @brief RoboMaster DJI 电机驱动对外接口
 * @author Yuhao
 * @date 2025/12/22
 * @details
 * 本模块面向 C610/C620/GM6020 等 DJI 协议电机，提供 O(1) 反馈分发、
 * 静态发送单元管理，以及 set_output 与 bus_sync 分离的控制时序。
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

/* --- 自动推导配置参数 --- */
/** @brief 反馈 ID 映射表起始值（含） */
#define DJI_RX_ID_START DJI_MOTOR_RX_ID_MIN
/** @brief 反馈 ID 映射表结束值（含） */
#define DJI_RX_ID_END DJI_MOTOR_RX_ID_MAX
/** @brief 反馈直接映射表长度 */
#define DJI_MOTOR_RX_MAP_SIZE (DJI_RX_ID_END - DJI_RX_ID_START + 1)

/* --- 协议常量与错误码 --- */
/** @brief C6x0 电机（ID 1~4）控制帧 ID */
#define DJI_TX_ID_C6X0_1_4 0x200
/** @brief 混合控制帧 ID（C6x0 ID 5~8 / GM6020 电压模式 ID 1~4） */
#define DJI_TX_ID_MIX_1_FF 0x1FF
/** @brief GM6020 电压模式（ID 5~7）控制帧 ID */
#define DJI_TX_ID_GM6020_V_5_7 0x2FF
/** @brief GM6020 电流模式（ID 1~4）控制帧 ID */
#define DJI_TX_ID_GM6020_C_1_4 0x1FE
/** @brief GM6020 电流模式（ID 5~7）控制帧 ID */
#define DJI_TX_ID_GM6020_C_5_7 0x2FE

/** @brief 无错误 */
#define DJI_ERR_NONE 0x00
/** @brief 内存相关错误 */
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
/** @brief 过热 */
#define DJI_ERR_OVERHEAT 0x08

/**
 * @brief DJI 电机类型
 */
typedef enum {
    DJI_MOTOR_TYPE_C610 = 0, /**< C610（典型对应 M2006） */
    DJI_MOTOR_TYPE_C620,     /**< C620（典型对应 M3508） */
    DJI_MOTOR_TYPE_GM6020,   /**< GM6020（云台电机） */
    DJI_MOTOR_TYPE_UNKNOWN   /**< 未知类型/非法占位 */
} DJIMotorType_e;

/**
 * @brief DJI 电机控制模式
 */
typedef enum {
    DJI_CTRL_MODE_CURRENT = 0, /**< 电流控制模式 */
    DJI_CTRL_MODE_VOLTAGE      /**< 电压控制模式（主要用于 GM6020） */
} DJIMotorCtrlMode_e;

/* 前置声明 */
typedef struct DJIMotorTxUnit DJIMotorTxUnit_s;
typedef struct DJIMotorDrv DJIMotorDrv_s;

/**
 * @brief 电机错误状态回调
 * @param motor 出错电机对象
 * @param error_code 当前错误码（0 表示恢复正常）
 */
typedef void (*DJIMotorErrorCb_t)(DJIMotorDrv_s *motor, uint8_t error_code);

/**
 * @brief DJI 发送单元（1 个 TxUnit 对应 1 帧 CAN 控制报文）
 */
struct DJIMotorTxUnit {
    ListHead_s list; /**< 所属总线发送链表节点 */
    uint32_t canId;        /**< 该发送单元绑定的标准帧 ID */
    uint8_t txBuffer[8];   /**< 8 字节控制负载，按 2 字节一组控制电机 */
    uint8_t isDirty;       /**< 脏标记：1 表示缓存有更新待发送 */
    uint8_t usageMask;     /**< 槽位占用位图，bit0~bit3 对应 4 个控制槽 */
};

/**
 * @brief DJI 电机对象句柄
 */
struct DJIMotorDrv {
    /**
     * @brief 静态路由信息（注册后由驱动维护）
     */
    struct
    {
        uint16_t rxId;            /**< 本电机反馈帧 ID */
        uint8_t txBufIdx;         /**< 在 txBuffer 中的起始字节下标（0/2/4/6） */
        DJIMotorTxUnit_s *txUnit; /**< 对应发送单元 */
        DJIMotorType_e type;      /**< 电机类型 */
    } link;

    /**
     * @brief 运行时反馈量测（建议通过访问 API 获取）
     */
    struct
    {
        int16_t angle;    /**< 单圈编码值（0~8191） */
        int16_t velocity; /**< 反馈转速（RPM） */
        int16_t current;  /**< 原始电流/电压编码值 */
        uint8_t temp;     /**< 电机温度（摄氏度） */

        uint8_t errorCode;     /**< 当前错误码 */
        uint8_t lastErrorCode; /**< 上一帧错误码（用于边沿触发） */

        int32_t totalAngle; /**< 多圈累计角度编码值 */
        uint16_t lastAngle; /**< 上一帧单圈角度 */
        int32_t roundCount; /**< 过零累计圈数 */
    } measure;

    int16_t targetOutput;            /**< 当前目标输出编码值 */
    DJIMotorCtrlMode_e mode;         /**< 控制模式 */
    float scale;                     /**< 编码值到物理量的换算比例 */
    DJIMotorErrorCb_t errorCallback; /**< 错误状态回调 */
};

/**
 * @brief DJI 电机总线管理器（一条 CAN 总线对应一个实例）
 */
typedef struct
{
    Device_t canDev;                                /**< 底层 CAN 设备句柄 */
    CanFilterHandle_t filterHandle;                 /**< 过滤器句柄（初始化时分配） */
    DJIMotorDrv_s *rxMap[DJI_MOTOR_RX_MAP_SIZE];   /**< O(1) 反馈 ID 映射表 */
    ListHead_s txList;                        /**< 待发送帧链表 */
} DJIMotorBus_s;

/* --- Core API --- */

/**
 * @brief 初始化 DJI 电机总线管理器
 * @param bus 总线管理器实例
 * @param can_dev 已打开的 CAN 设备句柄
 * @return `OM_OK` 表示成功，其它返回值表示初始化失败原因
 */
OmRet_e dji_motor_bus_init(DJIMotorBus_s *bus, Device_t can_dev);

/**
 * @brief 注册电机到指定总线并建立收发路由
 * @param bus 总线管理器实例
 * @param motor 电机对象实例
 * @param type 电机类型
 * @param id 电机物理 ID（建议 1~8，最终受配置范围约束）
 * @param mode 控制模式（电流或电压）
 * @return `OM_OK` 表示成功，`OM_ERR_CONFLICT` 表示 ID/槽位冲突，
 *         `OM_ERROR_MEMORY` 表示发送单元池耗尽，其它返回值表示参数非法
 */
OmRet_e dji_motor_register(DJIMotorBus_s *bus, DJIMotorDrv_s *motor, DJIMotorType_e type, uint8_t id, DJIMotorCtrlMode_e mode);

/**
 * @brief 设置电机输出编码值（仅写缓存，不立即发 CAN）
 * @param motor 电机对象
 * @param output 目标输出编码值
 */
void dji_motor_set_output(DJIMotorDrv_s *motor, int16_t output);

/**
 * @brief 同步发送总线上的所有脏帧
 * @param bus 总线管理器实例
 */
void dji_motor_bus_sync(DJIMotorBus_s *bus);

/* --- Error Handling API --- */

/**
 * @brief 配置错误状态回调
 * @param motor 电机对象
 * @param callback 回调函数指针
 * @note 回调为边沿触发，仅当错误码变化时触发
 */
static inline void dji_motor_config_error_callback(DJIMotorDrv_s *motor, DJIMotorErrorCb_t callback)
{
    if (motor)
        motor->errorCallback = callback;
}

/**
 * @brief 清除软件侧错误状态记录
 * @param motor 电机对象
 * @note 若硬件错误未恢复，下一帧反馈会重新置位
 */
static inline void dji_motor_clear_error(DJIMotorDrv_s *motor)
{
    if (motor) {
        motor->measure.errorCode = 0;
        motor->measure.lastErrorCode = 0;
    }
}

/* --- Data Access API --- */

/**
 * @brief 获取多圈累计角度
 * @param motor 电机对象
 * @return 角度值（单位：度）
 */
static inline float dji_motor_get_total_angle(DJIMotorDrv_s *motor)
{
    if (!motor)
        return 0.0f;
    return (float)motor->measure.totalAngle * (360.0f / 8192.0f);
}

/**
 * @brief 获取单圈角度
 * @param motor 电机对象
 * @return 角度值（单位：度）
 */
static inline float dji_motor_get_singgle_angle(DJIMotorDrv_s *motor)
{
    if (!motor)
        return 0.0f;
    return (float)motor->measure.angle * (360.0f / 8192.0f);
}

/**
 * @brief 获取当前转速
 * @param motor 电机对象
 * @return 转速（单位：RPM）
 */
static inline float dji_motor_get_velocity(DJIMotorDrv_s *motor)
{
    if (!motor)
        return 0.0f;
    return (float)motor->measure.velocity;
}

/**
 * @brief 获取当前实际扭矩电流
 * @param motor 电机对象
 * @return 电流值（单位：A）
 */
static inline float dji_motor_get_current(DJIMotorDrv_s *motor)
{
    if (!motor || motor->link.type >= DJI_MOTOR_TYPE_UNKNOWN)
        return 0.0f;
    return (float)motor->measure.current * motor->scale;
}

/**
 * @brief 获取当前温度
 * @param motor 电机对象
 * @return 温度（单位：摄氏度）
 */
static inline float dji_motor_get_temp(DJIMotorDrv_s *motor)
{
    if (!motor)
        return 0.0f;
    return (float)motor->measure.temp;
}

/**
 * @brief 获取当前错误码
 * @param motor 电机对象
 * @return 错误码原始值
 */
static inline uint8_t dji_motor_get_error_code(DJIMotorDrv_s *motor)
{
    if (!motor)
        return 0;
    return motor->measure.errorCode;
}

/**
 * @brief 重置反馈积分状态（如多圈角度）
 * @param motor 电机对象
 * @note 不重置 lastAngle，避免下一帧计算出现大幅跳变
 */
static inline void dji_motor_reset_feedback(DJIMotorDrv_s *motor)
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


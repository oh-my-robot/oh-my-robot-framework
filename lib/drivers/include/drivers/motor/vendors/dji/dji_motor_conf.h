/**
 * @file dji_motor_config.h
 * @brief DJI 电机驱动编译时配置文件
 * @note  用户可以在此文件中根据项目需求裁剪资源
 */
#ifndef __DJI_MOTOR_CONFIG_H__
#define DJI_MOTOR_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- 资源分配配置 --- */

/* --- 1. 接收范围配置 --- */

/**
 * @brief 启用的最小反馈 ID (默认 0x201)
 * @note  C6x0 ID1 = 0x201, GM6020 ID1 = 0x205
 */
#ifndef DJI_MOTOR_RX_ID_MIN
#define DJI_MOTOR_RX_ID_MIN 0x201
#endif

/**
 * @brief 启用的最大反馈 ID (默认 0x20B)
 * @note  C6x0 ID8 = 0x208, GM6020 ID7 = 0x20B
 * @example 如果只使用 4 个 C610/C620 (ID 1-4)，可将此值设为 0x204 以节省内存
 */
#ifndef DJI_MOTOR_RX_ID_MAX
#define DJI_MOTOR_RX_ID_MAX 0x20B
#endif

/**
 * @brief 发送单元(TxUnit) 静态内存池大小
 * @note  决定了全系统（所有 CAN 总线共享）可分配 TxUnit 的总上限。
 *        实际需求取决于各总线“独立控制帧组数”之和。
 *        该值过小会导致注册阶段返回 OM_ERROR_MEMORY，建议预留余量。
 */
#ifndef DJI_MOTOR_MAX_TX_UNITS
#define DJI_MOTOR_MAX_TX_UNITS 16
#endif

/* --- 调试与断言 --- */

/**
 * @brief 断言宏
 * @note  默认使用死循环挂起
 */
// TODO:对接系统的 ASSERT 接口
#ifndef DJI_ASSERT
#define DJI_ASSERT(cond) \
    do {                 \
        if (!(cond)) {   \
            while (1);   \
        }                \
    } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __DJI_MOTOR_CONFIG_H__ */

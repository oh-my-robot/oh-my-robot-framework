#ifndef __OM_CONFIG_H__
#define __OM_CONFIG_H__

/* 功能裁剪 */
#define OM_USE_ASSERT // TODO: 使能框架断言

/* 抽象层裁剪 */
#define OM_USE_HAL_SERIALS
#define OM_USE_HAL_CAN

/*
 * sync 加速开关（统一入口）：
 *
 * - OM_SYNC_ACCEL: 全局加速开关，用于标记是否启用加速后端
 *
 * 说明：
 * - 分原语 capability 由 port 层注入（如 OM_SYNC_ACCEL_CAP_COMPLETION）。
 * - 当前 FreeRTOS completion 无独立加速后端，默认回退 reference。
 */
#ifndef OM_SYNC_ACCEL
#define OM_SYNC_ACCEL 0
#endif

#endif // __OM_CONFIG_H__

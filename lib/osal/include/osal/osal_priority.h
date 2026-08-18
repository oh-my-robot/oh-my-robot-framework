/**
 * @file    osal_priority.h
 * @brief   统一线程优先级规范（参照通用 RTOS 优先级语义带宽模型）
 * @details
 *
 * 将 OSAL_PRIORITY_MAX 个优先级划分为 7 个语义带宽，每个带宽跨度 = OSAL_PRIO_BAND_WIDTH。
 * 子系统通过 `OSAL_PRIO_<band>_BASE + offset` 构造实际优先级值，不再散布 magic number。
 *
 * `offset` 范围 [0, OSAL_PRIO_BAND_WIDTH-1]，允许同带内微调相对顺序。
 *
 * ## 当前端口映射示例（数值越大优先级越高）
 *
 *   IDLE:          0-3   (0 = 仅 idle task 自身)
 *   LOW:           4-7   (后台维护：日志刷新、状态上报)
 *   NORMAL:        8-11  (框架 worker / 普通驱动线程默认值)
 *   ABOVE_NORMAL: 12-15  (关键驱动 worker：SPI/CAN 异步调度)
 *   HIGH:         16-19  (实时控制：电机环、姿态解算)
 *   CRITICAL:     20-23  (极低延迟：IPC producer、中断下半部)
 *   REALTIME:     24-27  (硬实时：高精度定时器回调)
 *   保留:          28-31  (28-30 系统级，31 = RTOS 定时器任务)
 *
 * 对于数值越小优先级越高的 RTOS，端口层需提供映射宏
 * `osal_prio_to_native()` 完成反转。
 */

#ifndef OM_OSAL_PRIORITY_H
#define OM_OSAL_PRIORITY_H

#include "osal_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 带宽定义
 *===========================================================================*/

/** 每个语义带宽在 RTOS 中所占的 slot 数 */
#define OSAL_PRIO_BAND_WIDTH 4u

/** 语义优先级基准值（实际优先级 = base + offset，offset ∈ [0, BAND_WIDTH-1]） */
#define OSAL_PRIO_IDLE_BASE         0u
#define OSAL_PRIO_LOW_BASE          (OSAL_PRIO_IDLE_BASE + OSAL_PRIO_BAND_WIDTH)
#define OSAL_PRIO_NORMAL_BASE       (OSAL_PRIO_LOW_BASE + OSAL_PRIO_BAND_WIDTH)
#define OSAL_PRIO_ABOVE_NORMAL_BASE (OSAL_PRIO_NORMAL_BASE + OSAL_PRIO_BAND_WIDTH)
#define OSAL_PRIO_HIGH_BASE         (OSAL_PRIO_ABOVE_NORMAL_BASE + OSAL_PRIO_BAND_WIDTH)
#define OSAL_PRIO_CRITICAL_BASE     (OSAL_PRIO_HIGH_BASE + OSAL_PRIO_BAND_WIDTH)
#define OSAL_PRIO_REALTIME_BASE     (OSAL_PRIO_CRITICAL_BASE + OSAL_PRIO_BAND_WIDTH)

/*===========================================================================
 * 便捷别名
 *===========================================================================*/

/** 最低可用优先级（= idle task 自身） */
#define OSAL_PRIO_LOWEST OSAL_PRIO_IDLE_BASE

/** 最高可用优先级（保留给系统级任务，应用层不应使用） */
#define OSAL_PRIO_HIGHEST (OSAL_PRIORITY_MAX - 1u)

/*===========================================================================
 * 编译期校验
 *===========================================================================*/

#if (OSAL_PRIO_REALTIME_BASE + OSAL_PRIO_BAND_WIDTH) > OSAL_PRIORITY_MAX
#error "OSAL priority bands exceed OSAL_PRIORITY_MAX. \
  Reduce OSAL_PRIO_BAND_WIDTH or increase OSAL_PRIORITY_MAX."
#endif

#ifdef __cplusplus
}
#endif

#endif /* OM_OSAL_PRIORITY_H */

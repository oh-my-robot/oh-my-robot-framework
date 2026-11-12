/**
 * @file osal_time_freertos.h
 * @brief FreeRTOS 时间换算辅助函数
 */
#ifndef __OM_OSAL_TIME_FREERTOS_H__
#define __OM_OSAL_TIME_FREERTOS_H__

#include <stdint.h>

#include "FreeRTOS.h"
#include "osal/osal_core.h"

/**
 * @brief 获取允许的最大有限等待 tick
 * @note 当支持 `INCLUDE_vTaskSuspend` 时，需避开 `portMAX_DELAY`（保留给 WAIT_FOREVER）。
 */
static inline TickType_t osal_max_finite_wait_ticks(void)
{
#if (INCLUDE_vTaskSuspend == 1)
    if (portMAX_DELAY > (TickType_t)0u)
        return (TickType_t)(portMAX_DELAY - (TickType_t)1u);
    return (TickType_t)0u;
#else
    return portMAX_DELAY;
#endif
}

/**
 * @brief 毫秒转 tick（向上取整）
 * @note
 * - `ms=0` 返回 0（非阻塞）。
 * - `ms=OSAL_WAIT_FOREVER` 返回 `portMAX_DELAY`。
 * - 其他值基于 `pdMS_TO_TICKS` 计算，并饱和到最大有限等待 tick。
 */
static inline TickType_t osal_ms_to_ticks(uint32_t ms)
{
    TickType_t max_finite_ticks = osal_max_finite_wait_ticks();

    if (ms == OSAL_WAIT_FOREVER)
        return portMAX_DELAY;
    if (ms == 0U)
        return 0;

    /* 先做边界饱和，避免 TickType_t 截断后丢失溢出信息。 */
    if ((uint32_t)pdTICKS_TO_MS(max_finite_ticks) < ms)
        return max_finite_ticks;

    TickType_t ticks = pdMS_TO_TICKS(ms);

    /* pdMS_TO_TICKS 默认向下取整，这里补齐到“向上取整”语义。 */
    if (ticks < max_finite_ticks)
    {
        if ((uint32_t)pdTICKS_TO_MS(ticks) < ms)
            ticks++;
    }

    if (ticks > max_finite_ticks)
        return max_finite_ticks;
    return ticks;
}

/**
 * @brief tick 转毫秒（截断）
 * @note 基于 `pdTICKS_TO_MS`，保持 OSAL 对外 ms 精度合同，不承诺亚毫秒分辨率。
 */
static inline uint32_t osal_ticks_to_ms32(TickType_t ticks)
{
    return (uint32_t)pdTICKS_TO_MS(ticks);
}

/**
 * @brief 将 FreeRTOS 等待结果映射为 OSAL 统一状态码
 */
static inline OsalStatus osal_wait_result_to_status(BaseType_t ok, uint32_t timeout_ms)
{
    if (ok == pdPASS)
        return OSAL_OK;
    if (timeout_ms == 0u)
        return OSAL_WOULD_BLOCK;
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return OSAL_INTERNAL;
    return OSAL_TIMEOUT;
}

#endif


/**
 * @file osal_time_freertos.c
 * @brief FreeRTOS 端 OSAL time 实现（32 位毫秒时基）
 */
#include "osal/osal_port.h"
#include "osal/osal_time.h"

#include "FreeRTOS.h"
#include "osal_time_freertos.h"
#include "task.h"

#if (OM_OSAL_PORT == OSAL_PORT_FREERTOS)

/**
 * @brief 获取当前 tick 计数
 * @note FreeRTOS tick 为原生 32 位计数，回绕由比较函数在语义层处理。
 */
static TickType_t osal_tick_count(void)
{
    return osal_is_in_isr() ? xTaskGetTickCountFromISR() : xTaskGetTickCount();
}

/**
 * @brief 获取单调毫秒时钟（32 位）
 * @note 返回值允许自然回绕。
 */
OsalTimeMs_t osal_time_now_monotonic(void)
{
    return (OsalTimeMs_t)osal_ticks_to_ms32(osal_tick_count());
}

/**
 * @brief 线程休眠指定毫秒
 */
OsalStatus_t osal_sleep_ms(OsalTimeMs_t sleep_ms)
{
    if (osal_is_in_isr())
        return OSAL_INVALID;
    if (sleep_ms == OSAL_WAIT_FOREVER)
        return OSAL_INVALID;

    OSAL_ASSERT_IN_TASK();
    vTaskDelay(osal_ms_to_ticks(sleep_ms));
    return OSAL_OK;
}

/**
 * @brief 计算已错过周期数（用于过期追赶观测）
 * @note 仅在回绕安全窗口（< 2^31 ms）内有定义。
 */
static uint32_t osal_compute_missed_periods(OsalTimeMs_t now_ms, OsalTimeMs_t deadline_ms, OsalTimeMs_t period_ms)
{
    if (!osal_time_after(now_ms, deadline_ms))
        return 0u;

    uint32_t overdue_ms = (uint32_t)(now_ms - deadline_ms);
    uint64_t missed = (overdue_ms / (uint64_t)period_ms) + 1ULL;
    if (missed > 0xFFFFFFFFULL)
        return 0xFFFFFFFFu;
    return (uint32_t)missed;
}

/**
 * @brief 周期延时（过期追赶：每次仅推进一个周期）
 * @details `deadline_cursor_ms` 为周期游标（in/out）：
 * - 首次传 0，由接口初始化；
 * - 后续调用原样回传，不需要调用方手动计算下一次 deadline。
 */
OsalStatus_t osal_delay_until(OsalTimeMs_t* deadline_cursor_ms, OsalTimeMs_t period_ms, uint32_t* missed_periods)
{
    if (!deadline_cursor_ms || period_ms == 0u)
        return OSAL_INVALID;
    if (osal_is_in_isr())
        return OSAL_INVALID;
    if (missed_periods)
        *missed_periods = 0u;

    OsalTimeMs_t now_ms = osal_time_now_monotonic();

    if (*deadline_cursor_ms == 0u)
    {
        *deadline_cursor_ms = (OsalTimeMs_t)(now_ms + period_ms);
        return OSAL_OK;
    }

#if (INCLUDE_xTaskDelayUntil == 1)
    /**
     * @brief 主路径：使用 FreeRTOS 官方绝对周期接口
     * @details
     * - xTaskDelayUntil 的输入是“上一次唤醒时刻”。
     * - OSAL 维护的是“下一次截止时刻”，因此先换算得到 previous_wake_tick。
     */
    TickType_t period_ticks = osal_ms_to_ticks(period_ms);
    TickType_t deadline_tick = (TickType_t)pdMS_TO_TICKS(*deadline_cursor_ms);
    TickType_t previous_wake_tick = (TickType_t)(deadline_tick - period_ticks);
    BaseType_t was_delayed = pdFALSE;

    if (period_ticks == (TickType_t)0u)
        return OSAL_INVALID;

    was_delayed = xTaskDelayUntil(&previous_wake_tick, period_ticks);

    if (missed_periods && (was_delayed == pdFALSE) && osal_time_before(*deadline_cursor_ms, now_ms))
        *missed_periods = osal_compute_missed_periods(now_ms, *deadline_cursor_ms, period_ms);
#else
    /**
     * @brief 回退路径：当 xTaskDelayUntil 不可用时，保持原有语义
     */
    if (osal_time_after(*deadline_cursor_ms, now_ms))
    {
        OsalTimeMs_t sleep_delta_ms = (OsalTimeMs_t)(*deadline_cursor_ms - now_ms);
        OsalStatus_t status = osal_sleep_ms(sleep_delta_ms);
        if (status != OSAL_OK)
            return status;
    }
    else if (osal_time_before(*deadline_cursor_ms, now_ms))
    {
        if (missed_periods)
            *missed_periods = osal_compute_missed_periods(now_ms, *deadline_cursor_ms, period_ms);
    }
#endif

    /* 无论是否过期，始终只推进一个周期。 */
    *deadline_cursor_ms = (OsalTimeMs_t)(*deadline_cursor_ms + period_ms);
    return OSAL_OK;
}

#endif /* OM_OSAL_PORT == OSAL_PORT_FREERTOS */


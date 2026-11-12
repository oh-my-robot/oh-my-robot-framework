/**
 * @file   osal_time_none.c
 * @brief  osal-none 端口时间面：单调毫秒 / 忙等睡眠 / 周期延时
 *
 * - now = arch 注入的自由运行计数毫秒（A2：CPU 时钟域；冻结即暂停；
 *   平台先启时钟契约——任何时间调用前时基已运行）。
 * - sleep = 忙等至截止时刻，期间内嵌刷新软件定时器。
 * - delay_until 语义对齐 freertos 端口回退路径（周期游标 + 过期统计 +
 *   每次推进一个周期）。
 */

#include "osal/osal_time.h"

#include "osal_none_internal.h"

OsalTimeMs osal_time_now_monotonic(void)
{
    return osal_none_time_now_ms();
}

OsalStatus osal_sleep_ms(OsalTimeMs sleep_ms)
{
    OsalTimeMs wake;
    OsalStatus status;

    status = osal_none_check_blocking_ctx();
    if (status != OSAL_OK)
    {
        return status;
    }
    if (sleep_ms == OSAL_WAIT_FOREVER)
    {
        return OSAL_INVALID; /* 无限睡眠非法（契约） */
    }
    if (sleep_ms == 0u)
    {
        return OSAL_OK;
    }
    if (sleep_ms > 0x7FFFFFFFu)
    {
        sleep_ms = 0x7FFFFFFFu; /* 合同比较窗口饱和 */
    }

    wake = osal_none_time_now_ms() + sleep_ms;
    while (!osal_time_after(osal_none_time_now_ms(), wake))
    {
        osal_none_timer_refresh();
        osal_none_arch_wait_pause();
    }
    return OSAL_OK;
}

/* 已错过周期数（过期追赶观测；窗口 < 2^31 ms 内有定义） */
static uint32_t osal_none_compute_missed_periods(OsalTimeMs now_ms, OsalTimeMs deadline_ms, OsalTimeMs period_ms)
{
    uint32_t overdue_ms;
    uint64_t missed;

    if (!osal_time_after(now_ms, deadline_ms))
    {
        return 0u;
    }
    overdue_ms = (uint32_t)(now_ms - deadline_ms);
    missed = ((uint64_t)overdue_ms / period_ms) + 1ULL;
    if (missed > 0xFFFFFFFFULL)
    {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)missed;
}

OsalStatus osal_delay_until(OsalTimeMs *deadline_cursor_ms, OsalTimeMs period_ms, uint32_t *missed_periods)
{
    OsalTimeMs now_ms;
    OsalStatus status;

    if (!deadline_cursor_ms || period_ms == 0u)
    {
        return OSAL_INVALID;
    }
    status = osal_none_check_blocking_ctx();
    if (status != OSAL_OK)
    {
        return status;
    }
    if (missed_periods)
    {
        *missed_periods = 0u;
    }

    now_ms = osal_time_now_monotonic();

    if (*deadline_cursor_ms == 0u)
    {
        /* 首次调用：初始化游标，立即返回 */
        *deadline_cursor_ms = (OsalTimeMs)(now_ms + period_ms);
        return OSAL_OK;
    }

    if (osal_time_after(*deadline_cursor_ms, now_ms))
    {
        OsalTimeMs sleep_delta_ms = (OsalTimeMs)(*deadline_cursor_ms - now_ms);
        status = osal_sleep_ms(sleep_delta_ms);
        if (status != OSAL_OK)
        {
            return status;
        }
    }
    else if (osal_time_before(*deadline_cursor_ms, now_ms))
    {
        if (missed_periods)
        {
            *missed_periods = osal_none_compute_missed_periods(now_ms, *deadline_cursor_ms, period_ms);
        }
    }

    /* 无论是否过期，始终只推进一个周期 */
    *deadline_cursor_ms = (OsalTimeMs)(*deadline_cursor_ms + period_ms);
    return OSAL_OK;
}

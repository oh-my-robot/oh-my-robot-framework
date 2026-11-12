/**
 * @file   osal_timer_none.c
 * @brief  osal-none 端口软件定时器：到期链 + 等待点内嵌刷新
 *
 * 执行模型：定时器到期由 osal_none_timer_refresh() 驱动——该函数在
 * 每次等待/sleep 循环内嵌调用（单流下无其它执行点；主流程纯计算不等待
 * 时定时器不推进——文档语义）。回调在任务上下文执行、不得阻塞
 * （阻塞类调用经 osal_none_check_blocking_ctx 拒绝）。
 *
 * 控制操作（start/stop/reset/delete）为直接操作、即时生效，timeout_ms
 * 参数保留兼容但无等待对象（无命令队列——与 daemon 架构端口的差异：
 * 无 WOULD_BLOCK/TIMEOUT 来源）。
 *
 * 回调内控制语义：周期定时器触发后自动重挂；回调内 stop 自身 = 取消
 * 后续周期；delete 自身 = 终止；start/reset 自身 = 立即重装。
 */

#include "osal/osal_timer.h"

#include "osal_none_internal.h"

struct OsalTimerHandle_s
{
    bool valid;
    bool active;   /* 在到期链上 */
    bool firing;   /* 回调执行中（stop 自身 → 抑制重挂） */
    bool suppress; /* 回调内 stop 置位：周期定时器不再重挂 */
    OsalTimerMode mode;
    OsalTimeMs period;
    OsalTimeMs deadline;
    OsalTimerCallback cb;
    void *user_id;
    struct OsalTimerHandle_s *next;
};

static struct OsalTimerHandle_s *s_list; /* 按 deadline 升序的单链 */
static bool s_cb_active;

bool osal_none_timer_cb_active(void)
{
    return s_cb_active;
}

static void osal_none_timer_list_insert(OsalTimer *timer)
{
    struct OsalTimerHandle_s **pp = &s_list;

    while (*pp && !osal_time_after(timer->deadline, (*pp)->deadline))
    {
        pp = &(*pp)->next;
    }
    timer->next = *pp;
    *pp = timer;
}

static void osal_none_timer_list_remove(OsalTimer *timer)
{
    struct OsalTimerHandle_s **pp = &s_list;

    while (*pp && *pp != timer)
    {
        pp = &(*pp)->next;
    }
    if (*pp == timer)
    {
        *pp = timer->next;
    }
    timer->next = NULL;
}

void osal_none_timer_refresh(void)
{
    OsalTimeMs now = osal_none_time_now_ms();

    while (s_list && !osal_time_after(s_list->deadline, now))
    {
        OsalTimer *timer = s_list;
        OsalTimerCallback cb;
        OsalTimerMode mode;
        OsalTimeMs period;

        s_list = timer->next;
        timer->next = NULL;
        timer->active = false;
        timer->firing = true;
        timer->suppress = false;

        /* 快照后执行回调：回调内 stop/delete/start 经对象字段生效 */
        cb = timer->cb;
        mode = timer->mode;
        period = timer->period;
        if (cb)
        {
            s_cb_active = true;
            cb(timer);
            s_cb_active = false;
        }
        timer->firing = false;

        /* 周期重挂：仅当未被 delete（valid/cb）且未被回调 stop（suppress）
         * 或 start/reset（active 已为真）时 */
        if (mode == OSAL_TIMER_PERIODIC && timer->valid && timer->cb && !timer->suppress && !timer->active)
        {
            timer->deadline += period;
            if (!osal_time_after(timer->deadline, now))
            {
                timer->deadline = now + period; /* 防积压连发 */
            }
            timer->active = true;
            osal_none_timer_list_insert(timer);
        }
    }
}

static OsalTimer *osal_none_timer_alloc(void)
{
    OsalTimer *timer;

    timer = (OsalTimer *)osal_malloc(sizeof(struct OsalTimerHandle_s));
    if (!timer)
    {
        return NULL;
    }
    timer->valid = false;
    timer->active = false;
    timer->firing = false;
    timer->suppress = false;
    timer->cb = NULL;
    timer->user_id = NULL;
    timer->next = NULL;
    return timer;
}

OsalStatus osal_timer_create(OsalTimer **out_timer, const char *name, uint32_t period_ms, OsalTimerMode mode,
                             void *user_id, OsalTimerCallback cb)
{
    OsalTimer *timer;

    (void)name; /* 单执行流无线程名载体，忽略 */
    if (!out_timer || !cb || period_ms == 0u)
    {
        return OSAL_INVALID;
    }
    if (mode != OSAL_TIMER_ONE_SHOT && mode != OSAL_TIMER_PERIODIC)
    {
        return OSAL_INVALID;
    }
    if (osal_is_in_isr())
    {
        return OSAL_INVALID;
    }

    timer = osal_none_timer_alloc();
    if (!timer)
    {
        return OSAL_NO_RESOURCE;
    }
    timer->valid = true;
    timer->mode = mode;
    timer->period = period_ms;
    timer->cb = cb;
    timer->user_id = user_id;
    *out_timer = timer;
    return OSAL_OK;
}

OsalStatus osal_timer_start(OsalTimer *timer, uint32_t timeout_ms)
{
    (void)timeout_ms; /* 直接操作即时生效：无命令队列可等待 */

    if (!timer || osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    if (!timer->valid)
    {
        return OSAL_INVALID;
    }

    if (timer->active)
    {
        osal_none_timer_list_remove(timer);
        timer->active = false;
    }
    timer->deadline = osal_none_time_now_ms() + timer->period;
    timer->active = true;
    timer->suppress = false;
    osal_none_timer_list_insert(timer);
    return OSAL_OK;
}

OsalStatus osal_timer_stop(OsalTimer *timer, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (!timer || osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    if (!timer->valid)
    {
        return OSAL_INVALID;
    }

    if (timer->active)
    {
        osal_none_timer_list_remove(timer);
        timer->active = false;
    }
    else if (timer->firing)
    {
        timer->suppress = true; /* 回调内 stop 自身：取消周期重挂 */
    }
    return OSAL_OK;
}

OsalStatus osal_timer_reset(OsalTimer *timer, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (!timer || osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    if (!timer->valid)
    {
        return OSAL_INVALID;
    }

    /* 重装语义：以当前时刻重新计时；未运行时等价 start */
    if (timer->active)
    {
        osal_none_timer_list_remove(timer);
        timer->active = false;
    }
    timer->deadline = osal_none_time_now_ms() + timer->period;
    timer->active = true;
    timer->suppress = false;
    osal_none_timer_list_insert(timer);
    return OSAL_OK;
}

OsalStatus osal_timer_delete(OsalTimer *timer, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (!timer || osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    if (!timer->valid)
    {
        return OSAL_INVALID;
    }

    if (timer->active)
    {
        osal_none_timer_list_remove(timer);
        timer->active = false;
    }
    timer->valid = false;
    timer->cb = NULL; /* 刷新路径据此不再重挂/回调 */
    return OSAL_OK;
}

void *osal_timer_get_id(OsalTimer *timer)
{
    if (!timer || osal_is_in_isr())
    {
        return NULL;
    }
    if (!timer->valid)
    {
        return NULL;
    }
    return timer->user_id;
}

void osal_timer_set_id(OsalTimer *timer, void *id)
{
    if (!timer || osal_is_in_isr())
    {
        return;
    }
    if (!timer->valid)
    {
        return;
    }
    timer->user_id = id;
}

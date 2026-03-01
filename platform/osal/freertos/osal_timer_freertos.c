#include "osal/osal_timer.h"

#include "FreeRTOS.h"
#include "osal_time_freertos.h"
#include "timers.h"

static inline TimerHandle_t osal_timer_to_native(OsalTimer_t timer)
{
    return (TimerHandle_t)timer;
}

static inline OsalTimer_t osal_timer_from_native(TimerHandle_t timer)
{
    return (OsalTimer_t)timer;
}

static int osal_timer_check_task_context(void)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr == 0);
    return (in_isr == 0);
}

static OsalStatus_t osal_timer_queue_cmd_result_to_status(BaseType_t ok, uint32_t timeout_ms)
{
    if (ok == pdPASS)
        return OSAL_OK;
    if (timeout_ms == 0u)
        return OSAL_WOULD_BLOCK;
    return OSAL_TIMEOUT;
}

OsalStatus_t osal_timer_create(OsalTimer_t* out_timer, const char* name, uint32_t period_ms, OsalTimerMode_e mode,
                                void* user_id, OsalTimerCallback_t cb)
{
    TimerHandle_t handle;
    BaseType_t auto_reload;

    if (!out_timer || !cb || period_ms == 0u)
        return OSAL_INVALID;
    if (!osal_timer_check_task_context())
        return OSAL_INVALID;
    if ((mode != OSAL_TIMER_ONE_SHOT) && (mode != OSAL_TIMER_PERIODIC))
        return OSAL_INVALID;

    *out_timer = NULL;
    auto_reload = (mode == OSAL_TIMER_PERIODIC) ? pdTRUE : pdFALSE;
    handle = xTimerCreate(name, osal_ms_to_ticks(period_ms), auto_reload, user_id, (TimerCallbackFunction_t)cb);
    if (!handle)
        return OSAL_NO_RESOURCE;

    *out_timer = osal_timer_from_native(handle);
    return OSAL_OK;
}

OsalStatus_t osal_timer_start(OsalTimer_t timer, uint32_t timeout_ms)
{
    if (!timer)
        return OSAL_INVALID;
    if (!osal_timer_check_task_context())
        return OSAL_INVALID;

    BaseType_t ok = xTimerStart(osal_timer_to_native(timer), osal_ms_to_ticks(timeout_ms));
    return osal_timer_queue_cmd_result_to_status(ok, timeout_ms);
}

OsalStatus_t osal_timer_stop(OsalTimer_t timer, uint32_t timeout_ms)
{
    if (!timer)
        return OSAL_INVALID;
    if (!osal_timer_check_task_context())
        return OSAL_INVALID;

    BaseType_t ok = xTimerStop(osal_timer_to_native(timer), osal_ms_to_ticks(timeout_ms));
    return osal_timer_queue_cmd_result_to_status(ok, timeout_ms);
}

OsalStatus_t osal_timer_reset(OsalTimer_t timer, uint32_t timeout_ms)
{
    if (!timer)
        return OSAL_INVALID;
    if (!osal_timer_check_task_context())
        return OSAL_INVALID;

    BaseType_t ok = xTimerReset(osal_timer_to_native(timer), osal_ms_to_ticks(timeout_ms));
    return osal_timer_queue_cmd_result_to_status(ok, timeout_ms);
}

OsalStatus_t osal_timer_delete(OsalTimer_t timer, uint32_t timeout_ms)
{
    if (!timer)
        return OSAL_INVALID;
    if (!osal_timer_check_task_context())
        return OSAL_INVALID;

    BaseType_t ok = xTimerDelete(osal_timer_to_native(timer), osal_ms_to_ticks(timeout_ms));
    return osal_timer_queue_cmd_result_to_status(ok, timeout_ms);
}

void* osal_timer_get_id(OsalTimer_t timer)
{
    if (!timer)
        return NULL;
    if (!osal_timer_check_task_context())
        return NULL;

    return pvTimerGetTimerID(osal_timer_to_native(timer));
}

void osal_timer_set_id(OsalTimer_t timer, void* id)
{
    if (!timer)
        return;
    if (!osal_timer_check_task_context())
        return;

    vTimerSetTimerID(osal_timer_to_native(timer), id);
}


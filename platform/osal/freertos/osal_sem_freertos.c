#include "osal/osal_sem.h"

#include "FreeRTOS.h"
#include "osal_time_freertos.h"
#include "semphr.h"

static inline SemaphoreHandle_t osal_sem_to_native(OsalSem* sem)
{
    return (SemaphoreHandle_t)sem;
}

static inline OsalSem* osal_sem_from_native(SemaphoreHandle_t sem)
{
    return (OsalSem*)sem;
}

static int osal_sem_check_task_context(void)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr == 0);
    return (in_isr == 0);
}

static int osal_sem_check_isr_context(void)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr != 0);
    return (in_isr != 0);
}

static int osal_sem_u32_to_ubase(uint32_t value, UBaseType_t* out_value)
{
    if (!out_value)
        return 0;
    if ((uint32_t)((UBaseType_t)value) != value)
        return 0;

    *out_value = (UBaseType_t)value;
    return 1;
}

OsalStatus osal_sem_create(OsalSem** sem, uint32_t max_count, uint32_t init_count)
{
    UBaseType_t max_count_u = 0u;
    UBaseType_t init_count_u = 0u;
    SemaphoreHandle_t handle = NULL;

    if (!sem || max_count == 0u || init_count > max_count)
        return OSAL_INVALID;
    if (!osal_sem_check_task_context())
        return OSAL_INVALID;
    if (!osal_sem_u32_to_ubase(max_count, &max_count_u))
        return OSAL_INVALID;
    if (!osal_sem_u32_to_ubase(init_count, &init_count_u))
        return OSAL_INVALID;

    *sem = NULL;
    handle = xSemaphoreCreateCounting(max_count_u, init_count_u);
    if (!handle)
        return OSAL_NO_RESOURCE;

    *sem = osal_sem_from_native(handle);
    return OSAL_OK;
}

OsalStatus osal_sem_delete(OsalSem* sem)
{
    if (!sem)
        return OSAL_INVALID;
    if (!osal_sem_check_task_context())
        return OSAL_INVALID;

    vSemaphoreDelete(osal_sem_to_native(sem));
    return OSAL_OK;
}

OsalStatus osal_sem_wait(OsalSem* sem, uint32_t timeout_ms)
{
    if (!sem)
        return OSAL_INVALID;
    if (!osal_sem_check_task_context())
        return OSAL_INVALID;

    OSAL_ASSERT_IN_TASK();
    TickType_t ticks = osal_ms_to_ticks(timeout_ms);
    BaseType_t ok = xSemaphoreTake(osal_sem_to_native(sem), ticks);
    return osal_wait_result_to_status(ok, timeout_ms);
}

OsalStatus osal_sem_post(OsalSem* sem)
{
    if (!sem)
        return OSAL_INVALID;
    if (!osal_sem_check_task_context())
        return OSAL_INVALID;

    OSAL_ASSERT_IN_TASK();
    return (xSemaphoreGive(osal_sem_to_native(sem)) == pdPASS) ? OSAL_OK : OSAL_NO_RESOURCE;
}

OsalStatus osal_sem_post_from_isr(OsalSem* sem)
{
    if (!sem)
        return OSAL_INVALID;
    if (!osal_sem_check_isr_context())
        return OSAL_INVALID;

    BaseType_t need_switch = pdFALSE;
    BaseType_t ok = xSemaphoreGiveFromISR(osal_sem_to_native(sem), &need_switch);
    if (need_switch)
        portYIELD_FROM_ISR(need_switch);
    return (ok == pdPASS) ? OSAL_OK : OSAL_NO_RESOURCE;
}

OsalStatus osal_sem_get_count(OsalSem* sem, uint32_t* out_count)
{
    if (!sem || !out_count)
        return OSAL_INVALID;
    if (!osal_sem_check_task_context())
        return OSAL_INVALID;

    *out_count = (uint32_t)uxSemaphoreGetCount(osal_sem_to_native(sem));
    return OSAL_OK;
}

OsalStatus osal_sem_get_count_from_isr(OsalSem* sem, uint32_t* out_count)
{
    if (!sem || !out_count)
        return OSAL_INVALID;
    if (!osal_sem_check_isr_context())
        return OSAL_INVALID;

    *out_count = (uint32_t)uxSemaphoreGetCountFromISR(osal_sem_to_native(sem));
    return OSAL_OK;
}


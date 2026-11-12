#include "osal/osal_mutex.h"

#include "FreeRTOS.h"
#include "osal_time_freertos.h"
#include "semphr.h"
#include "task.h"

static inline SemaphoreHandle_t osal_mutex_to_native(OsalMutex* mutex)
{
    return (SemaphoreHandle_t)mutex;
}

static inline OsalMutex* osal_mutex_from_native(SemaphoreHandle_t mutex)
{
    return (OsalMutex*)mutex;
}

static int osal_mutex_check_task_context(void)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr == 0);
    return (in_isr == 0);
}

static int osal_mutex_is_current_owner(OsalMutex* mutex)
{
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    TaskHandle_t owner_task = xSemaphoreGetMutexHolder(osal_mutex_to_native(mutex));
    return (owner_task == current_task);
}

OsalStatus osal_mutex_create(OsalMutex** mutex)
{
    if (!mutex)
        return OSAL_INVALID;
    if (!osal_mutex_check_task_context())
        return OSAL_INVALID;

    *mutex = osal_mutex_from_native(xSemaphoreCreateMutex());
    return (*mutex) ? OSAL_OK : OSAL_NO_RESOURCE;
}

OsalStatus osal_mutex_delete(OsalMutex* mutex)
{
    if (!mutex)
        return OSAL_INVALID;
    if (!osal_mutex_check_task_context())
        return OSAL_INVALID;

    vSemaphoreDelete(osal_mutex_to_native(mutex));
    return OSAL_OK;
}

OsalStatus osal_mutex_lock(OsalMutex* mutex, uint32_t timeout_ms)
{
    if (!mutex)
        return OSAL_INVALID;
    if (!osal_mutex_check_task_context())
        return OSAL_INVALID;

    OSAL_ASSERT_IN_TASK();
    TickType_t ticks = osal_ms_to_ticks(timeout_ms);
    BaseType_t ok = xSemaphoreTake(osal_mutex_to_native(mutex), ticks);
    return osal_wait_result_to_status(ok, timeout_ms);
}

OsalStatus osal_mutex_unlock(OsalMutex* mutex)
{
    if (!mutex)
        return OSAL_INVALID;
    if (!osal_mutex_check_task_context())
        return OSAL_INVALID;

    OSAL_ASSERT_IN_TASK();
    if (!osal_mutex_is_current_owner(mutex))
        return OSAL_INVALID;

    return (xSemaphoreGive(osal_mutex_to_native(mutex)) == pdPASS) ? OSAL_OK : OSAL_INVALID;
}


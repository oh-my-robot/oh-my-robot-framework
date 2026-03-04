#include "osal/osal_queue.h"

#include "FreeRTOS.h"
#include "osal_time_freertos.h"
#include "queue.h"

static inline QueueHandle_t osal_queue_to_native(OsalQueue* queue)
{
    return (QueueHandle_t)queue;
}

static inline OsalQueue* osal_queue_from_native(QueueHandle_t queue)
{
    return (OsalQueue*)queue;
}

static inline int osal_queue_check_task_context(void)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr == 0);
    return (in_isr == 0);
}

static inline int osal_queue_check_isr_context(void)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr != 0);
    return (in_isr != 0);
}

static inline OsalStatus osal_queue_from_isr_result_to_status(BaseType_t result)
{
    return (result == pdPASS) ? OSAL_OK : OSAL_WOULD_BLOCK;
}

OsalStatus osal_queue_create(OsalQueue** queue, uint32_t length, uint32_t item_size)
{
    if (!queue || length == 0u || item_size == 0u)
        return OSAL_INVALID;
    if (!osal_queue_check_task_context())
        return OSAL_INVALID;

    QueueHandle_t handle = xQueueCreate((UBaseType_t)length, (UBaseType_t)item_size);
    if (!handle)
        return OSAL_NO_RESOURCE;

    *queue = osal_queue_from_native(handle);
    return OSAL_OK;
}

OsalStatus osal_queue_delete(OsalQueue* queue)
{
    if (!queue)
        return OSAL_INVALID;
    if (!osal_queue_check_task_context())
        return OSAL_INVALID;

    vQueueDelete(osal_queue_to_native(queue));
    return OSAL_OK;
}

OsalStatus osal_queue_send(OsalQueue* queue, const void* item, uint32_t timeout_ms)
{
    if (!queue || !item)
        return OSAL_INVALID;
    if (!osal_queue_check_task_context())
        return OSAL_INVALID;

    OSAL_ASSERT_IN_TASK();
    TickType_t ticks = osal_ms_to_ticks(timeout_ms);
    BaseType_t ok = xQueueSend(osal_queue_to_native(queue), item, ticks);
    return osal_wait_result_to_status(ok, timeout_ms);
}

OsalStatus osal_queue_send_from_isr(OsalQueue* queue, const void* item)
{
    if (!queue || !item)
        return OSAL_INVALID;
    if (!osal_queue_check_isr_context())
        return OSAL_INVALID;

    BaseType_t need_switch = pdFALSE;
    BaseType_t ok = xQueueSendFromISR(osal_queue_to_native(queue), item, &need_switch);
    if (need_switch == pdTRUE)
        portYIELD_FROM_ISR(need_switch);

    return osal_queue_from_isr_result_to_status(ok);
}

OsalStatus osal_queue_recv(OsalQueue* queue, void* item, uint32_t timeout_ms)
{
    if (!queue || !item)
        return OSAL_INVALID;
    if (!osal_queue_check_task_context())
        return OSAL_INVALID;

    OSAL_ASSERT_IN_TASK();
    TickType_t ticks = osal_ms_to_ticks(timeout_ms);
    BaseType_t ok = xQueueReceive(osal_queue_to_native(queue), item, ticks);
    return osal_wait_result_to_status(ok, timeout_ms);
}

OsalStatus osal_queue_recv_from_isr(OsalQueue* queue, void* item)
{
    if (!queue || !item)
        return OSAL_INVALID;
    if (!osal_queue_check_isr_context())
        return OSAL_INVALID;

    BaseType_t need_switch = pdFALSE;
    BaseType_t ok = xQueueReceiveFromISR(osal_queue_to_native(queue), item, &need_switch);
    if (need_switch == pdTRUE)
        portYIELD_FROM_ISR(need_switch);

    return osal_queue_from_isr_result_to_status(ok);
}

OsalStatus osal_queue_peek(OsalQueue* queue, void* item, uint32_t timeout_ms)
{
    if (!queue || !item)
        return OSAL_INVALID;
    if (!osal_queue_check_task_context())
        return OSAL_INVALID;

    OSAL_ASSERT_IN_TASK();
    TickType_t ticks = osal_ms_to_ticks(timeout_ms);
    BaseType_t ok = xQueuePeek(osal_queue_to_native(queue), item, ticks);
    return osal_wait_result_to_status(ok, timeout_ms);
}

OsalStatus osal_queue_peek_from_isr(OsalQueue* queue, void* item)
{
    if (!queue || !item)
        return OSAL_INVALID;
    if (!osal_queue_check_isr_context())
        return OSAL_INVALID;

    BaseType_t ok = xQueuePeekFromISR(osal_queue_to_native(queue), item);
    return osal_queue_from_isr_result_to_status(ok);
}

OsalStatus osal_queue_reset(OsalQueue* queue)
{
    if (!queue)
        return OSAL_INVALID;
    if (!osal_queue_check_task_context())
        return OSAL_INVALID;

    return (xQueueReset(osal_queue_to_native(queue)) == pdPASS) ? OSAL_OK : OSAL_INTERNAL;
}

OsalStatus osal_queue_messages_waiting(OsalQueue* queue, uint32_t* out_count)
{
    if (!queue || !out_count)
        return OSAL_INVALID;

    if (osal_is_in_isr())
    {
        *out_count = (uint32_t)uxQueueMessagesWaitingFromISR(osal_queue_to_native(queue));
        return OSAL_OK;
    }

    *out_count = (uint32_t)uxQueueMessagesWaiting(osal_queue_to_native(queue));
    return OSAL_OK;
}

OsalStatus osal_queue_spaces_available(OsalQueue* queue, uint32_t* out_count)
{
    if (!queue || !out_count)
        return OSAL_INVALID;

    if (osal_is_in_isr())
    {
        UBaseType_t queue_length = uxQueueGetQueueLength(osal_queue_to_native(queue));
        UBaseType_t waiting = uxQueueMessagesWaitingFromISR(osal_queue_to_native(queue));
        if (waiting > queue_length)
        {
            OSAL_ASSERT(waiting <= queue_length);
            return OSAL_INTERNAL;
        }

        *out_count = (uint32_t)(queue_length - waiting);
        return OSAL_OK;
    }

    *out_count = (uint32_t)uxQueueSpacesAvailable(osal_queue_to_native(queue));
    return OSAL_OK;
}


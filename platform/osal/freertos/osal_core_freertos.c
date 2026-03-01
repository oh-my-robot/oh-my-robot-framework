#include "osal/osal_core.h"

#include "FreeRTOS.h"
#include "portable.h"
#include "task.h"

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define OSAL_STATIC_ASSERT(cond, msg) _Static_assert((cond), #msg)
#else
#define OSAL_STATIC_ASSERT(cond, msg) typedef char static_assert_##msg[(cond) ? 1 : -1]
#endif

OSAL_STATIC_ASSERT((OSAL_PRIORITY_MAX == configMAX_PRIORITIES), osal_priority_max_mismatch);
OSAL_STATIC_ASSERT((OSAL_TASK_NAME_MAX == configMAX_TASK_NAME_LEN), osal_task_name_max_mismatch);
OSAL_STATIC_ASSERT((OSAL_STACK_WORD_BYTES == sizeof(StackType_t)), osal_stack_word_bytes_mismatch);
OSAL_STATIC_ASSERT((OSAL_WAIT_FOREVER == portMAX_DELAY), osal_wait_forever_mismatch);

int osal_is_in_isr(void)
{
    return (xPortIsInsideInterrupt() == pdTRUE) ? 1 : 0;
}

void osal_irq_lock_task(void)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr == 0);
    if (in_isr)
        return;

    taskENTER_CRITICAL();
}

void osal_irq_unlock_task(void)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr == 0);
    if (in_isr)
        return;

    taskEXIT_CRITICAL();
}

OsalIrqIsrState_t osal_irq_lock_from_isr(void)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr != 0);
    if (!in_isr)
        return (OsalIrqIsrState_t)0u;

    return (OsalIrqIsrState_t)taskENTER_CRITICAL_FROM_ISR();
}

void osal_irq_unlock_from_isr(OsalIrqIsrState_t state)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr != 0);
    if (!in_isr)
        return;

    taskEXIT_CRITICAL_FROM_ISR((UBaseType_t)state);
}

void* osal_malloc(size_t size)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr == 0);
    if (in_isr)
        return NULL;

    return pvPortMalloc(size);
}

void osal_free(void* ptr)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr == 0);
    if (in_isr)
        return;

    vPortFree(ptr);
}


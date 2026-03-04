#include "osal/osal_thread.h"

#include "FreeRTOS.h"
#include "task.h"

#define OSAL_THREAD_DEFAULT_STACK_DEPTH (512u)
#define OSAL_THREAD_DEFAULT_PRIORITY (1u)

static inline TaskHandle_t osal_thread_to_native(OsalThread* thread)
{
    return (TaskHandle_t)thread;
}

static inline OsalThread* osal_thread_from_native(TaskHandle_t thread)
{
    return (OsalThread*)thread;
}

static int osal_thread_check_task_context(void)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr == 0);
    return (in_isr == 0);
}

static uint32_t osal_thread_default_stack_size_bytes(void)
{
    return (uint32_t)(OSAL_THREAD_DEFAULT_STACK_DEPTH * (uint32_t)sizeof(StackType_t));
}

static int osal_thread_stack_bytes_to_depth(uint32_t stack_size_bytes, configSTACK_DEPTH_TYPE* stack_depth)
{
    uint32_t word_bytes;
    uint32_t rounded_size_bytes;
    uint32_t stack_depth_u32;

    if (!stack_depth || stack_size_bytes == 0u)
        return 0;

    word_bytes = (uint32_t)sizeof(StackType_t);
    rounded_size_bytes = stack_size_bytes + (word_bytes - 1u);
    if (rounded_size_bytes < stack_size_bytes)
        return 0;

    stack_depth_u32 = rounded_size_bytes / word_bytes;
    if (stack_depth_u32 == 0u)
        stack_depth_u32 = 1u;

    if ((uint32_t)((configSTACK_DEPTH_TYPE)stack_depth_u32) != stack_depth_u32)
        return 0;

    *stack_depth = (configSTACK_DEPTH_TYPE)stack_depth_u32;
    return 1;
}

OsalStatus osal_thread_create(OsalThread** thread, const OsalThreadAttr* attr, OsalThreadEntryFunction entry,
                                 void* arg)
{
    configSTACK_DEPTH_TYPE stack_depth = 0;
    TaskHandle_t handle = NULL;
    const char* name = (attr && attr->name) ? attr->name : "osal_thread";
    uint32_t stack_size_bytes = (attr && attr->stackSize) ? attr->stackSize : osal_thread_default_stack_size_bytes();
    uint32_t priority = (attr) ? attr->priority : OSAL_THREAD_DEFAULT_PRIORITY;

    if (!thread || !entry)
        return OSAL_INVALID;
    if (!osal_thread_check_task_context())
        return OSAL_INVALID;
    if (!osal_thread_stack_bytes_to_depth(stack_size_bytes, &stack_depth))
        return OSAL_INVALID;

    *thread = NULL;
    if (xTaskCreate((TaskFunction_t)entry, name, stack_depth, arg, (UBaseType_t)priority, &handle) == pdPASS)
    {
        *thread = osal_thread_from_native(handle);
        return OSAL_OK;
    }

    return OSAL_NO_RESOURCE;
}

OsalThread* osal_thread_self(void)
{
    if (!osal_thread_check_task_context())
        return NULL;

    return osal_thread_from_native(xTaskGetCurrentTaskHandle());
}

OsalStatus osal_thread_join(OsalThread* thread, uint32_t timeout_ms)
{
    if (!thread)
        return OSAL_INVALID;
    if (!osal_thread_check_task_context())
        return OSAL_INVALID;

    (void)timeout_ms;
    return OSAL_NOT_SUPPORTED;
}

void osal_thread_yield(void)
{
    if (!osal_thread_check_task_context())
        return;

    taskYIELD();
}

void osal_thread_exit(void)
{
    if (!osal_thread_check_task_context())
        return;

    vTaskDelete(NULL);
}

OsalStatus osal_thread_terminate(OsalThread* thread)
{
    TaskHandle_t target;
    TaskHandle_t self;

    if (!thread)
        return OSAL_INVALID;
    if (!osal_thread_check_task_context())
        return OSAL_INVALID;

    target = osal_thread_to_native(thread);
    self = xTaskGetCurrentTaskHandle();
    if (target == self)
        return OSAL_INVALID;

    vTaskDelete(target);
    return OSAL_OK;
}

OsalStatus osal_kernel_start(void)
{
    if (!osal_thread_check_task_context())
        return OSAL_INVALID;

    vTaskStartScheduler();
    return OSAL_INTERNAL;
}


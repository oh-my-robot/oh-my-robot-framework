/**
 * @file   osal_thread_none.c
 * @brief  osal-none 端口线程面：直调占位（LibXR None 同款语义）
 *
 * 单执行流无任务调度、无上下文切换——本族全部函数为占位语义：
 * - create：当场直调 entry(arg)（无后台线程）；entry 返回后 *thread 指向
 *   唯一执行体单例并返回 OSAL_OK。entry 为死循环时本调用不返回
 *   （调用者后续流程被吞——文档语义，非缺陷）。
 * - self：唯一执行体单例（mutex owner 等身份语义的锚点）。
 * - exit：fail-loud 死循环（单流无"退出当前执行体并继续"的载体，禁调）。
 * - terminate/join：无其它执行体 / 对齐 freertos 端口。
 * - kernel_start：OSAL_OK no-op（单执行流无"启动调度器"动作）。
 */

#include "osal/osal_thread.h"

#include "osal_none_internal.h"

struct OsalThreadHandle_s
{
    uint32_t magic;
};

static struct OsalThreadHandle_s s_executor = {0x4F4E4555u}; /* "ONEU" */

OsalThread *osal_none_executor_handle(void)
{
    return (OsalThread *)&s_executor;
}

OsalStatus osal_thread_create(OsalThread **thread, const OsalThreadAttr *attr, OsalThreadEntryFunction entry,
                              void *arg)
{
    (void)attr; /* 直调占位：name/stackSize/priority 无意义 */

    if (!thread || !entry)
    {
        return OSAL_INVALID;
    }
    if (osal_is_in_isr())
    {
        return OSAL_INVALID;
    }

    /* 直调占位：无后台执行者——当场执行 entry。entry 死循环则本调用不返回。 */
    entry(arg);

    *thread = osal_none_executor_handle();
    return OSAL_OK;
}

OsalThread *osal_thread_self(void)
{
    if (osal_is_in_isr())
    {
        return NULL; /* 契约：ISR 中误用返回 NULL */
    }
    return osal_none_executor_handle();
}

OsalStatus osal_thread_join(OsalThread *thread, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!thread)
    {
        return OSAL_INVALID;
    }
    if (osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    return OSAL_NOT_SUPPORTED; /* 对齐 freertos 端口 */
}

void osal_thread_yield(void)
{
    if (osal_is_in_isr())
    {
        return;
    }
    /* no-op：单执行流无其它执行体可让出 */
}

void osal_thread_exit(void)
{
    if (osal_is_in_isr())
    {
        return;
    }
    /* fail-loud：单流下"退出当前线程"无载体（无处返回、无调度器接手）。
     * 语义 = 终止整个执行流——实现为死循环，禁调。 */
    for (;;)
    {
    }
}

OsalStatus osal_thread_terminate(OsalThread *thread)
{
    if (!thread)
    {
        return OSAL_INVALID;
    }
    if (osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    /* 唯一可引用的句柄 = 执行体自身；terminate(self) 非法（契约）——
     * 单流下不存在可被他人终止的执行体。 */
    return OSAL_INVALID;
}

OsalStatus osal_kernel_start(void)
{
    if (osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    /* no-op：单执行流无调度器启动动作——执行体已在运行。 */
    return OSAL_OK;
}

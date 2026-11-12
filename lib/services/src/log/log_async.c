/**
 * @file log_async.c
 * @brief log 异步消费调度器：门铃创建（存入 LogRing 实例）+ 日志线程（等待→抽环→emit）
 * @details 边界：本文件属"谁/何时消费"（调度器）——数据通道能力在 ring.c（LogRing API，
 *          含生产侧门铃 post），实例属主在 core.c。跨文件静态状态为零：唯一共享状态 =
 *          实例字段 LogRing.doorbell——此处创建后存入、生产侧（ring.c）post、本文件
 *          等待循环 wait——同读该实例字段（对象化前提下的安全切分）。
 *          线程入口先 drain 一次：接住启动期滞留（门铃未建时生产无信号可等）。
 *          OM_LOG_ASYNC=0 时本文件编译为空（同步模式现场触发——见 ring.c）。
 */

#include "core/om_config.h"

#if OM_USE_LOG && OM_LOG_ASYNC

#include "core/om_def.h"
#include "core/om_interrupt.h"
#include "log_internal.h"
#include "osal/osal.h" /* 门铃/线程/优先级（本文件仅目标侧编译——无 host 桩需求） */

/** @brief 日志线程入口：先行 drain（启动期滞留——门铃未建时生产无信号可等）→ wait 循环
 *  @param arg LogRing*（经线程参数引用实例——实例属主 core.c） */
static void log_async_thread(void *arg)
{
    LogRing *ring = (LogRing *)arg;
    log_ring_drain(ring); /* 启动期滞留段（生产者早期在门铃未建时入环——无信号可等） */
    for (;;)
    {
        (void)osal_sem_wait(ring->doorbell, OSAL_WAIT_FOREVER);
        log_ring_drain(ring);
    }
}

OmRet log_async_start(LogRing *ring)
{
    if (osal_sem_create(&ring->doorbell, 1U, 0U) != OSAL_OK)
    {
        return OM_ERR_NO_MEM;
    }
    OsalThreadAttr attr = {"log_thread", 0U, OSAL_PRIO_LOW_BASE};
    OsalThread *thread = NULL;
    if (osal_thread_create(&thread, &attr, log_async_thread, ring) != OSAL_OK)
    {
        return OM_ERR_NO_MEM;
    }
    return OM_OK;
}

#endif /* OM_USE_LOG && OM_LOG_ASYNC */

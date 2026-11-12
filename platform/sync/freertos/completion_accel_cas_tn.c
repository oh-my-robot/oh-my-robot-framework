/*
 * completion_accel_cas_tn.c
 * FreeRTOS 加速后端: CAS 状态机 + Task Notification (index 1)
 *
 * 用 CAS 原子操作替代 irq_lock，用 Task Notification 替代 Semaphore。
 * 占用通知 index 1，其他模块不得使用同 index。
 * ISR 路径不使用临界区。
 */
#include "sync/completion.h"

#include "core/atomic/atomic_simple.h"
#include "osal/osal_core.h"
#include "osal/osal_port.h"
#include "osal/osal_thread.h"

#include "FreeRTOS.h"
#include "task.h"

/* TN 后端占用通知 index 1，index 0 留给 FreeRTOS 内置 API */
#if configTASK_NOTIFICATION_ARRAY_ENTRIES < 2
#error "COMPLETION TN backend requires configTASK_NOTIFICATION_ARRAY_ENTRIES >= 2. \
        Set it in FreeRTOSConfig.h."
#endif

static TickType_t cas_tn_timeout_to_ticks(size_t timeout_ms)
{
    if (timeout_ms >= (size_t)0xFFFFFFFFu)
        return portMAX_DELAY;
    if (timeout_ms == 0U)
        return 0;
    return (TickType_t)(timeout_ms * configTICK_RATE_HZ / 1000U);
}

#define COMPLETION_NOTIFY_INDEX 1U

static void cas_tn_drain_notification(void)
{
    ulTaskNotifyTakeIndexed(COMPLETION_NOTIFY_INDEX, pdTRUE, 0);
}

OmRet completion_accel_init(Completion *completion)
{
    if (!completion)
        return OM_ERROR_PARAM;

    completion->sem = NULL;
    completion->waitThread = NULL;
    OM_STORE_REL(&completion->status, COMP_INIT);
    return OM_OK;
}

void completion_accel_deinit(Completion *completion)
{
    if (!completion)
        return;

    completion->sem = NULL;
    completion->waitThread = NULL;
    OM_STORE_REL(&completion->status, COMP_INIT);
}

OmRet completion_accel_wait_one_shot(Completion *completion, size_t timeout_ms)
{
    if (!completion)
        return OM_ERROR_PARAM;
    if (osal_is_in_isr())
        return OM_ERROR_PARAM;

    OsalThread *self = osal_thread_self();
    if (!self)
        return OM_ERROR;

    /* 阶段 1: 尝试消费已完成的 done */
    CompStatus snap = (CompStatus)OM_LOAD_ACQ(&completion->status);

    if (snap == COMP_DONE)
    {
        CompStatus expected = COMP_DONE;
        if (OM_CAS_AR(&completion->status, &expected, COMP_INIT))
        {
            completion->waitThread = NULL;
            cas_tn_drain_notification();
            return OM_OK;
        }
        snap = (CompStatus)OM_LOAD_ACQ(&completion->status);
    }

    if (snap == COMP_WAIT || snap == COMP_WAITING)
        return OM_ERROR_BUSY;

    if (snap != COMP_INIT)
        return OM_ERROR;

    if (timeout_ms == 0U)
        return OM_ERROR_TIMEOUT;

    /* 阶段 2: 注册等待 (INIT → WAIT) */
    completion->waitThread = self;
    OM_FENCE_REL();

    CompStatus expected = COMP_INIT;
    if (!OM_CAS_AR(&completion->status, &expected, COMP_WAIT))
    {
        completion->waitThread = NULL;
        if (expected == COMP_DONE)
        {
            expected = COMP_DONE;
            if (OM_CAS_AR(&completion->status, &expected, COMP_INIT))
            {
                cas_tn_drain_notification();
                return OM_OK;
            }
        }
        return (expected == COMP_WAIT || expected == COMP_WAITING) ? OM_ERROR_BUSY : OM_ERROR;
    }

    /* 阶段 3: WAIT → WAITING */
    expected = COMP_WAIT;
    if (!OM_CAS_AR(&completion->status, &expected, COMP_WAITING))
    {
        completion->waitThread = NULL;
        if (expected == COMP_DONE)
        {
            expected = COMP_DONE;
            OM_CAS_AR(&completion->status, &expected, COMP_INIT);
            cas_tn_drain_notification();
            return OM_OK;
        }
        OM_STORE_REL(&completion->status, COMP_INIT);
        return OM_ERROR;
    }

    /* 阶段 4: 阻塞等待 Task Notification */
    TickType_t ticks = cas_tn_timeout_to_ticks(timeout_ms);
    uint32_t notify_result = ulTaskNotifyTakeIndexed(COMPLETION_NOTIFY_INDEX, pdTRUE, ticks);

    if (notify_result != 0U)
    {
        completion->waitThread = NULL;
        OM_STORE_REL(&completion->status, COMP_INIT);
        return OM_OK;
    }

    /* 超时路径: 清理 WAITING → INIT */
    expected = COMP_WAITING;
    if (!OM_CAS_AR(&completion->status, &expected, COMP_INIT))
    {
        completion->waitThread = NULL;
        if (expected == COMP_DONE)
        {
            OM_STORE_REL(&completion->status, COMP_INIT);
            cas_tn_drain_notification();
            return OM_OK;
        }
        OM_STORE_REL(&completion->status, COMP_INIT);
        return OM_ERROR;
    }

    completion->waitThread = NULL;
    return OM_ERROR_TIMEOUT;
}

OmRet completion_accel_done_one_shot(Completion *completion)
{
    if (!completion)
        return OM_ERROR_PARAM;

    int in_isr = osal_is_in_isr();

    /* 快速路径: INIT → DONE */
    CompStatus expected = COMP_INIT;
    if (OM_CAS_AR(&completion->status, &expected, COMP_DONE))
        return OM_OK;

    if (expected == COMP_DONE)
        return OM_ERROR_BUSY;

    /* 等待路径: WAIT 或 WAITING → DONE */
    if (expected == COMP_WAIT || expected == COMP_WAITING)
    {
        CompStatus prev = expected;
        if (OM_CAS_AR(&completion->status, &prev, COMP_DONE))
        {
            TaskHandle_t wait_task = (TaskHandle_t)completion->waitThread;
            if (wait_task != NULL)
            {
                if (in_isr)
                {
                    BaseType_t higher_priority_woken = pdFALSE;
                    vTaskNotifyGiveIndexedFromISR(wait_task, COMPLETION_NOTIFY_INDEX, &higher_priority_woken);
                    portYIELD_FROM_ISR(higher_priority_woken);
                }
                else
                {
                    (void)xTaskNotifyGiveIndexed(wait_task, COMPLETION_NOTIFY_INDEX);
                }
            }
            return OM_OK;
        }
        if (prev == COMP_DONE)
            return OM_ERROR_BUSY;
        return OM_ERROR;
    }

    return OM_ERROR;
}

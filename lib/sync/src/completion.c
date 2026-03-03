#include "sync/completion.h"

#include "osal/osal_core.h"
#include "osal/osal_port.h"

/*
 * completion 后端选择策略：
 * - reference 后端始终可用（基于 OSAL semaphore）；
 * - 加速后端通过 capability 显式声明；
 * - 当前 FreeRTOS 未声明 completion capability，因此固定回退到 reference 后端。
 */
#if defined(OM_SYNC_ACCEL) && (OM_SYNC_ACCEL == 1) && defined(OM_SYNC_ACCEL_CAP_COMPLETION) && \
    (OM_SYNC_ACCEL_CAP_COMPLETION == 1)
#define OM_COMPLETION_ACCEL_ENABLED 1
#else
#define OM_COMPLETION_ACCEL_ENABLED 0
#endif

static uint32_t completion_timeout_to_osal_ms(size_t timeout_ms)
{
    if (timeout_ms >= (size_t)0xFFFFFFFFu)
        return OSAL_WAIT_FOREVER;
    return (uint32_t)timeout_ms;
}

static OmRet_e completion_wait_status_from_osal(OsalStatus_t wait_status)
{
    if (wait_status == OSAL_OK)
        return OM_OK;
    if (wait_status == OSAL_WOULD_BLOCK || wait_status == OSAL_TIMEOUT)
        return OM_ERROR_TIMEOUT;
    if (wait_status == OSAL_INVALID)
        return OM_ERROR_PARAM;
    return OM_ERROR;
}

static void completion_sem_drain(OsalSem_t sem)
{
    if (!sem)
        return;
    while (osal_sem_wait(sem, 0u) == OSAL_OK)
    {
    }
}

static OmRet_e completion_sem_init(Completion_t completion)
{
    if (!completion)
        return OM_ERROR_PARAM;

    if (!completion->sem)
    {
        if (osal_sem_create(&completion->sem, 1u, 0u) != OSAL_OK)
            return OM_ERROR_MEMORY;
    }

    completion_sem_drain(completion->sem);
    completion->waitThread = NULL;
    completion->status = COMP_INIT;
    return OM_OK;
}

static void completion_sem_deinit(Completion_t completion)
{
    if (!completion)
        return;

    if (completion->sem)
    {
        (void)osal_sem_delete(completion->sem);
        completion->sem = NULL;
    }

    completion->waitThread = NULL;
    completion->status = COMP_INIT;
}

static OmRet_e completion_sem_wait_one_shot(Completion_t completion, size_t timeout_ms)
{
    if (!completion || !completion->sem)
        return OM_ERROR_PARAM;
    if (osal_is_in_isr())
        return OM_ERROR_PARAM;

    OsalThread_t self = osal_thread_self();
    if (!self)
        return OM_ERROR;

    osal_irq_lock_task();
    if (completion->status == COMP_WAIT)
    {
        osal_irq_unlock_task();
        return OM_ERROR_BUSY;
    }
    if (completion->status == COMP_DONE)
    {
        completion->status = COMP_INIT;
        completion->waitThread = NULL;
        osal_irq_unlock_task();
        completion_sem_drain(completion->sem);
        return OM_OK;
    }
    if (timeout_ms == 0U)
    {
        osal_irq_unlock_task();
        return OM_ERROR_TIMEOUT;
    }
    if (completion->waitThread != NULL)
    {
        osal_irq_unlock_task();
        return OM_ERROR_BUSY;
    }

    completion->waitThread = self;
    completion->status = COMP_WAIT;
    osal_irq_unlock_task();

    uint32_t osal_timeout_ms = completion_timeout_to_osal_ms(timeout_ms);
    OsalStatus_t wait_status = osal_sem_wait(completion->sem, osal_timeout_ms);
    OmRet_e wait_ret = completion_wait_status_from_osal(wait_status);

    osal_irq_lock_task();
    if (completion->status != COMP_DONE)
    {
        completion->waitThread = NULL;
        completion->status = COMP_INIT;
        osal_irq_unlock_task();
        return wait_ret;
    }

    completion->waitThread = NULL;
    completion->status = COMP_INIT;
    osal_irq_unlock_task();
    completion_sem_drain(completion->sem);
    return OM_OK;
}

static OmRet_e completion_sem_done_one_shot(Completion_t completion)
{
    if (!completion || !completion->sem)
        return OM_ERROR_PARAM;

    int in_isr = osal_is_in_isr();
    OsalIrqIsrState_t isr_state = 0u;
    OmRet_e ret;

    if (in_isr)
        isr_state = osal_irq_lock_from_isr();
    else
        osal_irq_lock_task();

    switch (completion->status)
    {
    case COMP_INIT:
        completion->status = COMP_DONE;
        if (in_isr)
            (void)osal_sem_post_from_isr(completion->sem);
        else
            (void)osal_sem_post(completion->sem);
        ret = OM_OK;
        break;

    case COMP_WAIT:
        completion->status = COMP_DONE;
        if (in_isr)
            (void)osal_sem_post_from_isr(completion->sem);
        else
            (void)osal_sem_post(completion->sem);
        ret = OM_OK;
        break;

    case COMP_DONE:
        ret = OM_ERROR_BUSY;
        break;

    default:
        ret = OM_ERROR;
        break;
    }

    if (in_isr)
        osal_irq_unlock_from_isr(isr_state);
    else
        osal_irq_unlock_task();
    return ret;
}

#if OM_COMPLETION_ACCEL_ENABLED
extern OmRet_e completion_accel_init(Completion_t completion);
extern void completion_accel_deinit(Completion_t completion);
extern OmRet_e completion_accel_wait_one_shot(Completion_t completion, size_t timeout_ms);
extern OmRet_e completion_accel_done_one_shot(Completion_t completion);
#define OM_COMPLETION_BACKEND_INIT completion_accel_init
#define OM_COMPLETION_BACKEND_DEINIT completion_accel_deinit
#define OM_COMPLETION_BACKEND_WAIT_ONE_SHOT completion_accel_wait_one_shot
#define OM_COMPLETION_BACKEND_DONE_ONE_SHOT completion_accel_done_one_shot
#else
#define OM_COMPLETION_BACKEND_INIT completion_sem_init
#define OM_COMPLETION_BACKEND_DEINIT completion_sem_deinit
#define OM_COMPLETION_BACKEND_WAIT_ONE_SHOT completion_sem_wait_one_shot
#define OM_COMPLETION_BACKEND_DONE_ONE_SHOT completion_sem_done_one_shot
#endif

OmRet_e completion_init(Completion_t completion)
{
    return OM_COMPLETION_BACKEND_INIT(completion);
}

void completion_deinit(Completion_t completion)
{
    OM_COMPLETION_BACKEND_DEINIT(completion);
}

OmRet_e completion_wait(Completion_t completion, size_t timeout_ms)
{
    return OM_COMPLETION_BACKEND_WAIT_ONE_SHOT(completion, timeout_ms);
}

OmRet_e completion_done(Completion_t completion)
{
    return OM_COMPLETION_BACKEND_DONE_ONE_SHOT(completion);
}

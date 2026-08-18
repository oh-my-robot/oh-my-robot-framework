#include "sync/completion.h"

#include "core/atomic/atomic_simple.h"
#include "osal/osal_core.h"
#include "osal/osal_port.h"
#include "osal/osal_sem.h"
#include "osal/osal_thread.h"

/*
 * completion 后端选择策略：
 * - reference 后端：CAS + OSAL Semaphore（4 状态机，无 irq_lock）；
 * - 加速后端通过 capability 显式声明（如 CAS + 任务通知）。
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

static void completion_sem_drain(OsalSem *sem)
{
    if (!sem)
        return;
    while (osal_sem_wait(sem, 0u) == OSAL_OK) {
    }
}

static OmRet completion_cas_sem_init(Completion *completion)
{
    if (!completion)
        return OM_ERROR_PARAM;

    if (!completion->sem) {
        if (osal_sem_create(&completion->sem, 1u, 0u) != OSAL_OK)
            return OM_ERROR_MEMORY;
    }

    completion_sem_drain(completion->sem);
    completion->waitThread = NULL;
    OM_STORE_REL(&completion->status, COMP_INIT);
    return OM_OK;
}

static void completion_cas_sem_deinit(Completion *completion)
{
    if (!completion)
        return;

    if (completion->sem) {
        (void)osal_sem_delete(completion->sem);
        completion->sem = NULL;
    }

    completion->waitThread = NULL;
    OM_STORE_REL(&completion->status, COMP_INIT);
}

static OmRet completion_cas_sem_wait_one_shot(Completion *completion, size_t timeout_ms)
{
    if (!completion || !completion->sem)
        return OM_ERROR_PARAM;
    if (osal_is_in_isr())
        return OM_ERROR_PARAM;

    OsalThread *self = osal_thread_self();
    if (!self)
        return OM_ERROR;

    /* 阶段 1: 尝试消费已完成的 done */
    CompStatus snap = (CompStatus)OM_LOAD_ACQ(&completion->status);

    if (snap == COMP_DONE) {
        CompStatus expected = COMP_DONE;
        if (OM_CAS_AR(&completion->status, &expected, COMP_INIT)) {
            completion->waitThread = NULL;
            completion_sem_drain(completion->sem);
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
    if (!OM_CAS_AR(&completion->status, &expected, COMP_WAIT)) {
        completion->waitThread = NULL;
        if (expected == COMP_DONE) {
            expected = COMP_DONE;
            if (OM_CAS_AR(&completion->status, &expected, COMP_INIT)) {
                completion_sem_drain(completion->sem);
                return OM_OK;
            }
        }
        return (expected == COMP_WAIT || expected == COMP_WAITING) ? OM_ERROR_BUSY : OM_ERROR;
    }

    /* 阶段 3: WAIT → WAITING（消除 CAS→block 竞态） */
    expected = COMP_WAIT;
    if (!OM_CAS_AR(&completion->status, &expected, COMP_WAITING)) {
        completion->waitThread = NULL;
        if (expected == COMP_DONE) {
            expected = COMP_DONE;
            OM_CAS_AR(&completion->status, &expected, COMP_INIT);
            completion_sem_drain(completion->sem);
            return OM_OK;
        }
        OM_STORE_REL(&completion->status, COMP_INIT);
        return OM_ERROR;
    }

    /* 阶段 4: 阻塞等待 */
    uint32_t osal_timeout_ms = completion_timeout_to_osal_ms(timeout_ms);
    OsalStatus wait_status   = osal_sem_wait(completion->sem, osal_timeout_ms);

    if (wait_status == OSAL_OK) {
        completion->waitThread = NULL;
        OM_STORE_REL(&completion->status, COMP_INIT);
        completion_sem_drain(completion->sem);
        return OM_OK;
    }

    /* 超时路径: 清理 WAITING → INIT */
    expected = COMP_WAITING;
    if (!OM_CAS_AR(&completion->status, &expected, COMP_INIT)) {
        completion->waitThread = NULL;
        if (expected == COMP_DONE) {
            OM_STORE_REL(&completion->status, COMP_INIT);
            completion_sem_drain(completion->sem);
            return OM_OK;
        }
        OM_STORE_REL(&completion->status, COMP_INIT);
        return OM_ERROR;
    }

    completion->waitThread = NULL;
    return OM_ERROR_TIMEOUT;
}

static OmRet completion_cas_sem_done_one_shot(Completion *completion)
{
    if (!completion || !completion->sem)
        return OM_ERROR_PARAM;

    int in_isr = osal_is_in_isr();

    /* 快速路径: INIT → DONE */
    CompStatus expected = COMP_INIT;
    if (OM_CAS_AR(&completion->status, &expected, COMP_DONE))
        return OM_OK;

    if (expected == COMP_DONE)
        return OM_ERROR_BUSY;

    /* 等待路径: WAIT 或 WAITING → DONE */
    if (expected == COMP_WAIT || expected == COMP_WAITING) {
        CompStatus prev = expected;
        if (OM_CAS_AR(&completion->status, &prev, COMP_DONE)) {
            if (in_isr)
                (void)osal_sem_post_from_isr(completion->sem);
            else
                (void)osal_sem_post(completion->sem);
            return OM_OK;
        }
        if (prev == COMP_DONE)
            return OM_ERROR_BUSY;
        return OM_ERROR;
    }

    return OM_ERROR;
}

#if OM_COMPLETION_ACCEL_ENABLED
extern OmRet completion_accel_init(Completion *completion);
extern void completion_accel_deinit(Completion *completion);
extern OmRet completion_accel_wait_one_shot(Completion *completion, size_t timeout_ms);
extern OmRet completion_accel_done_one_shot(Completion *completion);
#define OM_COMPLETION_BACKEND_INIT          completion_accel_init
#define OM_COMPLETION_BACKEND_DEINIT        completion_accel_deinit
#define OM_COMPLETION_BACKEND_WAIT_ONE_SHOT completion_accel_wait_one_shot
#define OM_COMPLETION_BACKEND_DONE_ONE_SHOT completion_accel_done_one_shot
#else
#define OM_COMPLETION_BACKEND_INIT          completion_cas_sem_init
#define OM_COMPLETION_BACKEND_DEINIT        completion_cas_sem_deinit
#define OM_COMPLETION_BACKEND_WAIT_ONE_SHOT completion_cas_sem_wait_one_shot
#define OM_COMPLETION_BACKEND_DONE_ONE_SHOT completion_cas_sem_done_one_shot
#endif

OmRet completion_init(Completion *completion)
{
    return OM_COMPLETION_BACKEND_INIT(completion);
}

void completion_deinit(Completion *completion)
{
    OM_COMPLETION_BACKEND_DEINIT(completion);
}

OmRet completion_wait(Completion *completion, size_t timeout_ms)
{
    return OM_COMPLETION_BACKEND_WAIT_ONE_SHOT(completion, timeout_ms);
}

OmRet completion_done(Completion *completion)
{
    return OM_COMPLETION_BACKEND_DONE_ONE_SHOT(completion);
}

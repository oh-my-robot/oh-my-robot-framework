/**
 * @file   osal_sem_none.c
 * @brief  osal-none 端口信号量：计数信号量（忙等语义）
 *
 * 等待 = osal_none_poll_wait 忙等轮询（带超时三态映射）；计数为
 * 任务↔ISR 共享面，读写均在临界区内（host = 临界区对象，target = 关中断）。
 * 返回码映射与 freertos 端口一致：满时 post → OSAL_NO_RESOURCE；
 * timeout==0 条件不满足 → OSAL_WOULD_BLOCK；>0 到期 → OSAL_TIMEOUT。
 */

#include "osal/osal_sem.h"

#include "osal_none_internal.h"

struct OsalSemHandle_s
{
    bool valid;
    uint32_t max;
    uint32_t count;
};

static bool osal_none_sem_try_take(void *arg)
{
    OsalSem *sem = (OsalSem *)arg;
    bool ok = false;

    osal_irq_lock_task();
    if (sem->count > 0u)
    {
        sem->count--;
        ok = true;
    }
    osal_irq_unlock_task();
    return ok;
}

OsalStatus osal_sem_create(OsalSem **sem, uint32_t max_count, uint32_t init_count)
{
    OsalSem *obj;

    if (!sem || max_count == 0u || init_count > max_count)
    {
        return OSAL_INVALID;
    }
    if (osal_is_in_isr())
    {
        return OSAL_INVALID;
    }

    obj = (OsalSem *)osal_malloc(sizeof(struct OsalSemHandle_s));
    if (!obj)
    {
        return OSAL_NO_RESOURCE;
    }
    obj->valid = true;
    obj->max = max_count;
    obj->count = init_count;
    *sem = obj;
    return OSAL_OK;
}

OsalStatus osal_sem_delete(OsalSem *sem)
{
    if (!sem || osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    if (!sem->valid)
    {
        return OSAL_INVALID;
    }
    sem->valid = false; /* 堆只分配不释放：停用即回收语义 */
    return OSAL_OK;
}

OsalStatus osal_sem_wait(OsalSem *sem, uint32_t timeout_ms)
{
    OsalStatus status;

    if (!sem)
    {
        return OSAL_INVALID;
    }
    status = osal_none_check_blocking_ctx();
    if (status != OSAL_OK)
    {
        return status;
    }
    if (!sem->valid)
    {
        return OSAL_INVALID;
    }
    return osal_none_poll_wait(osal_none_sem_try_take, sem, timeout_ms);
}

OsalStatus osal_sem_post(OsalSem *sem)
{
    if (!sem || osal_is_in_isr())
    {
        return OSAL_INVALID; /* 契约：ISR 中 post 用 post_from_isr */
    }
    if (!sem->valid)
    {
        return OSAL_INVALID;
    }

    osal_irq_lock_task();
    if (sem->count >= sem->max)
    {
        osal_irq_unlock_task();
        return OSAL_NO_RESOURCE;
    }
    sem->count++;
    osal_irq_unlock_task();
    return OSAL_OK;
}

OsalStatus osal_sem_post_from_isr(OsalSem *sem)
{
    OsalIrqIsrState key;

    if (!sem || !osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    if (!sem->valid)
    {
        return OSAL_INVALID;
    }

    key = osal_irq_lock_from_isr();
    if (sem->count >= sem->max)
    {
        osal_irq_unlock_from_isr(key);
        return OSAL_NO_RESOURCE;
    }
    sem->count++;
    osal_irq_unlock_from_isr(key);
    return OSAL_OK;
}

OsalStatus osal_sem_get_count(OsalSem *sem, uint32_t *out_count)
{
    if (!sem || !out_count || osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    if (!sem->valid)
    {
        return OSAL_INVALID;
    }

    osal_irq_lock_task();
    *out_count = sem->count;
    osal_irq_unlock_task();
    return OSAL_OK;
}

OsalStatus osal_sem_get_count_from_isr(OsalSem *sem, uint32_t *out_count)
{
    OsalIrqIsrState key;

    if (!sem || !out_count || !osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    if (!sem->valid)
    {
        return OSAL_INVALID;
    }

    key = osal_irq_lock_from_isr();
    *out_count = sem->count;
    osal_irq_unlock_from_isr(key);
    return OSAL_OK;
}

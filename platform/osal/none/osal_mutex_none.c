/**
 * @file   osal_mutex_none.c
 * @brief  osal-none 端口互斥锁：0/1 互斥开关 + 持有者身份
 *
 * 单执行流下锁从不被"他人"争用——本实现保留完整语义（0/1、owner 检查、
 * 非递归）作为同码双形态的保真面：误用模式（重复加锁/空锁解锁/非 owner
 * 解锁）按契约返回显式错误码。owner = 唯一执行体单例句柄。
 */

#include "osal/osal_mutex.h"

#include "osal_none_internal.h"

struct OsalMutexHandle_s
{
    bool valid;
    bool locked;
    OsalThread *owner;
};

static bool osal_none_mutex_try_lock(void *arg)
{
    OsalMutex *mutex = (OsalMutex *)arg;
    bool ok = false;

    osal_irq_lock_task();
    if (!mutex->locked)
    {
        mutex->locked = true;
        mutex->owner = osal_none_executor_handle();
        ok = true;
    }
    osal_irq_unlock_task();
    return ok;
}

OsalStatus osal_mutex_create(OsalMutex **mutex)
{
    OsalMutex *obj;

    if (!mutex)
    {
        return OSAL_INVALID;
    }
    if (osal_is_in_isr())
    {
        return OSAL_INVALID;
    }

    obj = (OsalMutex *)osal_malloc(sizeof(struct OsalMutexHandle_s));
    if (!obj)
    {
        return OSAL_NO_RESOURCE;
    }
    obj->valid = true;
    obj->locked = false;
    obj->owner = NULL;
    *mutex = obj;
    return OSAL_OK;
}

OsalStatus osal_mutex_delete(OsalMutex *mutex)
{
    if (!mutex || osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    if (!mutex->valid)
    {
        return OSAL_INVALID;
    }
    mutex->valid = false;
    return OSAL_OK;
}

OsalStatus osal_mutex_lock(OsalMutex *mutex, uint32_t timeout_ms)
{
    OsalStatus status;

    if (!mutex)
    {
        return OSAL_INVALID;
    }
    status = osal_none_check_blocking_ctx();
    if (status != OSAL_OK)
    {
        return status;
    }
    if (!mutex->valid)
    {
        return OSAL_INVALID;
    }
    /* 非递归语义：持锁者重复加锁视为争用（按超时规则失败）——契约 */
    return osal_none_poll_wait(osal_none_mutex_try_lock, mutex, timeout_ms);
}

OsalStatus osal_mutex_unlock(OsalMutex *mutex)
{
    if (!mutex || osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    if (!mutex->valid)
    {
        return OSAL_INVALID;
    }

    osal_irq_lock_task();
    if (!mutex->locked || mutex->owner != osal_none_executor_handle())
    {
        osal_irq_unlock_task();
        return OSAL_INVALID; /* 未持有 / 非 owner 解锁（契约） */
    }
    mutex->locked = false;
    mutex->owner = NULL;
    osal_irq_unlock_task();
    return OSAL_OK;
}

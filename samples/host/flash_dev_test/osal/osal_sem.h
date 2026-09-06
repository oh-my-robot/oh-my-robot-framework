/**
 * @file   osal_sem.h
 * @brief  OSAL semaphore host 裁剪头（flash_dev_test 用，签名与 lib 版一致）
 */

#ifndef OM_OSAL_SEM_H
#define OM_OSAL_SEM_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalSemHandle_s OsalSem;

OsalStatus osal_sem_create(OsalSem **sem, uint32_t max_count, uint32_t init_count);
OsalStatus osal_sem_delete(OsalSem *sem);
OsalStatus osal_sem_wait(OsalSem *sem, uint32_t timeout_ms);
OsalStatus osal_sem_post(OsalSem *sem);
OsalStatus osal_sem_post_from_isr(OsalSem *sem);

/* 上下文自动分派（host 无 ISR 语境：恒走 post） */
static inline OsalStatus osal_sem_post_auto(OsalSem *sem)
{
    return osal_sem_post(sem);
}

#endif /* OM_OSAL_SEM_H */

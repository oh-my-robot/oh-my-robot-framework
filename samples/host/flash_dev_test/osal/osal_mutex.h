/**
 * @file   osal_mutex.h
 * @brief  OSAL mutex host 裁剪头（flash_dev_test 用，声明与 lib 版一致）
 */

#ifndef OM_OSAL_MUTEX_H
#define OM_OSAL_MUTEX_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalMutexHandle_s OsalMutex;

OsalStatus osal_mutex_create(OsalMutex **mutex);
OsalStatus osal_mutex_delete(OsalMutex *mutex);
OsalStatus osal_mutex_lock(OsalMutex *mutex, uint32_t timeout_ms);
OsalStatus osal_mutex_unlock(OsalMutex *mutex);

#endif /* OM_OSAL_MUTEX_H */

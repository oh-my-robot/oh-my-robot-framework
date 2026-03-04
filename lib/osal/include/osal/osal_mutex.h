#ifndef OM_OSAL_MUTEX_H
#define OM_OSAL_MUTEX_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalMutexHandle_s* OsalMutex_t;

/**
 * @brief 创建互斥锁（线程上下文）
 * @param mutex 输出互斥锁句
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 禁止ISR 中调用
 */
OsalStatus_t osal_mutex_create(OsalMutex_t* mutex);

/**
 * @brief 删除互斥锁（线程上下文）
 * @param mutex 互斥锁句
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID`
 * @note 禁止ISR 中调用
 * @note 严格前置条件：调用方需确保无并发访问和无等待者
 */
OsalStatus_t osal_mutex_delete(OsalMutex_t mutex);

/**
 * @brief 加锁（线程上下文，非递归语义）
 * @param mutex 互斥锁句
 * @param timeout_ms 超时时间（ms），可用 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 成功；失败返`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID/OSAL_INTERNAL`
 * @note 禁止ISR 中调用
 * @note v1.0 不支持递归 mutex；同线程重复加锁按超时规则返`OSAL_WOULD_BLOCK/OSAL_TIMEOUT`（或无限等待）
 */
OsalStatus_t osal_mutex_lock(OsalMutex_t mutex, uint32_t timeout_ms);

/**
 * @brief 解锁（线程上下文）
 * @param mutex 互斥锁句
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID`
 * @note 禁止ISR 中调用
 * @note 非 owner 解锁返回 `OSAL_INVALID`
 * @note 互斥锁应避免跨线程释放；仅持有锁的线程可执行解锁
 */
OsalStatus_t osal_mutex_unlock(OsalMutex_t mutex);

#endif

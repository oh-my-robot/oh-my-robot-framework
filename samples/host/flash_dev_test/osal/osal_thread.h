/**
 * @file   osal_thread.h
 * @brief  OSAL thread host 裁剪头（flash_dev_test 用，签名与 lib 版一致）
 */

#ifndef OM_OSAL_THREAD_H
#define OM_OSAL_THREAD_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalThreadHandle_s OsalThread;
typedef void (*OsalThreadEntryFunction)(void *arg);

typedef struct {
    const char *name;   /* 线程名称，用于调试 */
    uint32_t stackSize; /* 栈大小（字节），0 = 端口默认 */
    uint32_t priority;  /* 线程优先级（OSAL_PRIO_<band>_BASE + offset） */
} OsalThreadAttr;

OsalStatus osal_thread_create(OsalThread **thread, const OsalThreadAttr *attr,
                              OsalThreadEntryFunction entry, void *arg);
OsalThread *osal_thread_self(void);
OsalStatus osal_thread_join(OsalThread *thread, uint32_t timeout_ms);
void osal_thread_yield(void);
void osal_thread_exit(void);

#endif /* OM_OSAL_THREAD_H */

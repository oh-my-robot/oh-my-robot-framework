/**
 * @file   completion.h
 * @brief  Completion host 裁剪头（flash_dev_test 用）
 *
 * 覆盖 lib/sync 版（其依赖 core atomic 扩展）：结构自定，workqueue.c 只经
 * API 操作（不摸字段）。语义：单次 done 唤醒一个等待者（等 ISR 场景同构）。
 */

#ifndef OM_SYNC_COMPLETION_H
#define OM_SYNC_COMPLETION_H

#include <stddef.h>
#include <stdint.h>

#include "core/om_def.h"
#include "osal/osal_sem.h"

typedef struct Completion Completion;
typedef struct Completion {
    OsalSem *sem; /* 完成信号（count 0/1） */
} Completion;

OmRet completion_init(Completion *completion);
void completion_deinit(Completion *completion);
OmRet completion_wait(Completion *completion, size_t timeout_ms);
OmRet completion_done(Completion *completion);

#endif /* OM_SYNC_COMPLETION_H */

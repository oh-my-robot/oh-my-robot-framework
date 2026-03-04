#ifndef __OM_SYNC_COMPLETION_H__
#define __OM_SYNC_COMPLETION_H__

#include <stddef.h>
#include <stdint.h>

#include "core/om_def.h"
#include "osal/osal_sem.h"
#include "osal/osal_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COMP_INIT = 0,
    COMP_WAIT,
    COMP_DONE,
} CompStatus;

typedef struct Completion  Completion;
typedef struct Completion {
    OsalSem* sem;
    OsalThread* waitThread;
    volatile CompStatus status;
} Completion;

OmRet completion_init(Completion* completion);
void completion_deinit(Completion* completion);
OmRet completion_wait(Completion* completion, size_t timeout_ms);
OmRet completion_done(Completion* completion);

#ifdef __cplusplus
}
#endif

#endif


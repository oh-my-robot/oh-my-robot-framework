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
} CompStatus_e;

typedef struct Completion *Completion_t;
typedef struct Completion {
    OsalSem_t sem;
    OsalThread_t waitThread;
    volatile CompStatus_e status;
} Completion_s;

OmRet_e completion_init(Completion_t completion);
void completion_deinit(Completion_t completion);
OmRet_e completion_wait(Completion_t completion, size_t timeout_ms);
OmRet_e completion_done(Completion_t completion);

#ifdef __cplusplus
}
#endif

#endif


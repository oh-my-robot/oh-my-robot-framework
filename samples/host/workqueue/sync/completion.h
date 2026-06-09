#ifndef __OM_SYNC_COMPLETION_H__
#define __OM_SYNC_COMPLETION_H__

#include <stddef.h>
#include <stdint.h>

#include "core/om_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Completion {
    void *_handle; /**< Win32 HANDLE (Event) */
} Completion;

OmRet completion_init(Completion *c);
void  completion_deinit(Completion *c);
OmRet completion_wait(Completion *c, size_t timeout_ms);
OmRet completion_done(Completion *c);

#ifdef __cplusplus
}
#endif

#endif /* __OM_SYNC_COMPLETION_H__ */

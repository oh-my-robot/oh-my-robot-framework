#ifndef OM_OSAL_THREAD_H
#define OM_OSAL_THREAD_H

#include "osal_core.h"

typedef struct OsalThreadHandle_s OsalThread;
typedef void (*OsalThreadEntryFunction)(void *arg);

typedef struct {
    const char *name;
    uint32_t stackSize;
    uint32_t priority;
} OsalThreadAttr;

OsalStatus osal_thread_create(OsalThread **thread, const OsalThreadAttr *attr,
                              OsalThreadEntryFunction entry, void *arg);
void osal_thread_exit(void);

#endif

/**
 * @file    host_osal.c
 * @brief   Win32 平台 OSAL + Completion 实现（host 测试用）
 */

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

#include "osal/osal_config.h"
#include "osal/osal_core.h"
#include "osal/osal_sem.h"
#include "osal/osal_thread.h"
#include "sync/completion.h"

/* ===================================================================
 * irq_lock — Win32 CriticalSection
 * =================================================================== */

static CRITICAL_SECTION g_cs;
static volatile LONG g_cs_inited = 0;

static void ensure_cs(void)
{
    if (!g_cs_inited) {
        InitializeCriticalSection(&g_cs);
        g_cs_inited = 1;
    }
}

int osal_is_in_isr(void) { return 0; }

void osal_irq_lock_task(void)
{
    ensure_cs();
    EnterCriticalSection(&g_cs);
}

void osal_irq_unlock_task(void) { LeaveCriticalSection(&g_cs); }

OsalIrqIsrState osal_irq_lock_from_isr(void)
{
    osal_irq_lock_task();
    return 0;
}

void osal_irq_unlock_from_isr(OsalIrqIsrState state)
{
    (void)state;
    osal_irq_unlock_task();
}

void osal_sleep_ms(uint32_t ms) { Sleep(ms); }

/* ===================================================================
 * Semaphore — Win32 CreateSemaphore
 * =================================================================== */

struct OsalSemHandle_s {
    HANDLE handle;
};

OsalStatus osal_sem_create(OsalSem **sem, uint32_t max_count, uint32_t init_count)
{
    OsalSem *s = (OsalSem *)malloc(sizeof(OsalSem));
    if (!s) return OSAL_TIMEOUT;
    s->handle = CreateSemaphoreA(NULL, (LONG)init_count, (LONG)max_count, NULL);
    if (!s->handle) {
        free(s);
        return OSAL_TIMEOUT;
    }
    *sem = s;
    return OSAL_OK;
}

OsalStatus osal_sem_delete(OsalSem *sem)
{
    if (!sem) return OSAL_TIMEOUT;
    CloseHandle(sem->handle);
    free(sem);
    return OSAL_OK;
}

OsalStatus osal_sem_wait(OsalSem *sem, uint32_t timeout_ms)
{
    if (!sem) return OSAL_TIMEOUT;
    DWORD ms = (timeout_ms == OSAL_WAIT_FOREVER) ? INFINITE : (DWORD)timeout_ms;
    DWORD r  = WaitForSingleObject(sem->handle, ms);
    if (r == WAIT_OBJECT_0) return OSAL_OK;
    return OSAL_TIMEOUT;
}

OsalStatus osal_sem_post(OsalSem *sem)
{
    if (!sem) return OSAL_TIMEOUT;
    ReleaseSemaphore(sem->handle, 1, NULL);
    return OSAL_OK;
}

OsalStatus osal_sem_post_from_isr(OsalSem *sem) { return osal_sem_post(sem); }

/* ===================================================================
 * Thread — Win32 CreateThread
 * =================================================================== */

struct OsalThreadHandle_s {
    HANDLE handle;
};

typedef struct {
    OsalThreadEntryFunction entry;
    void                   *arg;
} ThreadStartCtx;

static DWORD WINAPI thread_start_wrapper(LPVOID param)
{
    ThreadStartCtx *ctx = (ThreadStartCtx *)param;
    OsalThreadEntryFunction fn = ctx->entry;
    void *arg = ctx->arg;
    free(ctx);
    fn(arg);
    return 0;
}

OsalStatus osal_thread_create(OsalThread **thread, const OsalThreadAttr *attr,
                              OsalThreadEntryFunction entry, void *arg)
{
    ThreadStartCtx *ctx = (ThreadStartCtx *)malloc(sizeof(ThreadStartCtx));
    if (!ctx) return OSAL_TIMEOUT;
    ctx->entry = entry;
    ctx->arg   = arg;

    OsalThread *t = (OsalThread *)malloc(sizeof(OsalThread));
    if (!t) {
        free(ctx);
        return OSAL_TIMEOUT;
    }

    t->handle = CreateThread(NULL, (SIZE_T)attr->stackSize,
                             thread_start_wrapper, ctx, 0, NULL);
    if (!t->handle) {
        free(t);
        free(ctx);
        return OSAL_TIMEOUT;
    }

    *thread = t;
    return OSAL_OK;
}

void osal_thread_exit(void) { ExitThread(0); }

/* ===================================================================
 * Completion — Win32 Event (manual-reset)
 * =================================================================== */

OmRet completion_init(Completion *c)
{
    if (c->_handle) {
        CloseHandle((HANDLE)c->_handle);
    }
    c->_handle = (void *)CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!c->_handle) return OM_ERROR;
    return OM_OK;
}

void completion_deinit(Completion *c)
{
    if (c->_handle) {
        CloseHandle((HANDLE)c->_handle);
        c->_handle = NULL;
    }
}

OmRet completion_wait(Completion *c, size_t timeout_ms)
{
    DWORD ms = (timeout_ms == OSAL_WAIT_FOREVER) ? INFINITE : (DWORD)timeout_ms;
    DWORD r  = WaitForSingleObject((HANDLE)c->_handle, ms);
    if (r == WAIT_OBJECT_0) return OM_OK;
    return OM_ERROR_TIMEOUT;
}

OmRet completion_done(Completion *c)
{
    SetEvent((HANDLE)c->_handle);
    return OM_OK;
}

/**
 * @file   host_osal.c
 * @brief  OSAL host 桩（flash_dev_test 用：irq_lock / mutex / sem / thread / time）
 *
 * 双平台：Windows = CRITICAL_SECTION + Semaphore + CreateThread；
 *         Linux  = pthread + sem_t。
 * 覆盖 workqueue 原语依赖面（thread/sem/completion/time/sleep）。
 */

#include <stdlib.h>
#include <string.h>

#include "osal/osal_core.h"
#include "osal/osal_mutex.h"
#include "osal/osal_sem.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h"
#include "sync/completion.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#endif

/* ===================================================================
 * irq_lock — 全局临界区（device 链表/队列保护）
 * =================================================================== */

#ifdef _WIN32
static CRITICAL_SECTION g_cs;
static volatile LONG g_cs_inited = 0;
static void ensure_cs(void)
{
    if (!g_cs_inited)
    {
        InitializeCriticalSection(&g_cs);
        g_cs_inited = 1;
    }
}
#else
/* 递归互斥：提交路径在 irq_lock 临界区内嵌 workqueue_enqueue（其内部再 lock） */
static pthread_mutex_t g_cs = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#endif

int osal_is_in_isr(void)
{
    return 0;
}

void osal_irq_lock_task(void)
{
#ifdef _WIN32
    ensure_cs();
    EnterCriticalSection(&g_cs);
#else
    pthread_mutex_lock(&g_cs);
#endif
}

void osal_irq_unlock_task(void)
{
#ifdef _WIN32
    LeaveCriticalSection(&g_cs);
#else
    pthread_mutex_unlock(&g_cs);
#endif
}

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

void osal_sleep_ms(uint32_t ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    usleep((useconds_t)ms * 1000u);
#endif
}

OsalTimeMs osal_time_now_monotonic(void)
{
#ifdef _WIN32
    return (OsalTimeMs)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (OsalTimeMs)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
#endif
}

/* ===================================================================
 * Mutex — 非递归 + owner 校验
 * =================================================================== */

struct OsalMutexHandle_s
{
#ifdef _WIN32
    CRITICAL_SECTION cs;
    DWORD owner;
#else
    pthread_mutex_t mtx;
    pthread_t owner;
#endif
    int owned;
};

static int osal_mutex_is_self(const struct OsalMutexHandle_s *m)
{
#ifdef _WIN32
    return m->owner == GetCurrentThreadId();
#else
    return pthread_equal(m->owner, pthread_self()) != 0;
#endif
}

OsalStatus osal_mutex_create(OsalMutex **mutex)
{
    if (!mutex)
    {
        return OSAL_INVALID;
    }
    struct OsalMutexHandle_s *m = (struct OsalMutexHandle_s *)calloc(1, sizeof(*m));
    if (!m)
    {
        return OSAL_NO_RESOURCE;
    }
#ifdef _WIN32
    InitializeCriticalSection(&m->cs);
#else
    pthread_mutex_init(&m->mtx, NULL);
#endif
    *mutex = (OsalMutex *)m;
    return OSAL_OK;
}

OsalStatus osal_mutex_delete(OsalMutex *mutex)
{
    struct OsalMutexHandle_s *m = (struct OsalMutexHandle_s *)mutex;
    if (!m)
    {
        return OSAL_INVALID;
    }
#ifdef _WIN32
    DeleteCriticalSection(&m->cs);
#else
    pthread_mutex_destroy(&m->mtx);
#endif
    free(m);
    return OSAL_OK;
}

OsalStatus osal_mutex_lock(OsalMutex *mutex, uint32_t timeout_ms)
{
    struct OsalMutexHandle_s *m = (struct OsalMutexHandle_s *)mutex;
    if (!m)
    {
        return OSAL_INVALID;
    }
    if (m->owned && osal_mutex_is_self(m))
    {
        return OSAL_WOULD_BLOCK; /* 非递归：同线程重入拒绝 */
    }

    if (timeout_ms == OSAL_WAIT_FOREVER)
    {
#ifdef _WIN32
        EnterCriticalSection(&m->cs);
#else
        pthread_mutex_lock(&m->mtx);
#endif
    }
    else
    {
        uint64_t deadline = 0;
        if (timeout_ms > 0)
        {
#ifdef _WIN32
            deadline = (uint64_t)GetTickCount64() + timeout_ms;
#else
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            deadline = (uint64_t)ts.tv_sec * 1000u + ts.tv_nsec / 1000000u + timeout_ms;
#endif
        }
        for (;;)
        {
#ifdef _WIN32
            if (TryEnterCriticalSection(&m->cs))
            {
                break;
            }
            Sleep(1);
            if (timeout_ms > 0 && (uint64_t)GetTickCount64() >= deadline)
            {
                return OSAL_TIMEOUT;
            }
#else
            if (pthread_mutex_trylock(&m->mtx) == 0)
            {
                break;
            }
            usleep(1000);
            if (timeout_ms > 0)
            {
                struct timespec ts2;
                clock_gettime(CLOCK_MONOTONIC, &ts2);
                uint64_t now = (uint64_t)ts2.tv_sec * 1000u + ts2.tv_nsec / 1000000u;
                if (now >= deadline)
                {
                    return OSAL_TIMEOUT;
                }
            }
#endif
            if (timeout_ms == 0)
            {
                return OSAL_WOULD_BLOCK;
            }
        }
    }
    m->owned = 1;
#ifdef _WIN32
    m->owner = GetCurrentThreadId();
#else
    m->owner = pthread_self();
#endif
    return OSAL_OK;
}

OsalStatus osal_mutex_unlock(OsalMutex *mutex)
{
    struct OsalMutexHandle_s *m = (struct OsalMutexHandle_s *)mutex;
    if (!m)
    {
        return OSAL_INVALID;
    }
    if (!m->owned || !osal_mutex_is_self(m))
    {
        return OSAL_INVALID;
    }
    m->owned = 0;
#ifdef _WIN32
    m->owner = 0;
    LeaveCriticalSection(&m->cs);
#else
    memset(&m->owner, 0, sizeof(m->owner));
    pthread_mutex_unlock(&m->mtx);
#endif
    return OSAL_OK;
}

/* ===================================================================
 * Semaphore — 计数信号量（Win CreateSemaphore / POSIX sem_t）
 * =================================================================== */

struct OsalSemHandle_s
{
#ifdef _WIN32
    HANDLE handle;
#else
    sem_t sem;
#endif
};

OsalStatus osal_sem_create(OsalSem **sem, uint32_t max_count, uint32_t init_count)
{
    if (!sem || max_count == 0)
    {
        return OSAL_INVALID;
    }
    struct OsalSemHandle_s *s = (struct OsalSemHandle_s *)calloc(1, sizeof(*s));
    if (!s)
    {
        return OSAL_NO_RESOURCE;
    }
#ifdef _WIN32
    s->handle = CreateSemaphore(NULL, (LONG)init_count, (LONG)max_count, NULL);
    if (!s->handle)
    {
        free(s);
        return OSAL_NO_RESOURCE;
    }
#else
    if (sem_init(&s->sem, 0, (unsigned)init_count) != 0)
    {
        free(s);
        return OSAL_NO_RESOURCE;
    }
#endif
    *sem = (OsalSem *)s;
    return OSAL_OK;
}

OsalStatus osal_sem_delete(OsalSem *sem)
{
    struct OsalSemHandle_s *s = (struct OsalSemHandle_s *)sem;
    if (!s)
    {
        return OSAL_INVALID;
    }
#ifdef _WIN32
    CloseHandle(s->handle);
#else
    sem_destroy(&s->sem);
#endif
    free(s);
    return OSAL_OK;
}

OsalStatus osal_sem_wait(OsalSem *sem, uint32_t timeout_ms)
{
    struct OsalSemHandle_s *s = (struct OsalSemHandle_s *)sem;
    if (!s)
    {
        return OSAL_INVALID;
    }
#ifdef _WIN32
    DWORD t = (timeout_ms == OSAL_WAIT_FOREVER) ? INFINITE : (DWORD)timeout_ms;
    DWORD r = WaitForSingleObject(s->handle, t);
    if (r == WAIT_OBJECT_0)
    {
        return OSAL_OK;
    }
    return (timeout_ms == 0) ? OSAL_WOULD_BLOCK : OSAL_TIMEOUT;
#else
    if (timeout_ms == OSAL_WAIT_FOREVER)
    {
        return (sem_wait(&s->sem) == 0) ? OSAL_OK : OSAL_INVALID;
    }
    if (timeout_ms == 0)
    {
        return (sem_trywait(&s->sem) == 0) ? OSAL_OK : OSAL_WOULD_BLOCK;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000u;
    ts.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return (sem_timedwait(&s->sem, &ts) == 0) ? OSAL_OK : OSAL_TIMEOUT;
#endif
}

OsalStatus osal_sem_post(OsalSem *sem)
{
    struct OsalSemHandle_s *s = (struct OsalSemHandle_s *)sem;
    if (!s)
    {
        return OSAL_INVALID;
    }
#ifdef _WIN32
    return ReleaseSemaphore(s->handle, 1, NULL) ? OSAL_OK : OSAL_INVALID;
#else
    return (sem_post(&s->sem) == 0) ? OSAL_OK : OSAL_INVALID;
#endif
}

OsalStatus osal_sem_post_from_isr(OsalSem *sem)
{
    return osal_sem_post(sem); /* host 无 ISR 语境 */
}

/* ===================================================================
 * Thread — 平台线程包装（TLS 记录 self 句柄）
 * =================================================================== */

struct OsalThreadHandle_s
{
#ifdef _WIN32
    HANDLE handle;
    DWORD id;
#else
    pthread_t tid;
#endif
};

#ifdef _WIN32
static DWORD g_self_tls = TLS_OUT_OF_INDEXES;
#else
static pthread_key_t g_self_key;
static int g_self_key_inited = 0;
#endif

static void osal_thread_tls_set(OsalThread *self)
{
#ifdef _WIN32
    if (g_self_tls == TLS_OUT_OF_INDEXES)
    {
        g_self_tls = TlsAlloc();
    }
    TlsSetValue(g_self_tls, self);
#else
    if (!g_self_key_inited)
    {
        pthread_key_create(&g_self_key, NULL);
        g_self_key_inited = 1;
    }
    pthread_setspecific(g_self_key, self);
#endif
}

static OsalThread *osal_thread_tls_get(void)
{
#ifdef _WIN32
    return (OsalThread *)(g_self_tls == TLS_OUT_OF_INDEXES ? NULL : TlsGetValue(g_self_tls));
#else
    return g_self_key_inited ? (OsalThread *)pthread_getspecific(g_self_key) : NULL;
#endif
}

typedef struct
{
    OsalThreadEntryFunction entry;
    void *arg;
    OsalThread *self;
} ThreadBoot;

#ifdef _WIN32
static DWORD WINAPI osal_thread_win_entry(LPVOID p)
{
    ThreadBoot *b = (ThreadBoot *)p;
    osal_thread_tls_set(b->self);
    b->entry(b->arg);
    free(b);
    return 0;
}
#else
static void *osal_thread_posix_entry(void *p)
{
    ThreadBoot *b = (ThreadBoot *)p;
    osal_thread_tls_set(b->self);
    b->entry(b->arg);
    free(b);
    return NULL;
}
#endif

OsalStatus osal_thread_create(OsalThread **thread, const OsalThreadAttr *attr,
                              OsalThreadEntryFunction entry, void *arg)
{
    (void)attr; /* host 桩：忽略栈/优先级 */
    if (!thread || !entry)
    {
        return OSAL_INVALID;
    }
    OsalThread *t = (OsalThread *)calloc(1, sizeof(OsalThread));
    if (!t)
    {
        return OSAL_NO_RESOURCE;
    }
    ThreadBoot *boot = (ThreadBoot *)malloc(sizeof(ThreadBoot));
    if (!boot)
    {
        free(t);
        return OSAL_NO_RESOURCE;
    }
    boot->entry = entry;
    boot->arg = arg;
    boot->self = t;

#ifdef _WIN32
    t->handle = CreateThread(NULL, 0, osal_thread_win_entry, boot, 0, &t->id);
    if (!t->handle)
    {
        free(boot);
        free(t);
        return OSAL_NO_RESOURCE;
    }
#else
    if (pthread_create(&t->tid, NULL, osal_thread_posix_entry, boot) != 0)
    {
        free(boot);
        free(t);
        return OSAL_NO_RESOURCE;
    }
#endif
    *thread = t;
    return OSAL_OK;
}

OsalThread *osal_thread_self(void)
{
    return osal_thread_tls_get();
}

OsalStatus osal_thread_join(OsalThread *thread, uint32_t timeout_ms)
{
    (void)timeout_ms; /* host 桩：无限等待 */
    if (!thread)
    {
        return OSAL_INVALID;
    }
#ifdef _WIN32
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
#else
    pthread_join(thread->tid, NULL);
#endif
    free(thread);
    return OSAL_OK;
}

void osal_thread_yield(void)
{
#ifdef _WIN32
    SwitchToThread();
#else
    sched_yield();
#endif
}

void osal_thread_exit(void)
{
#ifdef _WIN32
    ExitThread(0);
#else
    pthread_exit(NULL);
#endif
}

/* ===================================================================
 * Completion — 单次 done 唤醒一个等待者（等硬件事件场景同构）
 * =================================================================== */

OmRet completion_init(Completion *completion)
{
    if (!completion)
    {
        return OM_ERR_INVALID_ARG;
    }
    memset(completion, 0, sizeof(*completion));
    OsalSem *sem = NULL;
    if (osal_sem_create(&sem, 1u, 0u) != OSAL_OK)
    {
        return OM_ERR_NO_MEM;
    }
    completion->sem = sem;
    return OM_OK;
}

void completion_deinit(Completion *completion)
{
    if (!completion)
    {
        return;
    }
    if (completion->sem)
    {
        osal_sem_delete(completion->sem);
        completion->sem = NULL;
    }
}

OmRet completion_wait(Completion *completion, size_t timeout_ms)
{
    if (!completion || !completion->sem)
    {
        return OM_ERR_INVALID_ARG;
    }
    if (osal_sem_wait(completion->sem, (uint32_t)timeout_ms) != OSAL_OK)
    {
        return OM_ERR_TIMEOUT;
    }
    return OM_OK;
}

OmRet completion_done(Completion *completion)
{
    if (!completion || !completion->sem)
    {
        return OM_ERR_INVALID_ARG;
    }
    osal_sem_post(completion->sem);
    return OM_OK;
}

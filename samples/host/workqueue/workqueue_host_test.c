/**
 * @file    workqueue_host_test.c
 * @brief   Workqueue host 独立测试（Win32/pthread 平台）
 *
 * 移植自 samples/async/workqueue/main.c 的 13 个测试用例，
 * 额外增加 flush 测试。
 *
 * 通过 Win32 API 模拟 OSAL 最小子集，直接编译 workqueue.c 源码。
 */

#include <stdio.h>
#include <stdlib.h>

#include "async/workqueue.h"
#include "data_struct/corelist.h"
#include "osal/osal_config.h"
#include "osal/osal_core.h"
#include "osal/osal_sem.h"
#include "osal/osal_thread.h"
#include "sync/completion.h"

/* --------------------------------------------------------------------------
 * 测试结果追踪
 * -------------------------------------------------------------------------- */

static int g_total = 0;
static int g_failed = 0;
static int g_current_test = 0;

#define EXPECT(cond)                                               \
    do                                                             \
    {                                                              \
        g_total++;                                                 \
        if (!(cond))                                               \
        {                                                          \
            g_failed++;                                            \
            fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); \
        }                                                          \
    } while (0)

#define TEST_BEGIN(name)                              \
    do                                                \
    {                                                 \
        g_current_test++;                             \
        printf("[%2d] %-30s ", g_current_test, name); \
    } while (0)

#define TEST_END()      \
    do                  \
    {                   \
        printf("OK\n"); \
    } while (0)

/* --------------------------------------------------------------------------
 * 测试工作项
 * -------------------------------------------------------------------------- */

typedef struct
{
    Work w;
    OsalSem *s;
    int ex;
    int id;
} TW;

static void tw_handler(Work *w)
{
    TW *t = container_of(w, TW, w);
    t->ex++;
    if (t->s)
        osal_sem_post(t->s);
}

static void tw_init(TW *t, int id, OsalSem *s)
{
    work_init(&t->w, tw_handler, NULL);
    t->s = s;
    t->ex = 0;
    t->id = id;
}

static OsalSem *make_sem(void)
{
    OsalSem *s = NULL;
    (void)osal_sem_create(&s, 1u, 0u);
    return s;
}

static int wait_sem(OsalSem *s, uint32_t timeout_ms)
{
    return osal_sem_wait(s, timeout_ms) == OSAL_OK;
}

/* --------------------------------------------------------------------------
 * 测试用例
 * -------------------------------------------------------------------------- */

/* 测试1：NULL 参数校验 */
static void t_null_params(void)
{
    TEST_BEGIN("null_params");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_enqueue(NULL, NULL) == OM_ERROR_PARAM);
    EXPECT(workqueue_enqueue(&wq, NULL) == OM_ERROR_PARAM);
    EXPECT(workqueue_cancel(NULL) == OM_ERROR_PARAM);
    EXPECT(workqueue_init(NULL, &cfg) == OM_ERROR_PARAM);
    EXPECT(workqueue_init(&wq, NULL) == OM_ERROR_PARAM);
    EXPECT(workqueue_deinit(NULL) == OM_ERROR_PARAM);
    EXPECT(workqueue_start(NULL) == OM_ERROR_PARAM);
    EXPECT(workqueue_stop(NULL) == OM_ERROR_PARAM);
    EXPECT(workqueue_get_state(NULL) == WORKQUEUE_STATE_UNINIT);
    EXPECT(workqueue_is_empty(NULL) == true);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    TEST_END();
}

/* 测试2：生命周期状态机 */
static void t_lifecycle(void)
{
    TEST_BEGIN("lifecycle");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_get_state(&wq) == WORKQUEUE_STATE_UNINIT);
    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_get_state(&wq) == WORKQUEUE_STATE_IDLE);
    EXPECT(workqueue_start(&wq) == OM_OK);
    EXPECT(workqueue_get_state(&wq) == WORKQUEUE_STATE_RUNNING);
    EXPECT(workqueue_start(&wq) != OM_OK);
    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_get_state(&wq) == WORKQUEUE_STATE_IDLE);
    EXPECT(workqueue_stop(&wq) != OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);
    EXPECT(workqueue_get_state(&wq) == WORKQUEUE_STATE_RUNNING);
    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    TEST_END();
}

/* 测试3：基本入队→执行 */
static void t_basic(void)
{
    TEST_BEGIN("basic");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t;
    tw_init(&t, 42, sem);

    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_OK);
    EXPECT(wait_sem(sem, 5000u));
    EXPECT(t.ex == 1);

    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
    TEST_END();
}

/* 测试4：去重 */
static void t_dedup(void)
{
    TEST_BEGIN("dedup");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t;
    tw_init(&t, 0, sem);

    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_OK);
    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_ERROR_BUSY);
    EXPECT(wait_sem(sem, 5000u));
    EXPECT(t.ex == 1);

    /* 完成后可重新入队 */
    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_OK);
    EXPECT(wait_sem(sem, 5000u));

    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
    TEST_END();
}

/* 测试5：取消队列中尚未执行的工作项 */
static void t_cancel_pending(void)
{
    TEST_BEGIN("cancel_pending");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);

    TW t;
    tw_init(&t, 0, NULL);

    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_OK);
    EXPECT(work_is_busy(&t.w));
    EXPECT(workqueue_cancel(&t.w) == OM_OK);
    EXPECT(!work_is_busy(&t.w));

    osal_sleep_ms(100u);
    EXPECT(t.ex == 0);

    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    TEST_END();
}

/* 测试6：已完成的工作项无法取消 */
static void t_cancel_completed(void)
{
    TEST_BEGIN("cancel_completed");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t;
    tw_init(&t, 0, sem);

    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_OK);
    EXPECT(wait_sem(sem, 5000u));
    EXPECT(t.ex == 1);

    EXPECT(workqueue_cancel(&t.w) != OM_OK);

    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
    TEST_END();
}

/* 测试7：5 个工作项中取消第 3 个 */
static void t_cancel_one_of_many(void)
{
    TEST_BEGIN("cancel_one_of_many");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t[5];
    for (int i = 0; i < 5; i++)
        tw_init(&t[i], i, (i == 4) ? sem : NULL);

    for (int i = 0; i < 5; i++)
        EXPECT(workqueue_enqueue(&wq, &t[i].w) == OM_OK);
    EXPECT(workqueue_cancel(&t[2].w) == OM_OK);

    EXPECT(wait_sem(sem, 5000u));
    EXPECT(t[0].ex == 1);
    EXPECT(t[1].ex == 1);
    EXPECT(t[2].ex == 0);
    EXPECT(t[3].ex == 1);
    EXPECT(t[4].ex == 1);

    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
    TEST_END();
}

/* 测试8：取消后重新入队 */
static void t_cancel_reenqueue(void)
{
    TEST_BEGIN("cancel_reenqueue");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);

    TW t;
    tw_init(&t, 0, NULL);

    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_OK);
    EXPECT(workqueue_cancel(&t.w) == OM_OK);
    EXPECT(!work_is_busy(&t.w));

    OsalSem *sem = make_sem();
    t.s = sem;
    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_OK);
    EXPECT(wait_sem(sem, 5000u));
    EXPECT(t.ex == 1);

    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
    TEST_END();
}

/* 测试9：5 个工作项全部正常执行 */
static void t_multi(void)
{
    TEST_BEGIN("multi");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t[5];
    for (int i = 0; i < 5; i++)
        tw_init(&t[i], i, (i == 4) ? sem : NULL);

    for (int i = 0; i < 5; i++)
        EXPECT(workqueue_enqueue(&wq, &t[i].w) == OM_OK);
    EXPECT(wait_sem(sem, 5000u));

    for (int i = 0; i < 5; i++)
        EXPECT(t[i].ex == 1);

    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
    TEST_END();
}

/* 测试10：20 项压力测试 */
static void t_stress(void)
{
    TEST_BEGIN("stress (20 items)");
#define N 20
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t[N];
    for (int i = 0; i < N; i++)
        tw_init(&t[i], i, (i == N - 1) ? sem : NULL);

    for (int i = 0; i < N; i++)
        EXPECT(workqueue_enqueue(&wq, &t[i].w) == OM_OK);
    EXPECT(wait_sem(sem, 10000u));

    for (int i = 0; i < N; i++)
        EXPECT(t[i].ex == 1);

    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
#undef N
    TEST_END();
}

/* 测试11：stop 排空 */
static void t_stop_drain(void)
{
    TEST_BEGIN("stop_drain");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t;
    tw_init(&t, 42, sem);

    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_OK);
    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(t.ex == 1);
    EXPECT(!work_is_busy(&t.w));

    EXPECT(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
    TEST_END();
}

/* 测试12：停止后入队被拒绝 */
static void t_enq_stopped(void)
{
    TEST_BEGIN("enq_stopped");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);
    EXPECT(workqueue_stop(&wq) == OM_OK);

    TW t;
    tw_init(&t, 0, NULL);
    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_ERROR);

    EXPECT(workqueue_deinit(&wq) == OM_OK);
    TEST_END();
}

/* 测试13：查询函数 */
static void t_query(void)
{
    TEST_BEGIN("query");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t;
    tw_init(&t, 0, sem);

    EXPECT(!work_is_busy(&t.w));
    EXPECT(workqueue_is_empty(&wq));

    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_OK);
    EXPECT(work_is_busy(&t.w));
    EXPECT(!workqueue_is_empty(&wq));

    EXPECT(wait_sem(sem, 5000u));
    EXPECT(!work_is_busy(&t.w));

    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
    TEST_END();
}

/* 测试14：flush 排空并同步等待 */
static void t_flush(void)
{
    TEST_BEGIN("flush");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t[3];
    for (int i = 0; i < 2; i++)
        tw_init(&t[i], i, NULL);
    tw_init(&t[2], 2, sem);

    for (int i = 0; i < 3; i++)
        EXPECT(workqueue_enqueue(&wq, &t[i].w) == OM_OK);

    /* flush 应等待全部工作项完成 */
    EXPECT(workqueue_flush(&wq) == OM_OK);
    for (int i = 0; i < 3; i++)
        EXPECT(t[i].ex == 1);

    /* flush 后队列为空 */
    EXPECT(workqueue_is_empty(&wq));

    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
    TEST_END();
}

/* 测试15：work_wait_idle 等到 IDLE 才返回 */
static void t_wait_idle(void)
{
    TEST_BEGIN("wait_idle");
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    EXPECT(workqueue_init(&wq, &cfg) == OM_OK);
    EXPECT(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t;
    tw_init(&t, 7, sem);

    /* NULL / ISR 检查 */
    EXPECT(work_wait_idle(NULL, 100u) == OM_ERROR_PARAM);

    /* IDLE 状态下立即返回 */
    EXPECT(work_wait_idle(&t.w, 100u) == OM_OK);
    EXPECT(work_wait_idle(&t.w, 0u) == OM_OK);

    /* 入队→sem 知道 func 已执行→但 flags 仍可能 RUNNING */
    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_OK);
    EXPECT(wait_sem(sem, 5000u));
    EXPECT(t.ex == 1);

    /* work_wait_idle 必须等到 worker 真正写完 flags=IDLE 才返回；
     * 返回后 work_is_busy 必为 false，此时 t.w 才可安全析构/复用。 */
    EXPECT(work_wait_idle(&t.w, 5000u) == OM_OK);
    EXPECT(!work_is_busy(&t.w));

    /* 复用同一 work 再次入队 */
    EXPECT(workqueue_enqueue(&wq, &t.w) == OM_OK);
    EXPECT(wait_sem(sem, 5000u));
    EXPECT(t.ex == 2);
    EXPECT(work_wait_idle(&t.w, 5000u) == OM_OK);

    EXPECT(workqueue_stop(&wq) == OM_OK);
    EXPECT(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
    TEST_END();
}

/* --------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------- */

int main(void)
{
    printf("=== Workqueue Host Test ===\n\n");

    /** 显式归零：全局变量默认零初始化，但显式重置便于将来 main 被复用（如 host 框架重跑）。 */
    g_total = 0;
    g_failed = 0;
    g_current_test = 0;

    t_null_params();
    t_lifecycle();
    t_basic();
    t_dedup();
    t_cancel_pending();
    t_cancel_completed();
    t_cancel_one_of_many();
    t_cancel_reenqueue();
    t_multi();
    t_stress();
    t_stop_drain();
    t_enq_stopped();
    t_query();
    t_flush();
    t_wait_idle();

    printf("\n=== Results: %d/%d passed",
           g_total - g_failed, g_total);
    if (g_failed)
    {
        printf(" (%d FAILED)", g_failed);
    }
    printf(" ===\n");

    return g_failed ? 1 : 0;
}

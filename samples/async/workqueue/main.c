/**
 * @file    main.c
 * @brief   Workqueue 独立测试例程（MCU / FreeRTOS 目标）
 * @details
 * 本例程覆盖 Workqueue 的核心合同——生命周期、入队/去重、取消、排空。
 * 测试结果存储在全局 volatile 结构体中，可通过调试器检查。
 *
 * 观测变量：
 *   - g_result.total   : 累计断言数量
 *   - g_result.failed  : 失败断言数量
 *   - g_result.done    : 测试是否已执行完成（1 表示完成）
 */

#include "async/workqueue.h"
#include "core/om_init.h"
#include "data_struct/corelist.h"
#include "osal/osal.h"
#include "osal/osal_config.h"
#include "sync/completion.h"

/* --------------------------------------------------------------------------
 * Workqueue 实例分配
 *
 * 结构体定义已暴露在头文件中，可直接声明使用。
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * 测试结果追踪
 * -------------------------------------------------------------------------- */

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} TestResult;

static TestResult g_result;

static void expect(int condition)
{
    g_result.total++;
    if (!condition)
        g_result.failed++;
}

/* --------------------------------------------------------------------------
 * 测试工作项 —— handler 完成时 post semaphore
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
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_enqueue(NULL, NULL) == OM_ERROR_PARAM);
    expect(workqueue_enqueue(&wq, NULL) == OM_ERROR_PARAM);
    expect(workqueue_cancel(NULL) == OM_ERROR_PARAM);
    expect(workqueue_init(NULL, &cfg) == OM_ERROR_PARAM);
    expect(workqueue_init(&wq, NULL) == OM_ERROR_PARAM);
    expect(workqueue_deinit(NULL) == OM_ERROR_PARAM);
    expect(workqueue_start(NULL) == OM_ERROR_PARAM);
    expect(workqueue_stop(NULL) == OM_ERROR_PARAM);
    expect(workqueue_get_state(NULL) == WORKQUEUE_STATE_UNINIT);
    expect(workqueue_is_empty(NULL) == true);
    expect(workqueue_deinit(&wq) == OM_OK);
}

/* 测试2：生命周期状态机 IDLE→RUNNING→STOPPING→IDLE */
static void t_lifecycle(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_get_state(&wq) == WORKQUEUE_STATE_UNINIT);
    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_get_state(&wq) == WORKQUEUE_STATE_IDLE);
    expect(workqueue_start(&wq) == OM_OK);
    expect(workqueue_get_state(&wq) == WORKQUEUE_STATE_RUNNING);
    expect(workqueue_start(&wq) != OM_OK); /* 重复 start 被拒 */
    expect(workqueue_stop(&wq) == OM_OK);
    expect(workqueue_get_state(&wq) == WORKQUEUE_STATE_IDLE);
    expect(workqueue_stop(&wq) != OM_OK);  /* 重复 stop 被拒 */
    expect(workqueue_start(&wq) == OM_OK); /* stop 后可重新 start */
    expect(workqueue_get_state(&wq) == WORKQUEUE_STATE_RUNNING);
    expect(workqueue_stop(&wq) == OM_OK);
    expect(workqueue_deinit(&wq) == OM_OK);
}

/* 测试3：基本入队→执行 */
static void t_basic(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t;
    tw_init(&t, 42, sem);

    expect(workqueue_enqueue(&wq, &t.w) == OM_OK);
    expect(wait_sem(sem, 5000u));
    expect(t.ex == 1);

    expect(workqueue_stop(&wq) == OM_OK);
    expect(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
}

/* 测试4：去重——重复入队返回 OM_ERROR_BUSY，仅执行一次 */
static void t_dedup(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t;
    tw_init(&t, 0, sem);

    expect(workqueue_enqueue(&wq, &t.w) == OM_OK);
    expect(workqueue_enqueue(&wq, &t.w) == OM_ERROR_BUSY); /* 去重 */
    expect(wait_sem(sem, 5000u));
    expect(t.ex == 1); /* 只执行一次 */

    /* 完成后可重新入队 */
    expect(workqueue_enqueue(&wq, &t.w) == OM_OK);

    expect(workqueue_stop(&wq) == OM_OK);
    expect(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
}

/* 测试5：取消队列中尚未执行的工作项 */
static void t_cancel_pending(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_start(&wq) == OM_OK);

    TW t;
    tw_init(&t, 0, NULL);

    expect(workqueue_enqueue(&wq, &t.w) == OM_OK);
    expect(work_is_busy(&t.w));
    expect(workqueue_cancel(&t.w) == OM_OK);
    expect(!work_is_busy(&t.w));

    /* 给 worker 线程一点时间排空队列（工作项已被移除） */
    osal_sleep_ms(100u);
    expect(t.ex == 0); /* 未执行 */

    expect(workqueue_stop(&wq) == OM_OK);
    expect(workqueue_deinit(&wq) == OM_OK);
}

/* 测试6：已完成的工作项无法取消 */
static void t_cancel_completed(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t;
    tw_init(&t, 0, sem);

    expect(workqueue_enqueue(&wq, &t.w) == OM_OK);
    expect(wait_sem(sem, 5000u));
    expect(t.ex == 1);

    /* 已完成工作项不可取消 */
    expect(workqueue_cancel(&t.w) != OM_OK);

    expect(workqueue_stop(&wq) == OM_OK);
    expect(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
}

/* 测试7：5 个工作项中取消第 3 个，其余正常执行 */
static void t_cancel_one_of_many(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t[5];
    int i;
    for (i = 0; i < 5; i++)
        tw_init(&t[i], i, (i == 4) ? sem : NULL);

    for (i = 0; i < 5; i++)
        expect(workqueue_enqueue(&wq, &t[i].w) == OM_OK);
    expect(workqueue_cancel(&t[2].w) == OM_OK); /* 取消第 3 个 */

    expect(wait_sem(sem, 5000u));
    expect(t[0].ex == 1);
    expect(t[1].ex == 1);
    expect(t[2].ex == 0); /* 已取消 */
    expect(t[3].ex == 1);
    expect(t[4].ex == 1);

    expect(workqueue_stop(&wq) == OM_OK);
    expect(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
}

/* 测试8：取消后重新入队，正常执行 */
static void t_cancel_reenqueue(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_start(&wq) == OM_OK);

    TW t;
    tw_init(&t, 0, NULL);

    expect(workqueue_enqueue(&wq, &t.w) == OM_OK);
    expect(workqueue_cancel(&t.w) == OM_OK);
    expect(!work_is_busy(&t.w));

    /* 取消后重新入队 */
    OsalSem *sem = make_sem();
    t.s = sem;
    expect(workqueue_enqueue(&wq, &t.w) == OM_OK);
    expect(wait_sem(sem, 5000u));
    expect(t.ex == 1);

    expect(workqueue_stop(&wq) == OM_OK);
    expect(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
}

/* 测试9：5 个工作项全部正常执行 */
static void t_multi(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t[5];
    int i;
    for (i = 0; i < 5; i++)
        tw_init(&t[i], i, (i == 4) ? sem : NULL);

    for (i = 0; i < 5; i++)
        expect(workqueue_enqueue(&wq, &t[i].w) == OM_OK);
    expect(wait_sem(sem, 5000u));

    for (i = 0; i < 5; i++)
        expect(t[i].ex == 1);

    expect(workqueue_stop(&wq) == OM_OK);
    expect(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
}

/* 测试10：20 项压力测试 */
static void t_stress(void)
{
#define N 20
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t[N];
    int i;
    for (i = 0; i < N; i++)
        tw_init(&t[i], i, (i == N - 1) ? sem : NULL);

    for (i = 0; i < N; i++)
        expect(workqueue_enqueue(&wq, &t[i].w) == OM_OK);
    expect(wait_sem(sem, 10000u));

    for (i = 0; i < N; i++)
        expect(t[i].ex == 1);

    expect(workqueue_stop(&wq) == OM_OK);
    expect(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
#undef N
}

/* 测试11：stop 排空——入队后立即 stop，工作项应被执行 */
static void t_stop_drain(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t;
    tw_init(&t, 42, sem);

    expect(workqueue_enqueue(&wq, &t.w) == OM_OK);
    /* stop 会排空并等待 worker —— stop 返回后工作项必定已执行 */
    expect(workqueue_stop(&wq) == OM_OK);
    expect(t.ex == 1);
    expect(!work_is_busy(&t.w));

    expect(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
}

/* 测试12：停止后入队被拒绝 */
static void t_enq_stopped(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_start(&wq) == OM_OK);
    expect(workqueue_stop(&wq) == OM_OK);

    TW t;
    tw_init(&t, 0, NULL);
    expect(workqueue_enqueue(&wq, &t.w) == OM_ERROR); /* 非 RUNNING 状态 */

    expect(workqueue_deinit(&wq) == OM_OK);
}

/* 测试13：查询函数 —— work_is_busy / workqueue_is_empty */
static void t_query(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_start(&wq) == OM_OK);

    OsalSem *sem = make_sem();
    TW t;
    tw_init(&t, 0, sem);

    expect(!work_is_busy(&t.w));
    expect(workqueue_is_empty(&wq));

    expect(workqueue_enqueue(&wq, &t.w) == OM_OK);
    expect(work_is_busy(&t.w));
    expect(!workqueue_is_empty(&wq));

    expect(wait_sem(sem, 5000u));
    expect(!work_is_busy(&t.w));

    expect(workqueue_stop(&wq) == OM_OK);
    expect(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
}

/* 测试14：work_wait_idle —— 等 worker 真正释放 work 后再析构/复用 */
static void t_wait_idle(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"t", 8192u, 2u};

    expect(workqueue_init(&wq, &cfg) == OM_OK);
    expect(workqueue_start(&wq) == OM_OK);

    /* NULL 参数返回 PARAM */
    expect(work_wait_idle(NULL, 100u) == OM_ERROR_PARAM);

    OsalSem *sem = make_sem();
    TW t;
    tw_init(&t, 7, sem);

    /* IDLE 状态立即返回 OK，timeout=0 也 OK */
    expect(work_wait_idle(&t.w, 100u) == OM_OK);
    expect(work_wait_idle(&t.w, 0u) == OM_OK);

    /* 入队 → sem 知道 func 已执行，但 worker 写 flags=IDLE 可能尚未完成 */
    expect(workqueue_enqueue(&wq, &t.w) == OM_OK);
    expect(wait_sem(sem, 5000u));
    expect(t.ex == 1);

    /* work_wait_idle 必须等到 worker 真正写完 flags=IDLE 才返回；
     * 返回后 work_is_busy 必为 false，此时 t.w 才可安全析构/复用。 */
    expect(work_wait_idle(&t.w, 5000u) == OM_OK);
    expect(!work_is_busy(&t.w));

    /* 复用同一 work 再次入队 */
    expect(workqueue_enqueue(&wq, &t.w) == OM_OK);
    expect(wait_sem(sem, 5000u));
    expect(t.ex == 2);
    expect(work_wait_idle(&t.w, 5000u) == OM_OK);

    expect(workqueue_stop(&wq) == OM_OK);
    expect(workqueue_deinit(&wq) == OM_OK);
    osal_sem_delete(sem);
}

/* --------------------------------------------------------------------------
 * 测试线程入口 —— 顺序执行全部用例
 * -------------------------------------------------------------------------- */

static void test_thread_entry(void *arg)
{
    (void)arg;

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
    t_wait_idle();

    g_result.done = 1u;
    for (;;)
        osal_sleep_ms(1000u);
}

/* --------------------------------------------------------------------------
 * 例程入口
 * -------------------------------------------------------------------------- */

/* app 自身启动设置：经 OM_INIT_APPLICATION 分散加载，init 线程（调度器后）自动调用 */
static OmRet wq_app_setup(void)
{
    OsalThreadAttr test_attr = {
        "wq_test",
        4096u * OSAL_STACK_WORD_BYTES,
        3u,
    };
    OsalThread *test_thread = NULL;

    if (osal_thread_create(&test_thread, &test_attr,
                           test_thread_entry, NULL) != OSAL_OK)
        return OM_ERR_NO_MEM;

    return OM_OK;
}
OM_INIT_APPLICATION(wq_app_setup);

/**
 * @file   osal_none_test.c
 * @brief  osal-none 端口 host 单流语料（裸机行为真实模拟）
 *
 * 覆盖（单流顺序语义，非并发语料）：
 * - T0 时间面：now 单调 / sleep 时长下界 / sleep(0) / WAIT_FOREVER 拒绝 /
 *   delay_until 周期推进与过期统计；
 * - T1 信号量：三态超时映射 / 满 post / 计数 / 误用拒绝 / 模拟 ISR 唤醒
 *   （辅助线程 isr_enter → post_from_isr → 忙等者返回——真并发 + 临界区互斥）；
 * - T2 互斥锁：0/1 / 重复加锁 / 空锁解锁 / 非 owner（单流恒 self）语义；
 * - T3 线程占位族：create 直调 / self 单例 / terminate / join / kernel_start；
 * - T4 内存：heap_1 分配 / 耗尽 / 对齐 / free no-op；
 * - T7 定时器：单次 / 周期（等待点推进）/ 回调内 stop/delete / 回调内阻塞拒绝。
 *
 * 退出码 0 = 全绿。
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "osal/osal.h"

#include "async/workqueue.h"
#include "osal_none_internal.h"

static int g_pass;
static int g_fail;

#define CHECK(cond, ...)                    \
    do                                      \
    {                                       \
        if (cond)                           \
        {                                   \
            g_pass++;                       \
            printf("  PASS: " __VA_ARGS__); \
            printf("\n");                   \
        }                                   \
        else                                \
        {                                   \
            g_fail++;                       \
            printf("  FAIL: " __VA_ARGS__); \
            printf("\n");                   \
        }                                   \
    } while (0)

static OsalTimeMs t_now(void)
{
    return osal_time_now_monotonic();
}

/*===========================================================================
 * T1 辅助：模拟 ISR 辅助线程（真并发：与主执行流忙等并行，临界区互斥）
 *===========================================================================*/

typedef struct
{
    OsalSem *sem;
    OsalStatus post_result;
} IsrPostArg;

static DWORD WINAPI isr_poster_thread(LPVOID param)
{
    IsrPostArg *arg = (IsrPostArg *)param;

    Sleep(30);
    osal_none_arch_sim_isr_enter();
    arg->post_result = osal_sem_post_from_isr(arg->sem);
    osal_none_arch_sim_isr_exit();
    return 0;
}

static void test_time(void)
{
    OsalTimeMs t0, t1;
    OsalTimeMs cursor = 0u;
    uint32_t missed = 0u;

    printf("[T0 time]\n");
    CHECK(osal_sleep_ms(0) == OSAL_OK, "sleep(0) immediate");
    CHECK(osal_sleep_ms(OSAL_WAIT_FOREVER) == OSAL_INVALID, "sleep(FOREVER) rejected");

    t0 = t_now();
    CHECK(osal_sleep_ms(30) == OSAL_OK, "sleep(30) ok");
    t1 = t_now();
    CHECK((uint32_t)(t1 - t0) >= 28u, "sleep(30) elapsed >= 28ms (got %u)", (unsigned)(t1 - t0));

    osal_none_arch_sim_isr_enter();
    CHECK(osal_sleep_ms(1) == OSAL_INVALID, "sleep in ISR rejected");
    osal_none_arch_sim_isr_exit();

    /* delay_until：首调初始化游标并立即返回 */
    CHECK(osal_delay_until(&cursor, 20u, NULL) == OSAL_OK && cursor != 0u, "delay_until first call arms cursor");
    t0 = t_now();
    CHECK(osal_delay_until(&cursor, 20u, NULL) == OSAL_OK, "delay_until cycle 1");
    CHECK(osal_delay_until(&cursor, 20u, NULL) == OSAL_OK, "delay_until cycle 2");
    t1 = t_now();
    CHECK((uint32_t)(t1 - t0) >= 38u, "delay_until 2 periods elapsed >= 38ms (got %u)", (unsigned)(t1 - t0));

    /* 过期追赶：游标落后 50ms → 统计错过周期且本次不睡 */
    cursor = (OsalTimeMs)(t_now() - 50u);
    CHECK(osal_delay_until(&cursor, 15u, &missed) == OSAL_OK, "delay_until catch-up ok");
    CHECK(missed == 4u, "missed periods = 50/15+1 = 4 (got %u)", (unsigned)missed);
    CHECK(osal_delay_until(NULL, 15u, NULL) == OSAL_INVALID, "delay_until NULL cursor rejected");
    CHECK(osal_delay_until(&cursor, 0u, NULL) == OSAL_INVALID, "delay_until zero period rejected");
}

static void test_sem(void)
{
    OsalSem *sem = NULL;
    OsalSem *s2 = NULL;
    uint32_t count = 99u;
    OsalTimeMs t0;
    IsrPostArg arg;
    HANDLE h;

    printf("[T1 sem]\n");
    CHECK(osal_sem_create(NULL, 1u, 0u) == OSAL_INVALID, "create NULL out rejected");
    CHECK(osal_sem_create(&sem, 0u, 0u) == OSAL_INVALID, "create max=0 rejected");
    CHECK(osal_sem_create(&sem, 2u, 3u) == OSAL_INVALID, "create init>max rejected");

    CHECK(osal_sem_create(&sem, 2u, 2u) == OSAL_OK, "create(2,2) ok");
    CHECK(osal_sem_get_count(sem, &count) == OSAL_OK && count == 2u, "count == 2");
    CHECK(osal_sem_post(sem) == OSAL_NO_RESOURCE, "post at max -> NO_RESOURCE");
    CHECK(osal_sem_wait(sem, 0u) == OSAL_OK, "wait(0) takes");
    CHECK(osal_sem_wait(sem, 0u) == OSAL_OK, "wait(0) takes");
    CHECK(osal_sem_wait(sem, 0u) == OSAL_WOULD_BLOCK, "wait(0) empty -> WOULD_BLOCK");

    t0 = t_now();
    CHECK(osal_sem_wait(sem, 10u) == OSAL_TIMEOUT, "wait(10) -> TIMEOUT");
    CHECK((uint32_t)(t_now() - t0) >= 8u, "timeout elapsed >= 8ms");
    CHECK(osal_sem_post(sem) == OSAL_OK, "post ok");
    CHECK(osal_sem_wait(sem, 0u) == OSAL_OK, "wait(0) after post");

    /* 模拟 ISR 正路径：辅助线程真并发 post_from_isr 唤醒死等者 */
    CHECK(osal_sem_create(&s2, 1u, 0u) == OSAL_OK, "create s2(1,0) ok");
    arg.sem = s2;
    arg.post_result = OSAL_INTERNAL;
    h = CreateThread(NULL, 0, isr_poster_thread, &arg, 0, NULL);
    CHECK(h != NULL, "spawn ISR poster thread");
    t0 = t_now();
    CHECK(osal_sem_wait(s2, OSAL_WAIT_FOREVER) == OSAL_OK, "FOREVER wait woken by sim ISR post");
    CHECK((uint32_t)(t_now() - t0) >= 25u, "wakeup elapsed >= 25ms");
    WaitForSingleObject(h, 2000);
    CloseHandle(h);
    CHECK(arg.post_result == OSAL_OK, "ISR post returned OK");

    /* 模拟 ISR 上下文内行为 */
    osal_none_arch_sim_isr_enter();
    CHECK(osal_sem_wait(s2, 0u) == OSAL_INVALID, "wait inside ISR rejected");
    CHECK(osal_sem_post(s2) == OSAL_INVALID, "post inside ISR rejected");
    CHECK(osal_sem_post_from_isr(s2) == OSAL_OK, "post_from_isr inside ISR ok");
    CHECK(osal_sem_get_count(s2, &count) == OSAL_INVALID, "get_count inside ISR rejected");
    CHECK(osal_sem_get_count_from_isr(s2, &count) == OSAL_OK && count == 1u, "get_count_from_isr == 1");
    osal_none_arch_sim_isr_exit();

    /* 任务上下文调 from_isr = 误用拒绝 */
    CHECK(osal_sem_post_from_isr(s2) == OSAL_INVALID, "post_from_isr in task ctx rejected");
    CHECK(osal_sem_get_count_from_isr(s2, &count) == OSAL_INVALID, "get_count_from_isr in task ctx rejected");

    CHECK(osal_sem_delete(sem) == OSAL_OK, "delete ok");
    CHECK(osal_sem_wait(sem, 0u) == OSAL_INVALID, "wait after delete rejected");
    CHECK(osal_sem_delete(s2) == OSAL_OK, "delete s2 ok");
}

static void test_mutex(void)
{
    OsalMutex *mutex = NULL;
    OsalMutex *m2 = NULL;

    printf("[T2 mutex]\n");
    CHECK(osal_mutex_create(&mutex) == OSAL_OK, "create ok");
    CHECK(osal_mutex_unlock(mutex) == OSAL_INVALID, "unlock unlocked -> INVALID");
    CHECK(osal_mutex_lock(mutex, 0u) == OSAL_OK, "lock(0) ok");
    CHECK(osal_mutex_lock(mutex, 0u) == OSAL_WOULD_BLOCK, "re-lock -> WOULD_BLOCK (non-recursive)");
    CHECK(osal_mutex_lock(mutex, 5u) == OSAL_TIMEOUT, "re-lock(5) -> TIMEOUT");
    CHECK(osal_mutex_unlock(mutex) == OSAL_OK, "unlock ok");
    CHECK(osal_mutex_unlock(mutex) == OSAL_INVALID, "double unlock -> INVALID");

    osal_none_arch_sim_isr_enter();
    CHECK(osal_mutex_lock(mutex, 0u) == OSAL_INVALID, "lock inside ISR rejected");
    osal_none_arch_sim_isr_exit();

    CHECK(osal_mutex_delete(mutex) == OSAL_OK, "delete ok");
    CHECK(osal_mutex_lock(mutex, 0u) == OSAL_INVALID, "lock after delete rejected");
    CHECK(osal_mutex_create(NULL) == OSAL_INVALID, "create NULL out rejected");
    (void)m2;
}

static int g_entry_sum;

static void test_thread_entry(void *arg)
{
    g_entry_sum += (int)(intptr_t)arg;
}

static void test_thread(void)
{
    OsalThread *t1 = NULL;
    OsalThread *t2 = NULL;

    printf("[T3 thread]\n");
    g_entry_sum = 0;
    CHECK(osal_thread_create(&t1, NULL, test_thread_entry, (void *)(intptr_t)5) == OSAL_OK,
          "create direct-calls entry");
    CHECK(g_entry_sum == 5, "entry executed inline (sum=5)");
    CHECK(t1 == osal_thread_self(), "created handle == executor singleton");
    CHECK(osal_thread_self() == osal_thread_self(), "self stable");
    CHECK(osal_thread_create(NULL, NULL, test_thread_entry, NULL) == OSAL_INVALID, "create NULL out rejected");
    CHECK(osal_thread_create(&t2, NULL, NULL, NULL) == OSAL_INVALID, "create NULL entry rejected");

    osal_thread_yield();
    CHECK(osal_thread_join(t1, 10u) == OSAL_NOT_SUPPORTED, "join -> NOT_SUPPORTED (align freertos)");
    CHECK(osal_thread_terminate(t1) == OSAL_INVALID, "terminate(self) rejected (single flow)");
    CHECK(osal_thread_terminate(NULL) == OSAL_INVALID, "terminate NULL rejected");
    CHECK(osal_kernel_start() == OSAL_OK, "kernel_start no-op OK");

    osal_none_arch_sim_isr_enter();
    CHECK(osal_thread_self() == NULL, "self in ISR -> NULL");
    osal_none_arch_sim_isr_exit();
}

static void test_malloc(void)
{
    void *blocks[8300];
    int n = 0;
    size_t i;

    printf("[T4 malloc]\n");
    /* 分配到耗尽：1B 请求按 8B 步进 → 容量 65536 下上限约 8192 块
     * （前序用例已消耗少量对象分配）。上限 +1 防实现缺陷死循环。 */
    for (n = 0; n < 8300; n++)
    {
        void *p = osal_malloc(1u);
        if (!p)
        {
            break;
        }
        if (((uintptr_t)p & 7u) != 0u)
        {
            CHECK(false, "allocation misaligned");
            break;
        }
        blocks[n] = p;
    }
    CHECK(n >= 6000 && n <= 8200, "heap_1 exhausted near capacity (got %d)", n);
    CHECK(osal_malloc(1u) == NULL, "post-exhaustion malloc -> NULL (no reuse)");
    CHECK(osal_malloc(0u) == NULL, "malloc(0) -> NULL");

    /* free no-op：重复 free 无副作用，内存不回收 */
    if (n > 0)
    {
        osal_free(blocks[0]);
        osal_free(blocks[0]);
        CHECK(osal_malloc(1u) == NULL, "free no-op (still exhausted)");
    }
    osal_none_arch_sim_isr_enter();
    CHECK(osal_malloc(16u) == NULL, "malloc in ISR -> NULL");
    osal_none_arch_sim_isr_exit();

    for (i = 0; i < (size_t)n; i++)
    {
        osal_free(blocks[i]);
    }
}

/*===========================================================================
 * T7 定时器：回调经 get_id 取计数指针；等待点推进由 sleep/wait 内嵌刷新驱动
 *===========================================================================*/

static void timer_count_cb(OsalTimer *timer)
{
    int *counter = (int *)osal_timer_get_id(timer);

    if (counter)
    {
        (*counter)++;
    }
}

static void timer_block_probe_cb(OsalTimer *timer)
{
    int *probe = (int *)osal_timer_get_id(timer);

    if (probe)
    {
        *probe = (int)osal_sleep_ms(1); /* 回调内阻塞应被拒绝 */
    }
}

static int g_selfstop;

static void timer_selfstop_cb(OsalTimer *timer)
{
    g_selfstop++;
    osal_timer_stop(timer, 0u); /* 回调内 stop 自身：抑制周期重挂 */
}

static void test_timer(void)
{
    OsalTimer *t1 = NULL;
    OsalTimer *t2 = NULL;
    OsalTimer *t3 = NULL;
    int fired = 0;
    int probe = 0;

    printf("[T7 timer]\n");
    CHECK(osal_timer_create(&t1, NULL, 0u, OSAL_TIMER_ONE_SHOT, NULL, timer_count_cb) == OSAL_INVALID,
          "create period=0 rejected");
    CHECK(osal_timer_create(&t1, NULL, 30u, OSAL_TIMER_ONE_SHOT, NULL, NULL) == OSAL_INVALID,
          "create NULL cb rejected");

    CHECK(osal_timer_create(&t1, NULL, 30u, OSAL_TIMER_ONE_SHOT, &fired, timer_count_cb) == OSAL_OK,
          "create one-shot 30ms");
    CHECK(osal_timer_start(t1, OSAL_WAIT_FOREVER) == OSAL_OK, "start ok");
    CHECK(osal_sleep_ms(5) == OSAL_OK, "sleep 5");
    CHECK(fired == 0, "not fired before deadline");
    CHECK(osal_sleep_ms(40) == OSAL_OK, "sleep 40");
    CHECK(fired == 1, "one-shot fired once");

    fired = 0;
    CHECK(osal_timer_create(&t2, NULL, 15u, OSAL_TIMER_PERIODIC, &fired, timer_count_cb) == OSAL_OK,
          "create periodic 15ms");
    CHECK(osal_timer_start(t2, 0u) == OSAL_OK, "start periodic");
    CHECK(osal_sleep_ms(70) == OSAL_OK, "sleep 70");
    CHECK(fired >= 3 && fired <= 6, "periodic fired 3..6 times in 70ms (got %d)", fired);
    CHECK(osal_timer_stop(t2, 0u) == OSAL_OK, "stop periodic");
    CHECK(osal_sleep_ms(40) == OSAL_OK, "sleep 40 after stop");
    CHECK(fired >= 3 && fired <= 6, "no firing after stop (got %d)", fired);

    /* 周期定时器回调内 stop 自身 = 取消后续周期（suppress 路径） */
    g_selfstop = 0;
    CHECK(osal_timer_create(&t3, NULL, 15u, OSAL_TIMER_PERIODIC, NULL, timer_selfstop_cb) == OSAL_OK,
          "create self-stop periodic");
    osal_timer_start(t3, 0u);
    CHECK(osal_sleep_ms(50) == OSAL_OK, "sleep 50");
    CHECK(g_selfstop == 1, "self-stop after first fire (got %d)", g_selfstop);
    CHECK(osal_timer_delete(t3, 0u) == OSAL_OK, "delete self-stop timer");

    /* 回调内阻塞调用被拒绝（OSAL_INVALID 入 probe） */
    probe = 0;
    CHECK(osal_timer_create(&t3, NULL, 10u, OSAL_TIMER_PERIODIC, &probe, timer_block_probe_cb) == OSAL_OK,
          "create block-probe periodic");
    osal_timer_start(t3, 0u);
    CHECK(osal_sleep_ms(35) == OSAL_OK, "sleep 35 (drives refresh)");
    CHECK(probe == OSAL_INVALID, "blocking call inside timer cb rejected (got %d)", probe);
    osal_timer_stop(t3, 0u);
    CHECK(osal_timer_delete(t3, 0u) == OSAL_OK, "delete block-probe");

    osal_none_arch_sim_isr_enter();
    CHECK(osal_timer_start(t1, 0u) == OSAL_INVALID, "timer start in ISR rejected");
    osal_none_arch_sim_isr_exit();
    CHECK(osal_timer_delete(t1, 0u) == OSAL_OK, "delete one-shot");
}

/*===========================================================================
 * T8 workqueue 坍缩语义（无后台执行者：入队即执行）
 *===========================================================================*/

static int g_wq_sum;

static void wq_add_cb(Work *work)
{
    g_wq_sum += (int)(intptr_t)work->data;
}

typedef struct
{
    Workqueue *wq;
    OmRet result;
} WqReentryArg;

static void wq_reentry_cb(Work *work)
{
    WqReentryArg *arg = (WqReentryArg *)work->data;

    arg->result = workqueue_enqueue(arg->wq, work); /* 执行中重入同一 work */
}

static void test_workqueue(void)
{
    Workqueue wq = {0};
    WorkqueueConfig cfg = {"twq", 2048u, 1u};
    Work work;
    WqReentryArg rearg;

    printf("[T8 workqueue collapse]\n");
    CHECK(workqueue_init(NULL, &cfg) == OM_ERROR_PARAM, "init NULL wq rejected");
    CHECK(workqueue_init(&wq, NULL) == OM_ERROR_PARAM, "init NULL cfg rejected");
    CHECK(workqueue_init(&wq, &cfg) == OM_OK, "init ok");
    CHECK(workqueue_get_state(&wq) == WORKQUEUE_STATE_IDLE, "state IDLE after init");
    CHECK(workqueue_init(&wq, &cfg) == OM_ERROR, "double init rejected");
    CHECK(workqueue_start(&wq) == OM_OK, "start ok (no-op state machine)");
    CHECK(workqueue_get_state(&wq) == WORKQUEUE_STATE_RUNNING, "state RUNNING");

    g_wq_sum = 0;
    work_init(&work, wq_add_cb, (void *)(intptr_t)3);
    CHECK(workqueue_enqueue(&wq, &work) == OM_OK, "enqueue ok");
    CHECK(g_wq_sum == 3, "work executed inline at submit (sum=3)");
    CHECK(!work_is_busy(&work), "work IDLE right after enqueue returns");
    CHECK(work_wait_idle(&work, 0u) == OM_OK, "work_wait_idle immediate");
    CHECK(workqueue_enqueue(&wq, &work) == OM_OK, "re-enqueue ok (IDLE again)");
    CHECK(g_wq_sum == 6, "second inline execution (sum=6)");
    CHECK(workqueue_cancel(&work) == OM_ERROR, "cancel idle work -> not pending");
    CHECK(workqueue_flush(&wq) == OM_OK, "flush ok (nothing pending)");

    /* ISR 上下文入队：无执行载体，显式拒绝 */
    osal_none_arch_sim_isr_enter();
    CHECK(workqueue_enqueue(&wq, &work) == OM_ERR_NOT_SUPPORTED, "ISR enqueue rejected");
    osal_none_arch_sim_isr_exit();

    /* 执行中重入同一 work：RUNNING 检测 → BUSY */
    rearg.wq = &wq;
    rearg.result = OM_OK;
    work_init(&work, wq_reentry_cb, &rearg);
    CHECK(workqueue_enqueue(&wq, &work) == OM_OK, "reentry probe enqueue ok");
    CHECK(rearg.result == OM_ERROR_BUSY, "self re-enqueue inside func -> BUSY");

    CHECK(workqueue_stop(&wq) == OM_OK, "stop ok");
    CHECK(workqueue_get_state(&wq) == WORKQUEUE_STATE_IDLE, "state IDLE after stop");
    CHECK(workqueue_enqueue(&wq, &work) == OM_ERROR, "enqueue after stop rejected");
    CHECK(workqueue_deinit(&wq) == OM_OK, "deinit ok");
    CHECK(workqueue_get_state(&wq) == WORKQUEUE_STATE_UNINIT, "state UNINIT");
    CHECK(workqueue_deinit(&wq) == OM_ERROR, "deinit twice rejected");

    /* 可循环：UNINIT → init → start → stop → deinit */
    CHECK(workqueue_init(&wq, &cfg) == OM_OK && workqueue_start(&wq) == OM_OK &&
              workqueue_stop(&wq) == OM_OK && workqueue_deinit(&wq) == OM_OK,
          "full lifecycle reusable");
}

int main(void)
{
    printf("=== osal-none host corpus start ===\n");
    printf("in_isr(task) = %d\n", osal_is_in_isr());

    test_time();
    test_sem();
    test_mutex();
    test_thread();
    test_timer();
    test_workqueue();
    test_malloc(); /* 堆耗尽用例放最后：heap_1 不回收，先耗尽会让后续分配失败 */

    printf("=== osal-none host corpus: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}

/**
 * @file main.c
 * @brief SYNC completion 独立例程
 * @details
 * 本例程覆盖 completion 的基础合同与高并发压力场景。
 * - 基础语义：单等待者、one-shot、done 先于 wait、重复 done 返回 BUSY、timeout 语义。
 * - 压力语义：
 *   - 组8：多线程并发 done + 单线程 wait（done 与 waiter 三种优先级关系）。
 *   - 组9：线程模拟 ISR done + 线程 wait（不依赖芯片 IRQ 头文件）。
 * - 测试模型（单模式）：
 *   - 功能正确性轨道：固定轮次 + 严格 one-shot 计数关系。
 *   - 并发压力轨道：固定时间窗 + 活性阈值（>=1）+ 竞争证据。
 * - 判定标准：
 *   - 全局通过：`g_result.done == 1` 且 `g_result.failed == 0`。
 *   - 压测公共通过条件：
 *     1) waiter/producer 均按期退出；
 *     2) `waiter_wait_err == 0` 且 done 侧 `*_done_err == 0`。
 *   - 功能正确性轨道额外条件：
 *     3) `*_done_ok >= waiter_wait_ok` 且 `*_done_ok - waiter_wait_ok <= 1`；
 *     4) `waiter_wait_ok == target_wait_ok`。
 *   - 并发压力轨道额外条件：
 *     3) `waiter_wait_ok >= min_wait_ok`（活性阈值，默认 1）；
 *     4) `done_busy > 0`（证明存在有效竞争）。
 */
#include "osal/osal.h"
#include "osal/osal_config.h"
#include "sync/completion.h"

/**
 * @brief completion 线程模拟 ISR 专项压测开关
 * @details
 * - `0`：关闭组 9（线程模拟 ISR 路径专项）
 * - `1`：启用组 9（默认）
 */
#ifndef COMPLETION_ISR_STRESS_ENABLE
#define COMPLETION_ISR_STRESS_ENABLE 1u
#endif

#if (COMPLETION_ISR_STRESS_ENABLE != 0u) && (COMPLETION_ISR_STRESS_ENABLE != 1u)
#error "COMPLETION_ISR_STRESS_ENABLE only accepts 0 or 1"
#endif

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t fatalFailed;
    volatile uint32_t statFailed;
    volatile uint32_t done;
} SyncCompletionTestResult;

enum {
    /* 并发 done 线程数上限，用于控制竞争压力 */
    COMPLETION_STRESS_DONE_WORKERS = 4u,
    /* 组8中 done > waiter 压力子场景使用的 done 线程数 */
    COMPLETION_STRESS_DONE_WORKERS_GT_CASE = 2u,
    /* 功能正确性轨道（固定轮次）：done < waiter */
    COMPLETION_STRESS_FUNCTIONAL_TARGET_ROUNDS_LT = 20000u,
    /* 功能正确性轨道（固定轮次）：done == waiter */
    COMPLETION_STRESS_FUNCTIONAL_TARGET_ROUNDS_EQ = 20000u,
    /* 功能正确性轨道单子场景超时保护（毫秒） */
    COMPLETION_STRESS_FUNCTIONAL_CASE_TIMEOUT_MS = 120000u,
    /* 并发压力轨道时间窗（毫秒） */
    COMPLETION_STRESS_PRESSURE_WINDOW_MS = 20000u,
    /* 并发压力轨道活性阈值（仅要求存在最小前进，不做性能评级） */
    COMPLETION_STRESS_PRESSURE_MIN_WAIT_OK = 1u,
    /* 不设轮次上限（由 stop 收敛） */
    COMPLETION_STRESS_TARGET_ROUNDS_UNBOUNDED = 0xFFFFFFFFu,
    /* 组8停机收尾等待上限（毫秒） */
    COMPLETION_STRESS_STOP_TIMEOUT_MS = 5000u,
    COMPLETION_STRESS_SHUTDOWN_TIMEOUT_MS = 2000u,
    /* done 线程让出 CPU 的节流掩码（每 64 次循环让出一次） */
    COMPLETION_STRESS_WORKER_YIELD_MASK = 0x003Fu
};

enum {
    /* 线程模拟 ISR 压力轨道时间窗（毫秒） */
    COMPLETION_ISR_STRESS_WINDOW_MS = 20000u,
    /* 线程模拟 ISR 压力轨道活性阈值（仅要求存在最小前进） */
    COMPLETION_ISR_STRESS_MIN_WAIT_OK = 1u,
    /* 线程模拟 ISR 专项停机收尾等待上限（毫秒） */
    COMPLETION_ISR_STRESS_STOP_TIMEOUT_MS = 5000u,
    /* 触发线程让出 CPU 的节流掩码（每 1024 次循环让出一次） */
    COMPLETION_ISR_STRESS_TRIGGER_YIELD_MASK = 0x03FFu,
};

enum {
    /* 组8/组9的 waiter 基准优先级 */
    COMPLETION_STRESS_WAITER_PRIORITY_BASE = 3u,
};

typedef enum {
    COMPLETION_STRESS_WAITER_IDLE = 0,
    COMPLETION_STRESS_WAITER_RUNNING,
    COMPLETION_STRESS_WAITER_DONE_OK,
    COMPLETION_STRESS_WAITER_DONE_ERROR
} CompletionStressWaiterState;

typedef struct
{
    volatile uint32_t start;
    volatile uint32_t stop;
    volatile uint32_t targetWaitOk;
    volatile uint32_t waiterStarted;
    volatile uint32_t waiterDone;
    volatile CompletionStressWaiterState waiterState;
    volatile uint32_t waiterWaitOk;
    volatile uint32_t waiterWaitErr;
    volatile OmRet waiterLastRet;
    volatile uint32_t workerDone[COMPLETION_STRESS_DONE_WORKERS];
    volatile uint32_t workerDoneOk[COMPLETION_STRESS_DONE_WORKERS];
    volatile uint32_t workerDoneBusy[COMPLETION_STRESS_DONE_WORKERS];
    volatile uint32_t workerDoneErr[COMPLETION_STRESS_DONE_WORKERS];
} CompletionStressCtx;

typedef struct
{
    volatile uint32_t start;
    volatile uint32_t stop;
    volatile uint32_t targetWaitOk;
    volatile uint32_t waiterStarted;
    volatile uint32_t waiterDone;
    volatile CompletionStressWaiterState waiterState;
    volatile uint32_t waiterWaitOk;
    volatile uint32_t waiterWaitErr;
    volatile OmRet waiterLastRet;
    volatile uint32_t triggerThreadDone;
    volatile uint32_t isrDoneOk;
    volatile uint32_t isrDoneBusy;
    volatile uint32_t isrDoneErr;
} CompletionIsrStressCtx;

typedef struct
{
    uint32_t doneOk;
    uint32_t doneBusy;
    uint32_t doneErr;
} CompletionDoneStats;

static Completion g_completion                                           = {0};
static OsalThread* g_test_thread                                         = NULL;
static OsalThread* g_waiter_thread                                       = NULL;
static OsalThread* g_done_thread                                         = NULL;
static SyncCompletionTestResult g_result                              = {0u, 0u, 0u, 0u, 0u};
static CompletionStressCtx g_stress                                    = {0};
static CompletionIsrStressCtx g_isr_stress                            = {0};
static OsalThread* g_stress_waiter_thread                                = NULL;
static OsalThread* g_stress_done_threads[COMPLETION_STRESS_DONE_WORKERS] = {0};
#if (COMPLETION_ISR_STRESS_ENABLE != 0u)
static OsalThread* g_isr_stress_waiter_thread = NULL;
static OsalThread* g_isr_trigger_thread       = NULL;
#endif

static volatile uint32_t g_waiter_started   = 0u;
static volatile uint32_t g_waiter_waiting   = 0u;
static volatile uint32_t g_waiter_done      = 0u;
static volatile OmRet g_waiter_ret      = OM_ERROR;
static volatile uint32_t g_done_thread_done = 0u;
static volatile OmRet g_done_thread_ret = OM_ERROR;
static volatile uint32_t g_done_delay_ms    = 0u;
static volatile uint32_t g_test_infra_abort = 0u;

typedef enum
{
    COMPLETION_ASSERT_FATAL = 0,
    COMPLETION_ASSERT_STAT
} CompletionAssertLevel;

static void completion_expect_level(int condition, CompletionAssertLevel level)
{
    g_result.total++;
    if (condition)
        return;

    if (level == COMPLETION_ASSERT_STAT) {
        g_result.statFailed++;
        return;
    }

    g_result.fatalFailed++;
    g_result.failed++;
}

/**
 * @brief 简单断言计数器
 * @param condition 非 0 表示断言通过
 */
static void completion_expect(int condition)
{
    g_result.total++;
    if (!condition)
        g_result.failed++;
}

/**
 * @brief 等待某个标志位为 1（超时失败）
 */
static int completion_wait_flag(volatile uint32_t *flag, uint32_t timeout_ms)
{
    OsalTimeMs start_ms = osal_time_now_monotonic();
    OsalTimeMs deadline_ms = start_ms + timeout_ms;

    while (*flag == 0u) {
        if (!osal_time_before(osal_time_now_monotonic(), deadline_ms))
            return 0;
        (void)osal_sleep_ms(1u);
    }
    return 1;
}

/**
 * @brief 等待 completion 进入指定状态（用于消除测试握手竞争）
 */
static int completion_wait_status(volatile CompStatus *status, CompStatus expected, uint32_t timeout_ms)
{
    OsalTimeMs start_ms = osal_time_now_monotonic();
    OsalTimeMs deadline_ms = start_ms + timeout_ms;

    while (*status != expected) {
        if (!osal_time_before(osal_time_now_monotonic(), deadline_ms))
            return 0;
        (void)osal_sleep_ms(1u);
    }
    return 1;
}

/**
 * @brief 在停机阶段循环补发 done，确保 waiter 从 WAIT_FOREVER 返回
 * @param completion completion 对象
 * @param waiter_done waiter 退出标志
 * @param timeout_ms 收尾上限（毫秒）
 * @param force_done_stats 非 NULL 时统计收尾阶段 done 返回值
 */
static int completion_force_wake_waiter(
    Completion* completion,
    volatile uint32_t *waiter_done,
    uint32_t timeout_ms,
    CompletionDoneStats *force_done_stats)
{
    OsalTimeMs start_ms = osal_time_now_monotonic();
    OsalTimeMs deadline_ms = start_ms + timeout_ms;

    while (*waiter_done == 0u) {
        OmRet done_ret = completion_done(completion);
        if (force_done_stats) {
            if (done_ret == OM_OK)
                force_done_stats->doneOk++;
            else if (done_ret == OM_ERROR_BUSY)
                force_done_stats->doneBusy++;
            else
                force_done_stats->doneErr++;
        }

        (void)osal_sleep_ms(1u);
        if (!osal_time_before(osal_time_now_monotonic(), deadline_ms))
            break;
    }
    return (*waiter_done != 0u) ? 1 : 0;
}

/**
 * @brief 获取当前端口可用的最高线程优先级
 */
static uint32_t completion_priority_max(void)
{
    if (OSAL_PRIORITY_MAX == 0u)
        return 0u;
    return OSAL_PRIORITY_MAX - 1u;
}

/**
 * @brief 将输入优先级收敛到当前端口有效范围
 */
static uint32_t completion_priority_clamp(uint32_t priority)
{
    uint32_t max_priority = completion_priority_max();

    if (priority > max_priority)
        return max_priority;
    return priority;
}

/**
 * @brief 获取组8/组9的 waiter 优先级
 */
static uint32_t completion_priority_stress_waiter(void)
{
    return completion_priority_clamp(COMPLETION_STRESS_WAITER_PRIORITY_BASE);
}

/**
 * @brief 获取组8中 done < waiter 的优先级
 */
static uint32_t completion_priority_stress_done_lt(void)
{
    uint32_t waiter_priority = completion_priority_stress_waiter();

    if (waiter_priority > 1u)
        return waiter_priority - 1u;
    return waiter_priority;
}

/**
 * @brief 获取组8中 done == waiter 的优先级
 */
static uint32_t completion_priority_stress_done_eq(void)
{
    return completion_priority_stress_waiter();
}

/**
 * @brief 获取组8中 done > waiter 的优先级
 */
static uint32_t completion_priority_stress_done_gt(void)
{
    uint32_t waiter_priority = completion_priority_stress_waiter();
    uint32_t max_priority    = completion_priority_max();

    if (waiter_priority < max_priority)
        return waiter_priority + 1u;
    return waiter_priority;
}

/**
 * @brief 获取组9触发线程（线程模拟 ISR）的优先级
 */
static uint32_t completion_priority_isr_trigger(void)
{
    return completion_priority_max();
}

/**
 * @brief 获取测试控制线程优先级（防止状态机推进被饿死）
 */
static uint32_t completion_priority_test_ctrl(void)
{
    return completion_priority_max();
}

/**
 * @brief 重置压力测试上下文
 */
static void completion_stress_reset_ctx(void)
{
    uint32_t worker_index = 0u;

    g_stress.start           = 0u;
    g_stress.stop            = 0u;
    g_stress.targetWaitOk  = COMPLETION_STRESS_FUNCTIONAL_TARGET_ROUNDS_EQ;
    g_stress.waiterStarted  = 0u;
    g_stress.waiterDone     = 0u;
    g_stress.waiterState    = COMPLETION_STRESS_WAITER_IDLE;
    g_stress.waiterWaitOk  = 0u;
    g_stress.waiterWaitErr = 0u;
    g_stress.waiterLastRet = OM_OK;

    for (worker_index = 0u; worker_index < COMPLETION_STRESS_DONE_WORKERS; worker_index++) {
        g_stress.workerDone[worker_index]      = 0u;
        g_stress.workerDoneOk[worker_index]   = 0u;
        g_stress.workerDoneBusy[worker_index] = 0u;
        g_stress.workerDoneErr[worker_index]  = 0u;
        g_stress_done_threads[worker_index]     = NULL;
    }
}

/**
 * @brief 检查压力测试 worker 是否全部退出
 */
static int completion_stress_workers_all_done(void)
{
    uint32_t worker_index = 0u;

    for (worker_index = 0u; worker_index < COMPLETION_STRESS_DONE_WORKERS; worker_index++) {
        if (g_stress.workerDone[worker_index] == 0u)
            return 0;
    }
    return 1;
}

/**
 * @brief 等待压力测试 worker 全部退出
 */
static int completion_stress_wait_workers_done(uint32_t timeout_ms)
{
    OsalTimeMs start_ms = osal_time_now_monotonic();
    OsalTimeMs deadline_ms = start_ms + timeout_ms;

    while (!completion_stress_workers_all_done()) {
        if (!osal_time_before(osal_time_now_monotonic(), deadline_ms))
            return 0;
        (void)osal_sleep_ms(1u);
    }
    return 1;
}

/**
 * @brief 汇总压力测试 worker 统计
 */
static void completion_stress_collect_stats(
    uint32_t *total_done_ok,
    uint32_t *total_done_busy,
    uint32_t *total_done_err)
{
    uint32_t worker_index = 0u;

    if (!total_done_ok || !total_done_busy || !total_done_err)
        return;

    *total_done_ok   = 0u;
    *total_done_busy = 0u;
    *total_done_err  = 0u;

    for (worker_index = 0u; worker_index < COMPLETION_STRESS_DONE_WORKERS; worker_index++) {
        *total_done_ok += g_stress.workerDoneOk[worker_index];
        *total_done_busy += g_stress.workerDoneBusy[worker_index];
        *total_done_err += g_stress.workerDoneErr[worker_index];
    }
}

#if (COMPLETION_ISR_STRESS_ENABLE != 0u)
/**
 * @brief 重置 ISR 专项压测上下文
 */
static void completion_isr_stress_reset_ctx(void)
{
    g_isr_stress.start               = 0u;
    g_isr_stress.stop                = 0u;
    g_isr_stress.targetWaitOk      = COMPLETION_STRESS_TARGET_ROUNDS_UNBOUNDED;
    g_isr_stress.waiterStarted      = 0u;
    g_isr_stress.waiterDone         = 0u;
    g_isr_stress.waiterState        = COMPLETION_STRESS_WAITER_IDLE;
    g_isr_stress.waiterWaitOk      = 0u;
    g_isr_stress.waiterWaitErr     = 0u;
    g_isr_stress.waiterLastRet     = OM_OK;
    g_isr_stress.triggerThreadDone = 0u;
    g_isr_stress.isrDoneOk         = 0u;
    g_isr_stress.isrDoneBusy       = 0u;
    g_isr_stress.isrDoneErr        = 0u;
}

/**
 * @brief 按 done 返回码统计 ISR 专项结果
 * @param done_ret `completion_done` 返回值
 */
static void completion_isr_stress_count_done_ret(OmRet done_ret)
{
    if (done_ret == OM_OK)
        g_isr_stress.isrDoneOk++;
    else if (done_ret == OM_ERROR_BUSY)
        g_isr_stress.isrDoneBusy++;
    else
        g_isr_stress.isrDoneErr++;
}

/**
 * @brief ISR 专项 waiter 线程入口
 */
static void completion_isr_stress_waiter_thread_entry(void *arg)
{
    Completion* completion = (Completion*)arg;

    if (!completion) {
        g_isr_stress.waiterLastRet = OM_ERROR_PARAM;
        g_isr_stress.waiterState    = COMPLETION_STRESS_WAITER_DONE_ERROR;
        g_isr_stress.waiterDone     = 1u;
        osal_thread_exit();
    }

    g_isr_stress.waiterStarted = 1u;
    g_isr_stress.waiterState   = COMPLETION_STRESS_WAITER_RUNNING;
    for (;;) {
        if (g_isr_stress.waiterWaitOk >= g_isr_stress.targetWaitOk)
            break;
        if (g_isr_stress.stop != 0u)
            break;

        OmRet wait_ret = completion_wait(completion, OM_WAIT_FOREVER);
        if (wait_ret == OM_OK) {
            g_isr_stress.waiterWaitOk++;
            continue;
        }

        g_isr_stress.waiterWaitErr++;
        g_isr_stress.waiterLastRet = wait_ret;
        break;
    }

    if (g_isr_stress.waiterWaitErr == 0u) {
        g_isr_stress.waiterLastRet = OM_OK;
        g_isr_stress.waiterState    = COMPLETION_STRESS_WAITER_DONE_OK;
    } else {
        g_isr_stress.waiterState = COMPLETION_STRESS_WAITER_DONE_ERROR;
    }

    g_isr_stress.stop        = 1u;
    g_isr_stress.waiterDone = 1u;
    osal_thread_exit();
}

/**
 * @brief 触发线程：以高频 `completion_done` 模拟 ISR 生产者
 */
static void completion_isr_trigger_thread_entry(void *arg)
{
    uint32_t loop_count = 0u;
    (void)arg;

    while (g_isr_stress.start == 0u)
        (void)osal_sleep_ms(1u);

    while (g_isr_stress.stop == 0u) {
        completion_isr_stress_count_done_ret(completion_done(&g_completion));

        loop_count++;
        if ((loop_count & COMPLETION_ISR_STRESS_TRIGGER_YIELD_MASK) == 0u)
            (void)osal_sleep_ms(1u);
    }

    g_isr_stress.triggerThreadDone = 1u;
    osal_thread_exit();
}
#endif

/**
 * @brief 等待线程：用于验证单等待者约束与唤醒路径
 */
static void completion_waiter_thread_entry(void *arg)
{
    (void)arg;
    g_waiter_started = 1u;
    g_waiter_waiting = 1u;
    g_waiter_ret     = completion_wait(&g_completion, OM_WAIT_FOREVER);
    g_waiter_waiting = 0u;
    g_waiter_done    = 1u;
    osal_thread_exit();
}

/**
 * @brief 延迟 done 线程：用于验证 wait 被异步唤醒
 */
static void completion_done_thread_entry(void *arg)
{
    (void)arg;
    (void)osal_sleep_ms(g_done_delay_ms);
    g_done_thread_ret  = completion_done(&g_completion);
    g_done_thread_done = 1u;
    osal_thread_exit();
}

/**
 * @brief 压力测试等待线程：循环消费 one-shot 完成事件
 */
static void completion_stress_waiter_thread_entry(void *arg)
{
    Completion* completion = (Completion*)arg;

    if (!completion) {
        g_stress.waiterLastRet = OM_ERROR_PARAM;
        g_stress.waiterState    = COMPLETION_STRESS_WAITER_DONE_ERROR;
        g_stress.waiterDone     = 1u;
        osal_thread_exit();
    }

    g_stress.waiterStarted = 1u;
    g_stress.waiterState   = COMPLETION_STRESS_WAITER_RUNNING;
    for (;;) {
        if (g_stress.waiterWaitOk >= g_stress.targetWaitOk)
            break;
        if (g_stress.stop != 0u)
            break;

        OmRet wait_ret = completion_wait(completion, OM_WAIT_FOREVER);
        if (wait_ret == OM_OK) {
            g_stress.waiterWaitOk++;
            continue;
        }

        g_stress.waiterWaitErr++;
        g_stress.waiterLastRet = wait_ret;
        break;
    }

    if (g_stress.waiterWaitErr == 0u) {
        g_stress.waiterLastRet = OM_OK;
        g_stress.waiterState    = COMPLETION_STRESS_WAITER_DONE_OK;
    } else {
        g_stress.waiterState = COMPLETION_STRESS_WAITER_DONE_ERROR;
    }

    g_stress.stop        = 1u;
    g_stress.waiterDone = 1u;
    osal_thread_exit();
}

/**
 * @brief 压力测试 done 线程：并发冲击 completion_done
 */
static void completion_stress_done_thread_entry(void *arg)
{
    uint32_t worker_index = (uint32_t)(uintptr_t)arg;
    uint32_t done_ok      = 0u;
    uint32_t done_busy    = 0u;
    uint32_t done_err     = 0u;
    uint32_t loop_count   = 0u;

    if (worker_index >= COMPLETION_STRESS_DONE_WORKERS)
        osal_thread_exit();

    while (g_stress.start == 0u)
        (void)osal_sleep_ms(1u);

    while (g_stress.stop == 0u) {
        OmRet done_ret = completion_done(&g_completion);
        if (done_ret == OM_OK)
            done_ok++;
        else if (done_ret == OM_ERROR_BUSY)
            done_busy++;
        else
            done_err++;

        loop_count++;
        /*
         * 周期性阻塞 1ms，避免 done 线程长期抢占 CPU，
         * 导致 waiter 在低优先级关系下被饿死。
         */
        if ((loop_count & COMPLETION_STRESS_WORKER_YIELD_MASK) == 0u)
            (void)osal_sleep_ms(1u);
    }

    g_stress.workerDoneOk[worker_index]   = done_ok;
    g_stress.workerDoneBusy[worker_index] = done_busy;
    g_stress.workerDoneErr[worker_index]  = done_err;
    g_stress.workerDone[worker_index]      = 1u;
    osal_thread_exit();
}

/**
 * @brief 组8功能正确性子用例（固定轮次 + 严格计数关系）
 * @param done_priority done 线程优先级
 * @param done_worker_count 本子用例启用的 done 线程数（0 或越界时回退到最大值）
 * @param enforce_busy_gate 非 0 时强制检查 `total_done_busy > 0`
 * @param target_wait_ok 本子用例目标轮次（0 时回退到默认值）
 */
static void completion_run_group8_functional_case(
    uint32_t done_priority,
    uint32_t done_worker_count,
    int enforce_busy_gate,
    uint32_t target_wait_ok)
{
    OsalThreadAttr stress_waiter_attr = {
        "sync_cpt_str_wait",
        768u * OSAL_STACK_WORD_BYTES,
        completion_priority_stress_waiter(),
    };
    OsalThreadAttr stress_done_attr = {
        "sync_cpt_str_done",
        512u * OSAL_STACK_WORD_BYTES,
        completion_priority_clamp(done_priority),
    };
    uint32_t worker_index          = 0u;
    uint32_t total_done_ok         = 0u;
    uint32_t total_done_busy       = 0u;
    uint32_t total_done_err        = 0u;
    uint32_t waiter_reached_target = 0u;
    uint32_t waiter_finished       = 0u;
    uint32_t workers_finished      = 0u;
    uint32_t done_wait_delta       = 0u;

    if ((done_worker_count == 0u) || (done_worker_count > COMPLETION_STRESS_DONE_WORKERS))
        done_worker_count = COMPLETION_STRESS_DONE_WORKERS;
    if (target_wait_ok == 0u)
        target_wait_ok = COMPLETION_STRESS_FUNCTIONAL_TARGET_ROUNDS_EQ;

    completion_expect(completion_init(&g_completion) == OM_OK);
    completion_stress_reset_ctx();
    g_stress.targetWaitOk = target_wait_ok;

    completion_expect(
        osal_thread_create(&g_stress_waiter_thread, &stress_waiter_attr, completion_stress_waiter_thread_entry, &g_completion) ==
        OSAL_OK);
    completion_expect(completion_wait_flag(&g_stress.waiterStarted, 1000u) == 1);

    for (worker_index = 0u; worker_index < done_worker_count; worker_index++) {
        completion_expect(osal_thread_create(
                              &g_stress_done_threads[worker_index],
                              &stress_done_attr,
                              completion_stress_done_thread_entry,
                              (void *)(uintptr_t)worker_index) == OSAL_OK);
    }
    for (; worker_index < COMPLETION_STRESS_DONE_WORKERS; worker_index++)
        g_stress.workerDone[worker_index] = 1u;

    g_stress.start = 1u;

    waiter_reached_target =
        (uint32_t)completion_wait_flag(&g_stress.waiterDone, COMPLETION_STRESS_FUNCTIONAL_CASE_TIMEOUT_MS);
    g_stress.stop = 1u;
    waiter_finished = waiter_reached_target;
    if (waiter_finished == 0u) {
        waiter_finished = (uint32_t)completion_force_wake_waiter(
            &g_completion,
            &g_stress.waiterDone,
            COMPLETION_STRESS_STOP_TIMEOUT_MS,
            NULL);
    }

    workers_finished = (uint32_t)completion_stress_wait_workers_done(COMPLETION_STRESS_STOP_TIMEOUT_MS);
    if ((waiter_finished == 0u) || (workers_finished == 0u)) {
        completion_expect(waiter_finished == 1u);
        completion_expect(workers_finished == 1u);
        g_test_infra_abort = 1u;

        g_stress_waiter_thread = NULL;
        for (worker_index = 0u; worker_index < COMPLETION_STRESS_DONE_WORKERS; worker_index++)
            g_stress_done_threads[worker_index] = NULL;
        return;
    }

    completion_stress_collect_stats(&total_done_ok, &total_done_busy, &total_done_err);

    completion_expect(waiter_reached_target == 1u);
    completion_expect(waiter_finished == 1u);
    completion_expect(workers_finished == 1u);
    completion_expect(g_stress.waiterWaitErr == 0u);
    completion_expect(g_stress.waiterState == COMPLETION_STRESS_WAITER_DONE_OK);
    completion_expect(g_stress.waiterLastRet == OM_OK);
    completion_expect(total_done_err == 0u);

    if (enforce_busy_gate != 0)
        completion_expect(total_done_busy > 0u);

    if (total_done_ok >= g_stress.waiterWaitOk) {
        done_wait_delta = total_done_ok - g_stress.waiterWaitOk;
        completion_expect(done_wait_delta <= 1u);
    } else {
        completion_expect(0);
    }

    completion_expect(g_stress.waiterWaitOk == g_stress.targetWaitOk);

    g_stress_waiter_thread = NULL;
    for (worker_index = 0u; worker_index < COMPLETION_STRESS_DONE_WORKERS; worker_index++)
        g_stress_done_threads[worker_index] = NULL;
    completion_deinit(&g_completion);
}

/**
 * @brief 组8并发压力子用例（时间窗 + 活性阈值）
 * @param done_priority done 线程优先级
 * @param done_worker_count 本子用例启用的 done 线程数（0 或越界时回退到最大值）
 * @param enforce_busy_gate 非 0 时强制检查 `total_done_busy > 0`
 * @param pressure_window_ms 压测时间窗（毫秒）
 * @param min_wait_ok 活性阈值（默认 1）
 */
static void completion_run_group8_pressure_case(
    uint32_t done_priority,
    uint32_t done_worker_count,
    int enforce_busy_gate,
    uint32_t pressure_window_ms,
    uint32_t min_wait_ok)
{
    OsalThreadAttr stress_waiter_attr = {
        "sync_cpt_str_wait",
        768u * OSAL_STACK_WORD_BYTES,
        completion_priority_stress_waiter(),
    };
    OsalThreadAttr stress_done_attr = {
        "sync_cpt_str_done",
        512u * OSAL_STACK_WORD_BYTES,
        completion_priority_clamp(done_priority),
    };
    OsalTimeMs deadline_ms          = 0u;
    uint32_t worker_index               = 0u;
    uint32_t total_done_ok              = 0u;
    uint32_t total_done_busy            = 0u;
    uint32_t total_done_err             = 0u;
    uint32_t waiter_finished            = 0u;
    uint32_t workers_finished           = 0u;
    CompletionDoneStats force_stats = {0u, 0u, 0u};

    if ((done_worker_count == 0u) || (done_worker_count > COMPLETION_STRESS_DONE_WORKERS))
        done_worker_count = COMPLETION_STRESS_DONE_WORKERS;
    if (pressure_window_ms == 0u)
        pressure_window_ms = COMPLETION_STRESS_PRESSURE_WINDOW_MS;
    if (min_wait_ok == 0u)
        min_wait_ok = COMPLETION_STRESS_PRESSURE_MIN_WAIT_OK;

    completion_expect(completion_init(&g_completion) == OM_OK);
    completion_stress_reset_ctx();
    g_stress.targetWaitOk = COMPLETION_STRESS_TARGET_ROUNDS_UNBOUNDED;

    completion_expect(
        osal_thread_create(&g_stress_waiter_thread, &stress_waiter_attr, completion_stress_waiter_thread_entry, &g_completion) ==
        OSAL_OK);
    completion_expect(completion_wait_flag(&g_stress.waiterStarted, 1000u) == 1);

    for (worker_index = 0u; worker_index < done_worker_count; worker_index++) {
        completion_expect(osal_thread_create(
                              &g_stress_done_threads[worker_index],
                              &stress_done_attr,
                              completion_stress_done_thread_entry,
                              (void *)(uintptr_t)worker_index) == OSAL_OK);
    }
    for (; worker_index < COMPLETION_STRESS_DONE_WORKERS; worker_index++)
        g_stress.workerDone[worker_index] = 1u;

    g_stress.start = 1u;
    deadline_ms = osal_time_now_monotonic() + pressure_window_ms;
    while (osal_time_before(osal_time_now_monotonic(), deadline_ms)) {
        if (g_stress.waiterWaitErr != 0u)
            break;
        (void)osal_sleep_ms(1u);
    }

    g_stress.stop = 1u;
    waiter_finished = (uint32_t)completion_force_wake_waiter(
        &g_completion,
        &g_stress.waiterDone,
        COMPLETION_STRESS_STOP_TIMEOUT_MS,
        &force_stats);
    workers_finished = (uint32_t)completion_stress_wait_workers_done(COMPLETION_STRESS_STOP_TIMEOUT_MS);
    if ((waiter_finished == 0u) || (workers_finished == 0u)) {
        completion_expect(waiter_finished == 1u);
        completion_expect(workers_finished == 1u);
        g_test_infra_abort = 1u;

        g_stress_waiter_thread = NULL;
        for (worker_index = 0u; worker_index < COMPLETION_STRESS_DONE_WORKERS; worker_index++)
            g_stress_done_threads[worker_index] = NULL;
        return;
    }

    completion_stress_collect_stats(&total_done_ok, &total_done_busy, &total_done_err);
    total_done_ok += force_stats.doneOk;
    total_done_busy += force_stats.doneBusy;
    total_done_err += force_stats.doneErr;

    completion_expect(waiter_finished == 1u);
    completion_expect(workers_finished == 1u);
    completion_expect(g_stress.waiterWaitErr == 0u);
    completion_expect(g_stress.waiterState == COMPLETION_STRESS_WAITER_DONE_OK);
    completion_expect(g_stress.waiterLastRet == OM_OK);
    completion_expect(total_done_err == 0u);
    completion_expect(g_stress.waiterWaitOk >= min_wait_ok);
    completion_expect(total_done_ok >= g_stress.waiterWaitOk);
    if (enforce_busy_gate != 0)
        completion_expect(total_done_busy > 0u);

    g_stress_waiter_thread = NULL;
    for (worker_index = 0u; worker_index < COMPLETION_STRESS_DONE_WORKERS; worker_index++)
        g_stress_done_threads[worker_index] = NULL;
    completion_deinit(&g_completion);
}

/**
 * @brief completion 合同测试线程
 */
static void completion_test_thread_entry(void *arg)
{
    OsalThreadAttr waiter_attr = {
        "sync_cpt_waiter",
        512u * OSAL_STACK_WORD_BYTES,
        2u,
    };
    OsalThreadAttr done_attr = {
        "sync_cpt_done",
        512u * OSAL_STACK_WORD_BYTES,
        2u,
    };
#if (COMPLETION_ISR_STRESS_ENABLE != 0u)
    OsalThreadAttr isr_waiter_attr = {
        "sync_cpt_isr_wait",
        768u * OSAL_STACK_WORD_BYTES,
        completion_priority_stress_waiter(),
    };
    OsalThreadAttr isr_trigger_attr = {
        "sync_cpt_isr_trig",
        512u * OSAL_STACK_WORD_BYTES,
        completion_priority_isr_trigger(),
    };
#endif
#if (COMPLETION_ISR_STRESS_ENABLE != 0u)
    uint32_t isr_waiter_finished            = 0u;
    uint32_t isr_trigger_finished           = 0u;
    uint32_t isr_total_done_ok              = 0u;
    uint32_t isr_total_done_busy            = 0u;
    uint32_t isr_total_done_err             = 0u;
    CompletionDoneStats isr_force_stats = {0u, 0u, 0u};
    OsalTimeMs isr_deadline_ms          = 0u;
#endif

    (void)arg;
    g_test_infra_abort = 0u;

    /* 组1：参数校验 */
    completion_expect(completion_init(NULL) == OM_ERROR_PARAM);
    completion_expect(completion_wait(NULL, 0u) == OM_ERROR_PARAM);
    completion_expect(completion_done(NULL) == OM_ERROR_PARAM);

    /* 组2：timeout=0 未完成即超时 */
    completion_expect(completion_init(&g_completion) == OM_OK);
    completion_expect(completion_wait(&g_completion, 0u) == OM_ERROR_TIMEOUT);

    /* 组3：done 先于 wait（one-shot 消费） */
    completion_expect(completion_done(&g_completion) == OM_OK);
    completion_expect(completion_wait(&g_completion, 0u) == OM_OK);
    completion_expect(completion_wait(&g_completion, 0u) == OM_ERROR_TIMEOUT);

    /* 组4：重复 done 返回 BUSY */
    completion_expect(completion_done(&g_completion) == OM_OK);
    completion_expect(completion_done(&g_completion) == OM_ERROR_BUSY);
    completion_expect(completion_wait(&g_completion, 0u) == OM_OK);

    /* 组5：有限超时 */
    completion_expect(completion_wait(&g_completion, 20u) == OM_ERROR_TIMEOUT);

    /* 组6：单等待者约束 */
    g_waiter_started = 0u;
    g_waiter_waiting = 0u;
    g_waiter_done    = 0u;
    g_waiter_ret     = OM_ERROR;
    completion_expect(osal_thread_create(&g_waiter_thread, &waiter_attr, completion_waiter_thread_entry, NULL) == OSAL_OK);
    completion_expect(completion_wait_flag(&g_waiter_started, 100u) == 1);
    completion_expect(completion_wait_flag(&g_waiter_waiting, 100u) == 1);
    completion_expect(completion_wait_status(&g_completion.status, COMP_WAIT, 100u) == 1);
    completion_expect(completion_wait(&g_completion, 0u) == OM_ERROR_BUSY);
    completion_expect(completion_done(&g_completion) == OM_OK);
    completion_expect(completion_wait_flag(&g_waiter_done, 100u) == 1);
    completion_expect(g_waiter_ret == OM_OK);
    g_waiter_thread = NULL;

    /* 组7：wait 先于 done 唤醒 */
    g_done_thread_done = 0u;
    g_done_thread_ret  = OM_ERROR;
    g_done_delay_ms    = 20u;
    completion_expect(osal_thread_create(&g_done_thread, &done_attr, completion_done_thread_entry, NULL) == OSAL_OK);
    completion_expect(completion_wait(&g_completion, OM_WAIT_FOREVER) == OM_OK);
    completion_expect(completion_wait_flag(&g_done_thread_done, 100u) == 1);
    completion_expect(g_done_thread_ret == OM_OK);
    g_done_thread = NULL;

    /*
     * 组8：双轨模式
     * 1) 功能正确性：done < waiter（固定轮次 + 严格 one-shot 计数关系）
     * 2) 功能正确性：done == waiter（固定轮次 + 严格 one-shot 计数关系）
     * 3) 并发压力：done > waiter（时间窗 + 活性阈值 + BUSY 竞争证据）
     */
    completion_run_group8_functional_case(
        completion_priority_stress_done_lt(),
        COMPLETION_STRESS_DONE_WORKERS,
        0,
        COMPLETION_STRESS_FUNCTIONAL_TARGET_ROUNDS_LT);
    if (g_test_infra_abort != 0u)
        goto test_finish;

    completion_run_group8_functional_case(
        completion_priority_stress_done_eq(),
        COMPLETION_STRESS_DONE_WORKERS,
        1,
        COMPLETION_STRESS_FUNCTIONAL_TARGET_ROUNDS_EQ);
    if (g_test_infra_abort != 0u)
        goto test_finish;

    completion_run_group8_pressure_case(
        completion_priority_stress_done_gt(),
        COMPLETION_STRESS_DONE_WORKERS_GT_CASE,
        1,
        COMPLETION_STRESS_PRESSURE_WINDOW_MS,
        COMPLETION_STRESS_PRESSURE_MIN_WAIT_OK);
    if (g_test_infra_abort != 0u)
        goto test_finish;

#if (COMPLETION_ISR_STRESS_ENABLE != 0u)
    /*
     * 组9：线程模拟 ISR 并发压力（单模式）
     * - 采用“时间窗 + 活性阈值 + BUSY 竞争证据”验收；
     * - 不验证真实中断上下文，仅验证高优先级生产者 + wait 消费者合同。
     */
    completion_expect(completion_init(&g_completion) == OM_OK);
    completion_isr_stress_reset_ctx();
    g_isr_stress.targetWaitOk = COMPLETION_STRESS_TARGET_ROUNDS_UNBOUNDED;

    completion_expect(osal_thread_create(
                          &g_isr_stress_waiter_thread,
                          &isr_waiter_attr,
                          completion_isr_stress_waiter_thread_entry,
                          &g_completion) == OSAL_OK);
    completion_expect(completion_wait_flag(&g_isr_stress.waiterStarted, 1000u) == 1);
    completion_expect(
        osal_thread_create(&g_isr_trigger_thread, &isr_trigger_attr, completion_isr_trigger_thread_entry, NULL) == OSAL_OK);

    g_isr_stress.start = 1u;
    isr_deadline_ms = osal_time_now_monotonic() + COMPLETION_ISR_STRESS_WINDOW_MS;
    while (osal_time_before(osal_time_now_monotonic(), isr_deadline_ms)) {
        if (g_isr_stress.waiterWaitErr != 0u)
            break;
        (void)osal_sleep_ms(1u);
    }

    g_isr_stress.stop = 1u;
    isr_waiter_finished = (uint32_t)completion_force_wake_waiter(
        &g_completion,
        &g_isr_stress.waiterDone,
        COMPLETION_ISR_STRESS_STOP_TIMEOUT_MS,
        &isr_force_stats);

    isr_trigger_finished =
        (uint32_t)completion_wait_flag(&g_isr_stress.triggerThreadDone, COMPLETION_ISR_STRESS_STOP_TIMEOUT_MS);

    if ((isr_waiter_finished == 0u) || (isr_trigger_finished == 0u)) {
        completion_expect(isr_waiter_finished == 1u);
        completion_expect(isr_trigger_finished == 1u);
        g_test_infra_abort = 1u;
        g_isr_stress_waiter_thread = NULL;
        g_isr_trigger_thread = NULL;
        goto test_finish;
    }

    isr_total_done_ok = g_isr_stress.isrDoneOk + isr_force_stats.doneOk;
    isr_total_done_busy = g_isr_stress.isrDoneBusy + isr_force_stats.doneBusy;
    isr_total_done_err = g_isr_stress.isrDoneErr + isr_force_stats.doneErr;

    completion_expect(isr_waiter_finished == 1u);
    completion_expect(isr_trigger_finished == 1u);
    completion_expect(g_isr_stress.waiterWaitErr == 0u);
    completion_expect(g_isr_stress.waiterState == COMPLETION_STRESS_WAITER_DONE_OK);
    completion_expect(g_isr_stress.waiterLastRet == OM_OK);
    completion_expect(isr_total_done_err == 0u);
    completion_expect(isr_total_done_busy > 0u);
    completion_expect(g_isr_stress.waiterWaitOk >= COMPLETION_ISR_STRESS_MIN_WAIT_OK);
    completion_expect(isr_total_done_ok >= g_isr_stress.waiterWaitOk);

    g_isr_stress_waiter_thread = NULL;
    g_isr_trigger_thread       = NULL;
    completion_deinit(&g_completion);
#endif

test_finish:
    g_result.done = 1u;

    for (;;)
        (void)osal_sleep_ms(1000u);
}

/**
 * @brief 例程入口
 * @return 创建测试线程失败返回 -1；成功后启动调度
 */
int main(void)
{
    OsalThreadAttr test_attr = {
        "sync_cpt_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, completion_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}

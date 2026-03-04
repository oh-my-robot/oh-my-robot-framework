/**
 * @file main.c
 * @brief SYNC completion 鐙珛渚嬬▼
 * @details
 * 鏈緥绋嬭鐩?completion 鐨勫熀纭€鍚堝悓涓庨珮骞跺彂鍘嬪姏鍦烘櫙锛? * - 鍩虹璇箟锛氬崟绛夊緟鑰?one-shot銆乨one 鍏堜簬 wait銆侀噸澶?done 杩斿洖 BUSY銆乼imeout 璇箟銆? * - 鍘嬪姏璇箟锛? *   - 缁?8锛氬绾跨▼骞跺彂 done + 鍗曠嚎绋?wait锛坉one 涓?waiter 涓夋€佷紭鍏堢骇瀵圭収锛夛紱
 *   - 缁?9锛氱嚎绋嬫ā鎷?ISR done + 绾跨▼ wait锛堜笉渚濊禆鑺墖 IRQ 澶存枃浠讹級銆? * - 娴嬭瘯妯″瀷锛堝崟妯″紡锛夛細
 *   - 鍔熻兘姝ｇ‘鎬ц建閬擄細鍥哄畾杞 + 涓ユ牸 one-shot 璁℃暟鍏崇郴锛? *   - 骞跺彂鍘嬪姏杞ㄩ亾锛氬浐瀹氭椂闂寸獥 + 娲绘€ч槇鍊硷紙>=1锛? 绔炰簤璇佹嵁銆? * - 鍒ゅ畾鏍囧噯锛? *   - 鍏ㄥ眬閫氳繃锛歚g_result.done == 1` 涓?`g_result.failed == 0`銆? *   - 鍘嬫祴鍏叡閫氳繃鏉′欢锛? *     1) waiter/producer 鍧囨寜鏈熼€€鍑猴紱
 *     2) `waiter_wait_err == 0` 涓?done 渚?`*_done_err == 0`锛? *   - 鍔熻兘姝ｇ‘鎬ц建閬撻澶栨潯浠讹細
 *     3) `*_done_ok >= waiter_wait_ok` 涓?`*_done_ok - waiter_wait_ok <= 1`锛? *     4) `waiter_wait_ok == target_wait_ok`銆? *   - 骞跺彂鍘嬪姏杞ㄩ亾棰濆鏉′欢锛? *     3) `waiter_wait_ok >= min_wait_ok`锛堟椿鎬ч槇鍊硷紝榛樿 1锛夛紱
 *     4) `done_busy > 0`锛堣瘉鏄庡瓨鍦ㄦ湁鏁堢珵浜夛級銆? */
#include "osal/osal.h"
#include "osal/osal_config.h"
#include "sync/completion.h"

/**
 * @brief completion 绾跨▼妯℃嫙 ISR 涓撻」鍘嬫祴寮€鍏? * @details
 * - `0`锛氬叧闂粍 9锛堢嚎绋嬫ā鎷?ISR 璺緞涓撻」锛夈€? * - `1`锛氬惎鐢ㄧ粍 9锛堥粯璁わ級銆? */
#ifndef COMPLETION_ISR_STRESS_ENABLE
#define COMPLETION_ISR_STRESS_ENABLE 1u
#endif

#if (COMPLETION_ISR_STRESS_ENABLE != 0u) && (COMPLETION_ISR_STRESS_ENABLE != 1u)
#error "COMPLETION_ISR_STRESS_ENABLE 浠呭厑璁?0 鎴?1"
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
    /* 骞跺彂 done 绾跨▼鏁颁笂闄愶紝鐢ㄤ簬鍒堕€犵珵浜夊帇鍔涖€?*/
    COMPLETION_STRESS_DONE_WORKERS = 4u,
    /* 缁?8 涓?done > waiter 鍘嬪姏瀛愬満鏅娇鐢ㄧ殑 done 绾跨▼鏁般€?*/
    COMPLETION_STRESS_DONE_WORKERS_GT_CASE = 2u,
    /* 鍔熻兘姝ｇ‘鎬ц建閬擄紙鍥哄畾杞锛夛細done<waiter銆?*/
    COMPLETION_STRESS_FUNCTIONAL_TARGET_ROUNDS_LT = 20000u,
    /* 鍔熻兘姝ｇ‘鎬ц建閬擄紙鍥哄畾杞锛夛細done==waiter銆?*/
    COMPLETION_STRESS_FUNCTIONAL_TARGET_ROUNDS_EQ = 20000u,
    /* 鍔熻兘姝ｇ‘鎬ц建閬撳崟瀛愬満鏅秴鏃朵繚鎶わ紙姣锛夈€?*/
    COMPLETION_STRESS_FUNCTIONAL_CASE_TIMEOUT_MS = 120000u,
    /* 骞跺彂鍘嬪姏杞ㄩ亾鏃堕棿绐楋紙姣锛夈€?*/
    COMPLETION_STRESS_PRESSURE_WINDOW_MS = 20000u,
    /* 骞跺彂鍘嬪姏杞ㄩ亾娲绘€ч槇鍊硷紙浠呰姹傚瓨鍦ㄦ渶灏忓墠杩涳紝涓嶅仛鎬ц兘璇勭骇锛夈€?*/
    COMPLETION_STRESS_PRESSURE_MIN_WAIT_OK = 1u,
    /* 涓嶈杞涓婇檺锛堢敱 stop 鏀舵暃锛夈€?*/
    COMPLETION_STRESS_TARGET_ROUNDS_UNBOUNDED = 0xFFFFFFFFu,
    /* 缁?8 鍋滄満鏀跺熬绛夊緟涓婇檺锛堟绉掞級銆?*/
    COMPLETION_STRESS_STOP_TIMEOUT_MS = 5000u,
    COMPLETION_STRESS_SHUTDOWN_TIMEOUT_MS = 2000u,
    /* done 绾跨▼璁╁嚭 CPU 鐨勮妭娴佹帺鐮侊紙姣?64 娆″惊鐜鍑轰竴娆★級銆?*/
    COMPLETION_STRESS_WORKER_YIELD_MASK = 0x003Fu
};

enum {
    /* 绾跨▼妯℃嫙 ISR 鍘嬪姏杞ㄩ亾鏃堕棿绐楋紙姣锛夈€?*/
    COMPLETION_ISR_STRESS_WINDOW_MS = 20000u,
    /* 绾跨▼妯℃嫙 ISR 鍘嬪姏杞ㄩ亾娲绘€ч槇鍊硷紙浠呰姹傚瓨鍦ㄦ渶灏忓墠杩涳級銆?*/
    COMPLETION_ISR_STRESS_MIN_WAIT_OK = 1u,
    /* 绾跨▼妯℃嫙 ISR 涓撻」鍋滄満鏀跺熬绛夊緟涓婇檺锛堟绉掞級銆?*/
    COMPLETION_ISR_STRESS_STOP_TIMEOUT_MS = 5000u,
    /* 瑙﹀彂绾跨▼璁╁嚭 CPU 鐨勮妭娴佹帺鐮侊紙姣?1024 娆″惊鐜鍑轰竴娆★級銆?*/
    COMPLETION_ISR_STRESS_TRIGGER_YIELD_MASK = 0x03FFu,
};

enum {
    /* 缁?8/缁?9 鐨?waiter 鍩哄噯浼樺厛绾с€?*/
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
 * @brief 绠€鍗曟柇瑷€璁℃暟鍣? * @param condition 闈?0 琛ㄧず鏂█閫氳繃
 */
static void completion_expect(int condition)
{
    g_result.total++;
    if (!condition)
        g_result.failed++;
}

/**
 * @brief 绛夊緟鏌愪釜鏍囧織浣嶄负 1锛堣秴鏃跺け璐ワ級
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
 * @brief 绛夊緟 completion 杩涘叆鎸囧畾鐘舵€侊紙鐢ㄤ簬娑堥櫎娴嬭瘯鎻℃墜绔炴€侊級
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
 * @brief 鍦ㄥ仠鏈洪樁娈靛惊鐜ˉ鍙?done锛岀‘淇?waiter 浠?WAIT_FOREVER 閫€鍑? * @param completion completion 瀵硅薄
 * @param waiter_done waiter 閫€鍑烘爣蹇? * @param timeout_ms 鏀跺熬涓婇檺锛堟绉掞級
 * @param force_done_stats 闈?NULL 鏃剁粺璁℃敹灏鹃樁娈?done 杩斿洖鍊? */
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
 * @brief 鑾峰彇褰撳墠绔彛鍙敤鐨勬渶楂樼嚎绋嬩紭鍏堢骇鍊? */
static uint32_t completion_priority_max(void)
{
    if (OSAL_PRIORITY_MAX == 0u)
        return 0u;
    return OSAL_PRIORITY_MAX - 1u;
}

/**
 * @brief 灏嗚緭鍏ヤ紭鍏堢骇鏀舵暃鍒板綋鍓嶇鍙ｆ湁鏁堣寖鍥? */
static uint32_t completion_priority_clamp(uint32_t priority)
{
    uint32_t max_priority = completion_priority_max();

    if (priority > max_priority)
        return max_priority;
    return priority;
}

/**
 * @brief 鑾峰彇缁?8/缁?9 鐨?waiter 浼樺厛绾? */
static uint32_t completion_priority_stress_waiter(void)
{
    return completion_priority_clamp(COMPLETION_STRESS_WAITER_PRIORITY_BASE);
}

/**
 * @brief 鑾峰彇缁?8 涓?done < waiter 鐨勪紭鍏堢骇
 */
static uint32_t completion_priority_stress_done_lt(void)
{
    uint32_t waiter_priority = completion_priority_stress_waiter();

    if (waiter_priority > 1u)
        return waiter_priority - 1u;
    return waiter_priority;
}

/**
 * @brief 鑾峰彇缁?8 涓?done == waiter 鐨勪紭鍏堢骇
 */
static uint32_t completion_priority_stress_done_eq(void)
{
    return completion_priority_stress_waiter();
}

/**
 * @brief 鑾峰彇缁?8 涓?done > waiter 鐨勪紭鍏堢骇
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
 * @brief 鑾峰彇缁?9 瑙﹀彂绾跨▼锛堢嚎绋嬫ā鎷?ISR锛夌殑浼樺厛绾? */
static uint32_t completion_priority_isr_trigger(void)
{
    return completion_priority_max();
}

/**
 * @brief 鑾峰彇娴嬭瘯鎺у埗绾跨▼浼樺厛绾э紙闃叉鐘舵€佹満鎺ㄨ繘琚タ姝伙級
 */
static uint32_t completion_priority_test_ctrl(void)
{
    return completion_priority_max();
}

/**
 * @brief 閲嶇疆鍘嬪姏娴嬭瘯涓婁笅鏂? */
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
 * @brief 妫€鏌ュ帇鍔涙祴璇?worker 鏄惁鍏ㄩ儴閫€鍑? */
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
 * @brief 绛夊緟鍘嬪姏娴嬭瘯 worker 鍏ㄩ儴閫€鍑? */
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
 * @brief 姹囨€诲帇鍔涙祴璇?worker 缁熻
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
 * @brief 閲嶇疆 ISR 涓撻」鍘嬫祴涓婁笅鏂? */
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
 * @brief 灏?done 杩斿洖鐮佽鍏?ISR 涓撻」缁熻
 * @param done_ret `completion_done` 杩斿洖鍊? */
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
 * @brief ISR 涓撻」 waiter 绾跨▼鍏ュ彛
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
 * @brief 瑙﹀彂绾跨▼锛氫互楂橀 `completion_done` 妯℃嫙 ISR 鐢熶骇鑰? */
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
 * @brief 绛夊緟绾跨▼锛氱敤浜庨獙璇佸崟绛夊緟鑰呯害鏉熶笌鍞ら啋璺緞
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
 * @brief 寤惰繜 done 绾跨▼锛氱敤浜庨獙璇?wait 琚紓姝ュ敜閱? */
static void completion_done_thread_entry(void *arg)
{
    (void)arg;
    (void)osal_sleep_ms(g_done_delay_ms);
    g_done_thread_ret  = completion_done(&g_completion);
    g_done_thread_done = 1u;
    osal_thread_exit();
}

/**
 * @brief 鍘嬪姏娴嬭瘯绛夊緟绾跨▼锛氬惊鐜秷璐?one-shot 瀹屾垚鎬? */
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
 * @brief 鍘嬪姏娴嬭瘯 done 绾跨▼锛氬苟鍙戝啿鍑?completion_done
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
         * 鍛ㄦ湡鎬ч樆濉?1ms锛岄伩鍏?done 绾跨▼闀挎湡鎶㈠崰 CPU锛?         * 瀵艰嚧 waiter 鍦ㄤ綆浼樺厛绾у叧绯讳笅琚タ姝汇€?         */
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
 * @brief 缁?8 鍔熻兘姝ｇ‘鎬у瓙鐢ㄤ緥锛堝浐瀹氳疆娆?+ 涓ユ牸璁℃暟鍏崇郴锛? * @param done_priority done 绾跨▼浼樺厛绾? * @param done_worker_count 鏈瓙鐢ㄤ緥鍚敤鐨?done 绾跨▼鏁帮紙0 鎴栬秺鐣屾椂鍥為€€鍒版渶澶у€硷級
 * @param enforce_busy_gate 闈?0 鏃跺己鍒舵鏌?`total_done_busy > 0`
 * @param target_wait_ok 鏈瓙鐢ㄤ緥鐩爣杞锛? 鏃跺洖閫€鍒伴粯璁わ級
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
 * @brief 缁?8 骞跺彂鍘嬪姏瀛愮敤渚嬶紙鏃堕棿绐?+ 娲绘€ч槇鍊硷級
 * @param done_priority done 绾跨▼浼樺厛绾? * @param done_worker_count 鏈瓙鐢ㄤ緥鍚敤鐨?done 绾跨▼鏁帮紙0 鎴栬秺鐣屾椂鍥為€€鍒版渶澶у€硷級
 * @param enforce_busy_gate 闈?0 鏃跺己鍒舵鏌?`total_done_busy > 0`
 * @param pressure_window_ms 鍘嬫祴鏃堕棿绐楋紙姣锛? * @param min_wait_ok 娲绘€ч槇鍊硷紙榛樿 1锛? */
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
 * @brief completion 鍚堝悓娴嬭瘯绾跨▼
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

    /* 缁?1锛氬弬鏁版牎楠?*/
    completion_expect(completion_init(NULL) == OM_ERROR_PARAM);
    completion_expect(completion_wait(NULL, 0u) == OM_ERROR_PARAM);
    completion_expect(completion_done(NULL) == OM_ERROR_PARAM);

    /* 缁?2锛歵imeout=0 鏈畬鎴愬嵆瓒呮椂 */
    completion_expect(completion_init(&g_completion) == OM_OK);
    completion_expect(completion_wait(&g_completion, 0u) == OM_ERROR_TIMEOUT);

    /* 缁?3锛歞one 鍏堜簬 wait锛坥ne-shot 娑堣垂锛?*/
    completion_expect(completion_done(&g_completion) == OM_OK);
    completion_expect(completion_wait(&g_completion, 0u) == OM_OK);
    completion_expect(completion_wait(&g_completion, 0u) == OM_ERROR_TIMEOUT);

    /* 缁?4锛氶噸澶?done 杩斿洖 BUSY */
    completion_expect(completion_done(&g_completion) == OM_OK);
    completion_expect(completion_done(&g_completion) == OM_ERROR_BUSY);
    completion_expect(completion_wait(&g_completion, 0u) == OM_OK);

    /* 缁?5锛氭湁闄愯秴鏃?*/
    completion_expect(completion_wait(&g_completion, 20u) == OM_ERROR_TIMEOUT);

    /* 缁?6锛氬崟绛夊緟鑰呯害鏉?*/
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

    /* 缁?7锛歸ait 鍏堜簬 done 鍞ら啋 */
    g_done_thread_done = 0u;
    g_done_thread_ret  = OM_ERROR;
    g_done_delay_ms    = 20u;
    completion_expect(osal_thread_create(&g_done_thread, &done_attr, completion_done_thread_entry, NULL) == OSAL_OK);
    completion_expect(completion_wait(&g_completion, OM_WAIT_FOREVER) == OM_OK);
    completion_expect(completion_wait_flag(&g_done_thread_done, 100u) == 1);
    completion_expect(g_done_thread_ret == OM_OK);
    g_done_thread = NULL;

    /*
     * 缁?8锛氬弻杞ㄦā鍨?     * 1) 鍔熻兘姝ｇ‘鎬э細done < waiter锛堝浐瀹氳疆娆?+ 涓ユ牸 one-shot 璁℃暟鍏崇郴锛?     * 2) 鍔熻兘姝ｇ‘鎬э細done == waiter锛堝浐瀹氳疆娆?+ 涓ユ牸 one-shot 璁℃暟鍏崇郴锛?     * 3) 骞跺彂鍘嬪姏锛歞one > waiter锛堟椂闂寸獥 + 娲绘€ч槇鍊?+ BUSY 绔炰簤璇佹嵁锛?     */
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
     * 缁?9锛氱嚎绋嬫ā鎷?ISR 骞跺彂鍘嬪姏锛堝崟妯″紡锛?     * - 閲囩敤鈥滄椂闂寸獥 + 娲绘€ч槇鍊?+ BUSY 绔炰簤璇佹嵁鈥濋獙鏀讹紱
     * - 涓嶉獙璇佺湡瀹炰腑鏂笂涓嬫枃锛屼粎楠岃瘉楂樹紭鍏堢骇鐢熶骇鑰?+ wait 娑堣垂鑰呭悎鍚屻€?     */
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
 * @brief 渚嬬▼鍏ュ彛
 * @return 鍒涘缓娴嬭瘯绾跨▼澶辫触杩斿洖 -1锛涙垚鍔熷悗鍚姩璋冨害鍣? */
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


/**
 * @file main.c
 * @brief MPSC 环形缓冲区 MCU 独立例程
 * @details
 * 本例程覆盖 MPSC 环形缓冲区的基础合同与多 Task 并发场景：
 * - 组1：基础合同（init/in/out/peek/is_full/is_empty/is_empty_after_out）
 * - 组2：多 Task MPSC（2 个生产者 Task + 1 个消费者 Task，验证 per-producer FIFO）
 * - 组3：高优先级 Task 模拟 ISR 生产者 + 普通 Task 生产者 → 单消费者
 *
 * 测试模型：固定轮次 + 严格计数关系。
 * 判定标准：`g_result.done == 1` 且 `g_result.failed == 0`。
 */
#include "core/om_init.h"
#include "data_struct/mpsc_ringbuf.h"
#include "osal/osal.h"
#include "osal/osal_config.h"

/* ---- 配置常量 ---- */

enum {
    MPSC_TEST_CAPACITY         = 16u,   /* 队列容量（2 的幂） */
    MPSC_TEST_ITEM_SIZE        = 8u,    /* 元素大小（字节） */
    MPSC_TEST_PRODUCER_ROUNDS  = 2000u, /* 每个生产者的发送轮次 */
    MPSC_TEST_PRODUCER_COUNT   = 2u,    /* 生产者数量 */
    MPSC_TEST_CONSUMER_TIMEOUT = 30000u /* 消费者超时保护（ms） */
};

/* ---- 测试数据结构 ---- */

typedef struct
{
    uint32_t producer_id;
    uint32_t seq;
} MpscTestItem;

/* ---- 测试结果基础设施 ---- */

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} MpscTestResult;

static MpscTestResult g_result = {0u, 0u, 0u};

static void mpsc_expect(int condition)
{
    g_result.total++;
    if (!condition)
        g_result.failed++;
}

static int mpsc_wait_flag(volatile uint32_t *flag, uint32_t timeout_ms)
{
    OsalTimeMs start_ms    = osal_time_now_monotonic();
    OsalTimeMs deadline_ms = start_ms + timeout_ms;

    while (*flag == 0u) {
        if (!osal_time_before(osal_time_now_monotonic(), deadline_ms))
            return 0;
        (void)osal_sleep_ms(1u);
    }
    return 1;
}

/* ---- 组1：基础合同测试 ---- */

static void mpsc_run_basic_tests(void)
{
    uint8_t buf[MPSC_TEST_CAPACITY * MPSC_TEST_ITEM_SIZE];
    OmAtomicU8 ready[MPSC_TEST_CAPACITY];
    MpscRingbuf rb;

    MpscTestItem in_item;
    MpscTestItem out_item;

    /* 参数非法 */
    mpsc_expect(mpscrb_init(NULL, buf, ready, sizeof(MpscTestItem), MPSC_TEST_CAPACITY) == false);
    mpsc_expect(mpscrb_init(&rb, NULL, ready, sizeof(MpscTestItem), MPSC_TEST_CAPACITY) == false);
    mpsc_expect(mpscrb_init(&rb, buf, NULL, sizeof(MpscTestItem), MPSC_TEST_CAPACITY) == false);

    /* 初始化 */
    mpsc_expect(mpscrb_init(&rb, buf, ready, sizeof(MpscTestItem), MPSC_TEST_CAPACITY) == true);
    mpsc_expect(mpscrb_is_empty(&rb) == true);
    mpsc_expect(mpscrb_is_full(&rb) == false);
    mpsc_expect(mpscrb_len(&rb) == 0u);
    mpsc_expect(mpscrb_avail(&rb) == MPSC_TEST_CAPACITY);

    /* 空队列出队失败 */
    mpsc_expect(mpscrb_out(&rb, &out_item) == false);
    mpsc_expect(mpscrb_out_peek(&rb, &out_item) == false);

    /* 写入 / 读取单元素 */
    in_item.producer_id = 0u;
    in_item.seq         = 42u;
    mpsc_expect(mpscrb_in(&rb, &in_item) == true);
    mpsc_expect(mpscrb_len(&rb) == 1u);

    out_item.producer_id = 0xFFu;
    out_item.seq         = 0xFFu;
    mpsc_expect(mpscrb_out(&rb, &out_item) == true);
    mpsc_expect(out_item.producer_id == 0u);
    mpsc_expect(out_item.seq == 42u);
    mpsc_expect(mpscrb_is_empty(&rb) == true);

    /* 填满 */
    for (uint32_t i = 0u; i < MPSC_TEST_CAPACITY; i++) {
        in_item.producer_id = 0u;
        in_item.seq         = i;
        mpsc_expect(mpscrb_in(&rb, &in_item) == true);
    }
    mpsc_expect(mpscrb_is_full(&rb) == true);
    mpsc_expect(mpscrb_in(&rb, &in_item) == false); /* 满时写入失败 */

    /* peek 不消费 */
    out_item.producer_id = 0xFFu;
    out_item.seq         = 0xFFu;
    mpsc_expect(mpscrb_out_peek(&rb, &out_item) == true);
    mpsc_expect(out_item.seq == 0u);
    mpsc_expect(mpscrb_len(&rb) == MPSC_TEST_CAPACITY); /* peek 后 len 不变 */

    /* 全部消费 */
    for (uint32_t i = 0u; i < MPSC_TEST_CAPACITY; i++) {
        mpsc_expect(mpscrb_out(&rb, &out_item) == true);
        mpsc_expect(out_item.seq == i);
    }
    mpsc_expect(mpscrb_is_empty(&rb) == true);
}

/* ---- 组2/组3：并发测试上下文 ---- */

typedef struct
{
    MpscRingbuf rb;
    uint32_t targetPerProducer;
    OmAtomicUint totalProduced;
    OmAtomicUint totalConsumed;
    OmAtomicUint producerReady[MPSC_TEST_PRODUCER_COUNT];
    OmAtomicUint producerDone[MPSC_TEST_PRODUCER_COUNT];
    volatile uint32_t consumerStarted;
    volatile uint32_t consumerDone;
    OmAtomicUint errors;
    OmAtomicUint stop;
} MpscConcurrentCtx;

typedef struct
{
    MpscConcurrentCtx *ctx;
    uint32_t producer_id;
} MpscProducerArg;

static void mpsc_producer_entry(void *arg)
{
    MpscProducerArg *parg  = (MpscProducerArg *)arg;
    MpscConcurrentCtx *ctx = parg->ctx;
    uint32_t id            = parg->producer_id;
    uint32_t seq           = 0u;
    MpscTestItem item;

    item.producer_id = id;

    OM_STORE_REL(&ctx->producerReady[id], 1u);

    while (seq < ctx->targetPerProducer && OM_LOAD_ACQ(&ctx->stop) == 0u) {
        item.seq = seq;
        if (mpscrb_in(&ctx->rb, &item)) {
            OM_INC_AR(&ctx->totalProduced);
            seq++;
            continue;
        }
        (void)osal_sleep_ms(1u); /* 让出 CPU，等待消费者腾出空间 */
    }

    OM_STORE_REL(&ctx->producerDone[id], 1u);
    osal_thread_exit();
}

static void mpsc_consumer_entry(void *arg)
{
    MpscConcurrentCtx *ctx                          = (MpscConcurrentCtx *)arg;
    uint32_t total                                  = ctx->targetPerProducer * MPSC_TEST_PRODUCER_COUNT;
    uint32_t expected_seq[MPSC_TEST_PRODUCER_COUNT] = {0u};
    uint32_t consecutive_empty                      = 0u;
    OsalTimeMs deadline_ms;

    ctx->consumerStarted = 1u;
    deadline_ms          = osal_time_now_monotonic() + MPSC_TEST_CONSUMER_TIMEOUT;

    while (OM_LOAD_ACQ(&ctx->totalConsumed) < total && OM_LOAD_ACQ(&ctx->stop) == 0u) {
        MpscTestItem item;

        if (mpscrb_out(&ctx->rb, &item)) {
            if (item.producer_id >= MPSC_TEST_PRODUCER_COUNT ||
                item.seq != expected_seq[item.producer_id]) {
                OM_INC_AR(&ctx->errors);
                OM_STORE_REL(&ctx->stop, 1u);
                break;
            }
            expected_seq[item.producer_id]++;
            OM_INC_AR(&ctx->totalConsumed);
            consecutive_empty = 0u;
            continue;
        }

        consecutive_empty++;
        if (!osal_time_before(osal_time_now_monotonic(), deadline_ms)) {
            OM_INC_AR(&ctx->errors);
            OM_STORE_REL(&ctx->stop, 1u);
            break;
        }
        (void)osal_sleep_ms(0u);
    }

    ctx->consumerDone = 1u;
    osal_thread_exit();
}

/**
 * @brief 运行多 Task MPSC 并发测试
 * @param producer_priority 生产者线程优先级
 */
static void mpsc_run_concurrent_test(uint32_t producer_priority)
{
    uint8_t buf[MPSC_TEST_CAPACITY * sizeof(MpscTestItem)];
    OmAtomicU8 ready[MPSC_TEST_CAPACITY];
    MpscConcurrentCtx ctx;

    OsalThreadAttr producer_attr = {
        "mpsc_prod",
        512u * OSAL_STACK_WORD_BYTES,
        producer_priority,
    };
    OsalThreadAttr consumer_attr = {
        "mpsc_cons",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    OsalThread *producer_threads[MPSC_TEST_PRODUCER_COUNT] = {NULL};
    MpscProducerArg producer_args[MPSC_TEST_PRODUCER_COUNT];
    OsalThread *consumer_thread = NULL;
    uint32_t total_target;
    uint32_t i;

    memset(&ctx, 0, sizeof(ctx));
    ctx.targetPerProducer = MPSC_TEST_PRODUCER_ROUNDS;
    total_target          = ctx.targetPerProducer * MPSC_TEST_PRODUCER_COUNT;

    mpsc_expect(mpscrb_init(&ctx.rb, buf, ready, sizeof(MpscTestItem), MPSC_TEST_CAPACITY) == true);

    /* 创建消费者 */
    mpsc_expect(osal_thread_create(&consumer_thread, &consumer_attr, mpsc_consumer_entry, &ctx) == OSAL_OK);
    mpsc_expect(mpsc_wait_flag(&ctx.consumerStarted, 1000u) == 1);

    /* 创建生产者 */
    for (i = 0u; i < MPSC_TEST_PRODUCER_COUNT; i++) {
        producer_args[i].ctx         = &ctx;
        producer_args[i].producer_id = i;
        mpsc_expect(osal_thread_create(&producer_threads[i], &producer_attr, mpsc_producer_entry,
                                       &producer_args[i]) == OSAL_OK);
    }

    /* 等待消费者完成 */
    mpsc_expect(mpsc_wait_flag(&ctx.consumerDone, MPSC_TEST_CONSUMER_TIMEOUT) == 1);

    /* 验证结果 */
    mpsc_expect(OM_LOAD_ACQ(&ctx.errors) == 0u);
    mpsc_expect(OM_LOAD_ACQ(&ctx.totalProduced) == total_target);
    mpsc_expect(OM_LOAD_ACQ(&ctx.totalConsumed) == total_target);
    mpsc_expect(mpscrb_is_empty(&ctx.rb) == true);
}

/* ---- 测试主入口 ---- */

static OsalThread *g_test_thread = NULL;

static void mpsc_test_thread_entry(void *arg)
{
    (void)arg;

    /* 组1：基础合同 */
    mpsc_run_basic_tests();

    /* 组2：多 Task MPSC（生产者优先级 == 消费者优先级） */
    mpsc_run_concurrent_test(2u);

    /* 组3：高优先级 Task 模拟 ISR 生产者 */
    mpsc_run_concurrent_test(OSAL_PRIO_HIGHEST);

    g_result.done = 1u;

    for (;;)
        (void)osal_sleep_ms(1000u);
}

/* app 自身启动设置：经 OM_INIT_APPLICATION 分散加载，init 线程（调度器后）自动调用 */
static OmRet mpsc_app_setup(void)
{
    OsalThreadAttr test_attr = {
        "mpsc_test",
        768u * OSAL_STACK_WORD_BYTES,
        OSAL_PRIO_IDLE_BASE + 2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, mpsc_test_thread_entry, NULL) != OSAL_OK)
        return OM_ERR_NO_MEM;

    return OM_OK;
}
OM_INIT_APPLICATION(mpsc_app_setup);

/**
 * @file main.c
 * @brief IPC Pipe 单向管道独立例程
 * @details
 * 本例程覆盖 Pipe 的基础合同与流式传输场景。
 * - 组1：参数校验（NULL 指针、非法容量、ISR 上下文守卫）
 * - 组2：生命周期（init/deinit、alloc/free）
 * - 组3：基础写入与读取（消费式、窥视、跳过）
 * - 组4：阻塞超时（write 满阻塞、read 空阻塞、超时返回）
 * - 组5：非阻塞模式（WOULD_BLOCK 语义）
 * - 组6：Task→Task 流式传输（写者线程 + 读者线程，验证数据完整性）
 * - 组7：线程模拟 ISR 写入（高优先级写者 + 阻塞读者，验证 from_isr 路径）
 * - 判定标准：`g_result.done == 1` 且 `g_result.failed == 0`
 */
#include "core/om_def.h"
#include "osal/osal.h"
#include "ipc/pipe.h"

/* ---- 用户宏配置 ---- */

#ifndef PIPE_TEST_BUF_CAPACITY
#define PIPE_TEST_BUF_CAPACITY 64u
#endif

#ifndef PIPE_TEST_STRESS_ROUNDS
#define PIPE_TEST_STRESS_ROUNDS 2000u
#endif

#ifndef PIPE_TEST_STRESS_PAYLOAD_LEN
#define PIPE_TEST_STRESS_PAYLOAD_LEN 7u
#endif

#ifndef PIPE_TEST_ISR_STRESS_ROUNDS
#define PIPE_TEST_ISR_STRESS_ROUNDS 500u
#endif

/* ---- 测试基础设施 ---- */

typedef struct {
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} PipeTestResult;

static PipeTestResult g_result = {0u, 0u, 0u};
static OsalThread* g_test_thread = NULL;

static void pipe_expect(int condition)
{
    g_result.total++;
    if (!condition)
        g_result.failed++;
}

/* ---- 测试用全局资源 ---- */

static Pipe g_pipe = {{0}, NULL, NULL};
static uint8_t g_pipe_buf[PIPE_TEST_BUF_CAPACITY] = {0};

/* ---- 组6：Task→Task 流式传输 ---- */

static volatile uint32_t g_writer_done = 0u;
static volatile uint32_t g_reader_done = 0u;
static volatile uint32_t g_writer_ok = 0u;
static volatile uint32_t g_reader_ok = 0u;
static volatile uint32_t g_writer_err = 0u;
static volatile uint32_t g_reader_err = 0u;
static OsalThread* g_writer_thread = NULL;
static OsalThread* g_reader_thread = NULL;

static void pipe_writer_thread_entry(void* arg)
{
    (void)arg;
    uint32_t round = 0u;
    uint8_t payload[PIPE_TEST_STRESS_PAYLOAD_LEN];

    for (round = 0u; round < PIPE_TEST_STRESS_ROUNDS; round++) {
        uint32_t i;
        for (i = 0u; i < PIPE_TEST_STRESS_PAYLOAD_LEN; i++)
            payload[i] = (uint8_t)(round + i);

        int n = pipe_write(&g_pipe, payload, (int)PIPE_TEST_STRESS_PAYLOAD_LEN, OSAL_WAIT_FOREVER);
        if (n == (int)PIPE_TEST_STRESS_PAYLOAD_LEN)
            g_writer_ok++;
        else
            g_writer_err++;
    }

    g_writer_done = 1u;
    osal_thread_exit();
}

static void pipe_reader_thread_entry(void* arg)
{
    (void)arg;
    uint32_t round = 0u;
    uint8_t expected[PIPE_TEST_STRESS_PAYLOAD_LEN];
    uint8_t actual[PIPE_TEST_STRESS_PAYLOAD_LEN];

    for (round = 0u; round < PIPE_TEST_STRESS_ROUNDS; round++) {
        uint32_t i;
        int n = pipe_read(&g_pipe, actual, (int)PIPE_TEST_STRESS_PAYLOAD_LEN, OSAL_WAIT_FOREVER);
        if (n != (int)PIPE_TEST_STRESS_PAYLOAD_LEN) {
            g_reader_err++;
            continue;
        }

        for (i = 0u; i < PIPE_TEST_STRESS_PAYLOAD_LEN; i++)
            expected[i] = (uint8_t)(round + i);

        int match = 1;
        for (i = 0u; i < PIPE_TEST_STRESS_PAYLOAD_LEN; i++) {
            if (actual[i] != expected[i]) {
                match = 0;
                break;
            }
        }

        if (match)
            g_reader_ok++;
        else
            g_reader_err++;
    }

    g_reader_done = 1u;
    osal_thread_exit();
}

/* ---- 组7：线程模拟 ISR 写入 ---- */

static volatile uint32_t g_isr_writer_done = 0u;
static volatile uint32_t g_isr_writer_ok = 0u;
static volatile uint32_t g_isr_writer_would_block = 0u;
static volatile uint32_t g_isr_reader_done = 0u;
static volatile uint32_t g_isr_reader_ok = 0u;
static volatile uint32_t g_isr_reader_err = 0u;
static OsalThread* g_isr_writer_thread = NULL;
static OsalThread* g_isr_reader_thread = NULL;

static void pipe_isr_writer_thread_entry(void* arg)
{
    (void)arg;
    uint32_t round = 0u;
    uint8_t payload[4];

    for (round = 0u; round < PIPE_TEST_ISR_STRESS_ROUNDS; round++) {
        payload[0] = (uint8_t)(round);
        payload[1] = (uint8_t)(round >> 8u);
        payload[2] = (uint8_t)(round >> 16u);
        payload[3] = (uint8_t)(round >> 24u);

        int n = pipe_write_from_isr(&g_pipe, payload, 4);
        if (n > 0)
            g_isr_writer_ok++;
        else if (n == OM_ERROR_WOULD_BLOCK)
            g_isr_writer_would_block++;

        if ((round & 0x00FFu) == 0u)
            (void)osal_sleep_ms(1u);
    }

    g_isr_writer_done = 1u;
    osal_thread_exit();
}

static void pipe_isr_reader_thread_entry(void* arg)
{
    (void)arg;
    uint32_t total_read = 0u;

    while (total_read < PIPE_TEST_ISR_STRESS_ROUNDS) {
        uint8_t buf[4];
        int n = pipe_read(&g_pipe, buf, 4, 100u);
        if (n == 4) {
            g_isr_reader_ok++;
            total_read++;
        } else if (n < 0 && n != OM_ERROR_WOULD_BLOCK && n != OM_ERROR_TIMEOUT) {
            g_isr_reader_err++;
            break;
        }

        if (g_isr_writer_done && pipe_is_empty(&g_pipe))
            break;
    }

    g_isr_reader_done = 1u;
    osal_thread_exit();
}

/* ---- 辅助：等待标志位 ---- */

static int pipe_wait_flag(volatile uint32_t* flag, uint32_t timeout_ms)
{
    OsalTimeMs start = osal_time_now_monotonic();
    OsalTimeMs deadline = start + timeout_ms;

    while (*flag == 0u) {
        if (!osal_time_before(osal_time_now_monotonic(), deadline))
            return 0;
        (void)osal_sleep_ms(1u);
    }
    return 1;
}

/* ---- 测试主线程 ---- */

static void pipe_test_thread_entry(void* arg)
{
    uint8_t test_buf[16];
    uint8_t verify_buf[16];
    uint32_t i;

    (void)arg;

    /* ---- 组1：参数校验 ---- */

    pipe_expect(pipe_init(NULL, g_pipe_buf, PIPE_TEST_BUF_CAPACITY) == OM_ERROR_PARAM);
    pipe_expect(pipe_init(&g_pipe, NULL, PIPE_TEST_BUF_CAPACITY) == OM_ERROR_PARAM);
    pipe_expect(pipe_init(&g_pipe, g_pipe_buf, 0u) == OM_ERROR_PARAM);
    pipe_expect(pipe_init(&g_pipe, g_pipe_buf, 3u) == OM_ERROR_PARAM);
    pipe_expect(pipe_alloc(NULL, PIPE_TEST_BUF_CAPACITY, NULL) == OM_ERROR_PARAM);
    pipe_expect(pipe_alloc(&g_pipe, 0u, NULL) == OM_ERROR_PARAM);
    pipe_expect(pipe_write(NULL, "x", 1, 0u) == OM_ERROR_PARAM);
    pipe_expect(pipe_write(&g_pipe, NULL, 1, 0u) == OM_ERROR_PARAM);
    pipe_expect(pipe_write(&g_pipe, "x", 0, 0u) == OM_ERROR_PARAM);
    pipe_expect(pipe_write_from_isr(NULL, "x", 1) == OM_ERROR_PARAM);
    pipe_expect(pipe_read(NULL, test_buf, 1, 0u) == OM_ERROR_PARAM);
    pipe_expect(pipe_read(&g_pipe, NULL, 1, 0u) == OM_ERROR_PARAM);
    pipe_expect(pipe_peek(NULL, test_buf, 1, 0u) == OM_ERROR_PARAM);
    pipe_expect(pipe_skip(NULL, 1) == OM_ERROR_PARAM);
    pipe_expect(pipe_skip(&g_pipe, 0) == OM_ERROR_PARAM);

    /* ---- 组2：生命周期 ---- */

    pipe_expect(pipe_init(&g_pipe, g_pipe_buf, PIPE_TEST_BUF_CAPACITY) == OM_OK);
    pipe_expect(pipe_is_empty(&g_pipe));
    pipe_expect(!pipe_is_full(&g_pipe));
    pipe_expect(pipe_len(&g_pipe) == 0);
    pipe_expect(pipe_cap(&g_pipe) == (int)PIPE_TEST_BUF_CAPACITY);
    pipe_expect(pipe_avail(&g_pipe) == (int)PIPE_TEST_BUF_CAPACITY);
    pipe_deinit(&g_pipe);

    {
        Pipe alloc_pipe = {{0}, NULL, NULL};
        OmRet ret = pipe_alloc(&alloc_pipe, 64u, NULL);
        pipe_expect(ret == OM_OK);
        if (ret == OM_OK)
            pipe_free(&alloc_pipe, NULL);
    }

    /* ---- 组3：基础写入与读取 ---- */

    pipe_expect(pipe_init(&g_pipe, g_pipe_buf, PIPE_TEST_BUF_CAPACITY) == OM_OK);

    for (i = 0u; i < 16u; i++)
        test_buf[i] = (uint8_t)i;

    {
        int n = pipe_write(&g_pipe, test_buf, 16, 0u);
        pipe_expect(n == 16);
        pipe_expect(!pipe_is_empty(&g_pipe));
        pipe_expect(pipe_len(&g_pipe) == 16);
    }

    {
        int n = pipe_peek(&g_pipe, verify_buf, 16, 0u);
        pipe_expect(n == 16);
        pipe_expect(pipe_len(&g_pipe) == 16);
        {
            int match = 1;
            for (i = 0u; i < 16u; i++) {
                if (verify_buf[i] != (uint8_t)i) { match = 0; break; }
            }
            pipe_expect(match);
        }
    }

    {
        int n = pipe_read(&g_pipe, verify_buf, 8, 0u);
        pipe_expect(n == 8);
        pipe_expect(pipe_len(&g_pipe) == 8);
        {
            int match = 1;
            for (i = 0u; i < 8u; i++) {
                if (verify_buf[i] != (uint8_t)i) { match = 0; break; }
            }
            pipe_expect(match);
        }
    }

    {
        OmRet ret = pipe_skip(&g_pipe, 8);
        pipe_expect(ret == OM_OK);
        pipe_expect(pipe_is_empty(&g_pipe));
    }

    {
        OmRet ret = pipe_skip(&g_pipe, 1);
        pipe_expect(ret == OM_ERROR_EMPTY);
    }

    pipe_deinit(&g_pipe);

    /* ---- 组4：阻塞超时 ---- */

    pipe_expect(pipe_init(&g_pipe, g_pipe_buf, PIPE_TEST_BUF_CAPACITY) == OM_OK);

    {
        int n = pipe_read(&g_pipe, verify_buf, 1, 10u);
        pipe_expect(n == OM_ERROR_TIMEOUT);
    }

    {
        while (pipe_avail(&g_pipe) > 0) {
            int n = pipe_write(&g_pipe, test_buf, 1, 0u);
            if (n <= 0) break;
        }
        pipe_expect(pipe_is_full(&g_pipe));

        {
            int n = pipe_write(&g_pipe, test_buf, 1, 10u);
            pipe_expect(n == OM_ERROR_TIMEOUT);
        }
    }

    pipe_deinit(&g_pipe);

    /* ---- 组5：非阻塞模式 ---- */

    pipe_expect(pipe_init(&g_pipe, g_pipe_buf, PIPE_TEST_BUF_CAPACITY) == OM_OK);

    {
        int n = pipe_read(&g_pipe, verify_buf, 1, 0u);
        pipe_expect(n == OM_ERROR_WOULD_BLOCK);
    }

    {
        while (pipe_avail(&g_pipe) > 0) {
            int n = pipe_write(&g_pipe, test_buf, 1, 0u);
            if (n <= 0) break;
        }
        {
            int n = pipe_write(&g_pipe, test_buf, 1, 0u);
            pipe_expect(n == OM_ERROR_WOULD_BLOCK);
        }
    }

    {
        int n = pipe_write_from_isr(&g_pipe, test_buf, 1);
        pipe_expect(n == OM_ERROR_WOULD_BLOCK);
    }

    pipe_deinit(&g_pipe);

    /* ---- 组6：Task→Task 流式传输 ---- */

    {
        OsalThreadAttr writer_attr = {
            "pipe_writer",
            512u * OSAL_STACK_WORD_BYTES,
            2u,
        };
        OsalThreadAttr reader_attr = {
            "pipe_reader",
            512u * OSAL_STACK_WORD_BYTES,
            2u,
        };

        g_writer_done = 0u;
        g_reader_done = 0u;
        g_writer_ok = 0u;
        g_reader_ok = 0u;
        g_writer_err = 0u;
        g_reader_err = 0u;

        pipe_expect(pipe_init(&g_pipe, g_pipe_buf, PIPE_TEST_BUF_CAPACITY) == OM_OK);

        pipe_expect(osal_thread_create(&g_writer_thread, &writer_attr, pipe_writer_thread_entry, NULL) == OSAL_OK);
        pipe_expect(osal_thread_create(&g_reader_thread, &reader_attr, pipe_reader_thread_entry, NULL) == OSAL_OK);

        pipe_expect(pipe_wait_flag(&g_writer_done, 30000u) == 1);
        pipe_expect(pipe_wait_flag(&g_reader_done, 30000u) == 1);

        pipe_expect(g_writer_err == 0u);
        pipe_expect(g_reader_err == 0u);
        pipe_expect(g_writer_ok == PIPE_TEST_STRESS_ROUNDS);
        pipe_expect(g_reader_ok == PIPE_TEST_STRESS_ROUNDS);

        g_writer_thread = NULL;
        g_reader_thread = NULL;
        pipe_deinit(&g_pipe);
    }

    /* ---- 组7：线程模拟 ISR 写入 ---- */

    {
        OsalThreadAttr isr_writer_attr = {
            "pipe_isr_w",
            512u * OSAL_STACK_WORD_BYTES,
            OSAL_PRIORITY_MAX > 0u ? OSAL_PRIORITY_MAX - 1u : 0u,
        };
        OsalThreadAttr isr_reader_attr = {
            "pipe_isr_r",
            512u * OSAL_STACK_WORD_BYTES,
            2u,
        };

        g_isr_writer_done = 0u;
        g_isr_reader_done = 0u;
        g_isr_writer_ok = 0u;
        g_isr_writer_would_block = 0u;
        g_isr_reader_ok = 0u;
        g_isr_reader_err = 0u;

        pipe_expect(pipe_init(&g_pipe, g_pipe_buf, PIPE_TEST_BUF_CAPACITY) == OM_OK);

        pipe_expect(osal_thread_create(&g_isr_reader_thread, &isr_reader_attr, pipe_isr_reader_thread_entry, NULL) == OSAL_OK);
        (void)osal_sleep_ms(10u);
        pipe_expect(osal_thread_create(&g_isr_writer_thread, &isr_writer_attr, pipe_isr_writer_thread_entry, NULL) == OSAL_OK);

        pipe_expect(pipe_wait_flag(&g_isr_writer_done, 30000u) == 1);
        pipe_expect(pipe_wait_flag(&g_isr_reader_done, 30000u) == 1);

        pipe_expect(g_isr_reader_err == 0u);
        pipe_expect(g_isr_reader_ok > 0u);
        pipe_expect(g_isr_writer_ok > 0u);

        g_isr_writer_thread = NULL;
        g_isr_reader_thread = NULL;
        pipe_deinit(&g_pipe);
    }

    /* ---- 测试结束 ---- */

    g_result.done = 1u;
    for (;;)
        (void)osal_sleep_ms(1000u);
}

int main(void)
{
    OsalThreadAttr test_attr = {
        "pipe_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, pipe_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}

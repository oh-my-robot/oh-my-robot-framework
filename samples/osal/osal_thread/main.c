/**
 * @file main.c
 * @brief OSAL thread 独立例程
 * @details
 * - 本例程用于验证 thread 生命周期合同与关键边界行为。
 * - 覆盖点：
 *   1) create 参数校验
 *   2) self / yield 基础语义
 *   3) join 能力（当前 FreeRTOS 端返回 `OSAL_NOT_SUPPORTED`）
 *   4) terminate(self) 非法约束
 *   5) terminate(other) 危险语义保留
 */
#include "osal/osal.h"
#include "osal/osal_config.h"

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} OsalThreadTestResult;

static OsalThread* g_test_thread = NULL;
static OsalThread* g_worker_thread = NULL;
static volatile uint32_t g_worker_counter = 0u;
static OsalThreadTestResult g_thread_result = {0u, 0u, 0u};

/**
 * @brief 简单断言计数器
 * @param condition 非 0 表示断言通过
 */
static void osal_thread_expect(int condition)
{
    g_thread_result.total++;
    if (!condition)
        g_thread_result.failed++;
}

/**
 * @brief 被终止目标线程
 * @note 持续更新计数，用于观察 terminate(other) 生效
 */
static void osal_thread_worker_entry(void* arg)
{
    (void)arg;

    for (;;)
    {
        g_worker_counter++;
        (void)osal_sleep_ms(5u);
    }
}

/**
 * @brief thread 合同测试线程
 */
static void osal_thread_test_entry(void* arg)
{
    OsalThreadAttr worker_attr = {
        "osal_thread_worker",
        512u * OSAL_STACK_WORD_BYTES,
        2u,
    };
    uint32_t snapshot = 0u;

    (void)arg;

    /* 组1：create 参数校验 */
    osal_thread_expect(osal_thread_create(NULL, &worker_attr, osal_thread_worker_entry, NULL) == OSAL_INVALID);
    osal_thread_expect(osal_thread_create(&g_worker_thread, &worker_attr, NULL, NULL) == OSAL_INVALID);

    /* 组2：self / yield 基础语义 */
    osal_thread_expect(osal_thread_self() != NULL);
    osal_thread_yield();
    osal_thread_expect(1);

    /* 组3：join 能力（当前端口应返回 NOT_SUPPORTED） */
    osal_thread_expect(osal_thread_join(NULL, 0u) == OSAL_INVALID);
    osal_thread_expect(osal_thread_create(&g_worker_thread, &worker_attr, osal_thread_worker_entry, NULL) == OSAL_OK);
    osal_thread_expect(osal_thread_join(g_worker_thread, 0u) == OSAL_NOT_SUPPORTED);

    /* 组4：terminate(self) 非法约束 */
    osal_thread_expect(osal_thread_terminate(osal_thread_self()) == OSAL_INVALID);

    /* 组5：terminate(other) 危险语义保留 */
    (void)osal_sleep_ms(30u);
    osal_thread_expect(g_worker_counter > 0u);
    osal_thread_expect(osal_thread_terminate(g_worker_thread) == OSAL_OK);
    g_worker_thread = NULL;

    snapshot = g_worker_counter;
    (void)osal_sleep_ms(20u);
    osal_thread_expect(g_worker_counter == snapshot);

    g_thread_result.done = 1u;
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
        "osal_thread_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_thread_test_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}

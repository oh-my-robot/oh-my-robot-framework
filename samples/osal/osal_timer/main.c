/**
 * @file main.c
 * @brief OSAL timer 独立例程
 * @details
 * - 本例程用于验证 timer 的生命周期合同与关键边界语义。
 * - 覆盖点：
 *   1) create 参数校验与模式校验
 *   2) get_id/set_id 一致性
 *   3) reset 语义（未运行时等价 start）
 *   4) start/stop/reset/delete 空句柄防护
 *   5) one-shot 与 periodic 行为
 */
#include "osal/osal.h"
#include "osal/osal_config.h"

#define OSAL_TIMER_PERIODIC_PERIOD_MS (20u)
#define OSAL_TIMER_ONE_SHOT_PERIOD_MS (30u)

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} OsalTimerTestResult;

static OsalThread* g_test_thread = NULL;
static OsalTimer* g_periodic_timer = NULL;
static OsalTimer* g_one_shot_timer = NULL;
static OsalTimerTestResult g_timer_result = {0u, 0u, 0u};
static volatile uint32_t g_periodic_hits = 0u;
static volatile uint32_t g_periodic_id_matches = 0u;
static volatile uint32_t g_one_shot_hits = 0u;
static uint32_t g_periodic_id_a = 0xA5A5A5A5u;
static uint32_t g_periodic_id_b = 0x5A5A5A5Au;

/**
 * @brief 简单断言计数器
 * @param condition 非 0 表示断言通过
 */
static void osal_timer_expect(int condition)
{
    g_timer_result.total++;
    if (!condition)
        g_timer_result.failed++;
}

/**
 * @brief 周期定时器回调（非阻塞）
 */
static void osal_periodic_timer_cb(OsalTimer* timer)
{
    if (osal_timer_get_id(timer) == &g_periodic_id_b)
        g_periodic_id_matches++;
    g_periodic_hits++;
}

/**
 * @brief 单次定时器回调（非阻塞）
 */
static void osal_one_shot_timer_cb(OsalTimer* timer)
{
    (void)timer;
    g_one_shot_hits++;
}

/**
 * @brief timer 合同测试线程
 */
static void osal_timer_test_thread_entry(void* arg)
{
    uint32_t snapshot = 0u;
    uint32_t one_shot_snapshot = 0u;

    (void)arg;

    /* 组1：create 参数与模式校验 */
    osal_timer_expect(osal_timer_create(NULL, "bad", 1u, OSAL_TIMER_PERIODIC, NULL, osal_periodic_timer_cb) ==
                      OSAL_INVALID);
    osal_timer_expect(osal_timer_create(&g_periodic_timer, "bad", 0u, OSAL_TIMER_PERIODIC, NULL, osal_periodic_timer_cb) ==
                      OSAL_INVALID);
    osal_timer_expect(osal_timer_create(&g_periodic_timer, "bad", 1u, OSAL_TIMER_PERIODIC, NULL, NULL) == OSAL_INVALID);
    osal_timer_expect(osal_timer_create(&g_periodic_timer, "bad", 1u, (OsalTimerMode)2u, NULL, osal_periodic_timer_cb) ==
                      OSAL_INVALID);

    /* 组2：创建 periodic 定时器与 id 读写 */
    osal_timer_expect(osal_timer_create(&g_periodic_timer, "osal_timer_periodic", OSAL_TIMER_PERIODIC_PERIOD_MS,
                                        OSAL_TIMER_PERIODIC, &g_periodic_id_a, osal_periodic_timer_cb) == OSAL_OK);
    osal_timer_expect(osal_timer_get_id(g_periodic_timer) == &g_periodic_id_a);
    osal_timer_set_id(g_periodic_timer, &g_periodic_id_b);
    osal_timer_expect(osal_timer_get_id(g_periodic_timer) == &g_periodic_id_b);

    /* 组3：空句柄防护 */
    osal_timer_expect(osal_timer_start(NULL, 0u) == OSAL_INVALID);
    osal_timer_expect(osal_timer_stop(NULL, 0u) == OSAL_INVALID);
    osal_timer_expect(osal_timer_reset(NULL, 0u) == OSAL_INVALID);
    osal_timer_expect(osal_timer_delete(NULL, 0u) == OSAL_INVALID);
    osal_timer_expect(osal_timer_get_id(NULL) == NULL);
    osal_timer_set_id(NULL, &g_periodic_id_a);
    osal_timer_expect(1);

    /* 组4：reset（未运行态）应等价 start */
    snapshot = g_periodic_hits;
    osal_timer_expect(osal_timer_reset(g_periodic_timer, 0u) == OSAL_OK);
    (void)osal_sleep_ms(90u);
    osal_timer_expect(g_periodic_hits > snapshot);
    osal_timer_expect(g_periodic_id_matches > 0u);

    /* 组5：stop 后不再触发 */
    snapshot = g_periodic_hits;
    osal_timer_expect(osal_timer_stop(g_periodic_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    (void)osal_sleep_ms(70u);
    osal_timer_expect(g_periodic_hits == snapshot);

    /* 组6：运行中 reset 保持重装语义 */
    osal_timer_expect(osal_timer_start(g_periodic_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    (void)osal_sleep_ms(40u);
    snapshot = g_periodic_hits;
    osal_timer_expect(osal_timer_reset(g_periodic_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    (void)osal_sleep_ms(80u);
    osal_timer_expect(g_periodic_hits > snapshot);

    /* 组7：one-shot 仅触发一次 */
    osal_timer_expect(osal_timer_create(&g_one_shot_timer, "osal_timer_one_shot", OSAL_TIMER_ONE_SHOT_PERIOD_MS,
                                        OSAL_TIMER_ONE_SHOT, NULL, osal_one_shot_timer_cb) == OSAL_OK);
    osal_timer_expect(osal_timer_start(g_one_shot_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    (void)osal_sleep_ms(120u);
    one_shot_snapshot = g_one_shot_hits;
    osal_timer_expect(one_shot_snapshot == 1u);
    (void)osal_sleep_ms(80u);
    osal_timer_expect(g_one_shot_hits == one_shot_snapshot);

    /* 组8：资源释放 */
    osal_timer_expect(osal_timer_stop(g_periodic_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    osal_timer_expect(osal_timer_delete(g_one_shot_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    g_one_shot_timer = NULL;
    osal_timer_expect(osal_timer_delete(g_periodic_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    g_periodic_timer = NULL;

    g_timer_result.done = 1u;
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
        "osal_timer_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_timer_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}

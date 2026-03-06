/**
 * @file main.c
 * @brief OSAL time 独立例程
 * @details
 * - 本例程用于验证 time 原语合同，覆盖单位转换、回绕比较、sleep 与 delay_until 关键语义。
 * - 观测变量：
 *   - `g_time_result.total`：累计断言数量
 *   - `g_time_result.failed`：失败断言数量
 *   - `g_time_result.done`：用例是否执行完成（1 表示完成）
 */
#include <stdint.h>

#include "osal/osal.h"
#include "osal/osal_config.h"

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} OsalTimeTestResult;

static OsalThread* g_test_thread = NULL;
static OsalTimeTestResult g_time_result = {0u, 0u, 0u};

/**
 * @brief 简单断言计数器
 * @param condition 非 0 表示断言通过
 */
static void osal_time_expect(int condition)
{
    g_time_result.total++;
    if (!condition)
        g_time_result.failed++;
}

/**
 * @brief 时间转换语义测试
 * @details 验证 us/ns 到 ms 的向上取整边界
 */
static void osal_time_test_conversion_group(void)
{
    osal_time_expect(osal_time_us_to_ms_ceil(0ULL) == 0u);
    osal_time_expect(osal_time_us_to_ms_ceil(1ULL) == 1u);
    osal_time_expect(osal_time_us_to_ms_ceil(999ULL) == 1u);
    osal_time_expect(osal_time_us_to_ms_ceil(1000ULL) == 1u);
    osal_time_expect(osal_time_us_to_ms_ceil(1001ULL) == 2u);

    osal_time_expect(osal_time_ns_to_ms_ceil(0ULL) == 0u);
    osal_time_expect(osal_time_ns_to_ms_ceil(1ULL) == 1u);
    osal_time_expect(osal_time_ns_to_ms_ceil(999999ULL) == 1u);
    osal_time_expect(osal_time_ns_to_ms_ceil(1000000ULL) == 1u);
    osal_time_expect(osal_time_ns_to_ms_ceil(1000001ULL) == 2u);
}

/**
 * @brief 单调时钟与回绕安全比较测试
 * @details
 * 1) 实时读数不应倒退
 * 2) `osal_time_before/after` 对自然回绕场景可用
 */
static void osal_time_test_monotonic_group(void)
{
    OsalTimeMs now_first = osal_time_now_monotonic();
    OsalTimeMs now_second = osal_time_now_monotonic();

    osal_time_expect(!osal_time_before(now_second, now_first));

    osal_time_expect(osal_time_after(11u, 10u));
    osal_time_expect(osal_time_before(10u, 11u));

    osal_time_expect(osal_time_after(0x00000010u, 0xFFFFFFF0u));
    osal_time_expect(osal_time_before(0xFFFFFFF0u, 0x00000010u));
}

/**
 * @brief sleep 语义测试
 * @details
 * - `OSAL_WAIT_FOREVER` 对 sleep 非法
 * - 0ms/1ms sleep 应返回成功
 */
static void osal_time_test_sleep_group(void)
{
    osal_time_expect(osal_sleep_ms(OSAL_WAIT_FOREVER) == OSAL_INVALID);
    osal_time_expect(osal_sleep_ms(0u) == OSAL_OK);
    osal_time_expect(osal_sleep_ms(1u) == OSAL_OK);
}

/**
 * @brief delay_until 基础语义测试
 * @details
 * 1) 参数校验
 * 2) 首次 `cursor=0` 自动初始化；
 * 3) 正常周期推进只推进一个周期
 */
static void osal_time_test_delay_until_base_group(void)
{
    OsalTimeMs deadline_cursor_ms = 0u;
    OsalTimeMs now_before_ms = 0u;
    OsalTimeMs expected_next_deadline_ms = 0u;
    uint32_t missed_periods = 0u;

    osal_time_expect(osal_delay_until(NULL, 10u, NULL) == OSAL_INVALID);
    osal_time_expect(osal_delay_until(&deadline_cursor_ms, 0u, NULL) == OSAL_INVALID);

    now_before_ms = osal_time_now_monotonic();
    deadline_cursor_ms = 0u;
    missed_periods = 0xFFFFFFFFu;
    osal_time_expect(osal_delay_until(&deadline_cursor_ms, 5u, &missed_periods) == OSAL_OK);
    osal_time_expect(!osal_time_before(deadline_cursor_ms, (OsalTimeMs)(now_before_ms + 5u)));
    osal_time_expect(missed_periods == 0u);

    deadline_cursor_ms = (OsalTimeMs)(osal_time_now_monotonic() + 3u);
    expected_next_deadline_ms = (OsalTimeMs)(deadline_cursor_ms + 7u);
    missed_periods = 0xFFFFFFFFu;
    osal_time_expect(osal_delay_until(&deadline_cursor_ms, 7u, &missed_periods) == OSAL_OK);
    osal_time_expect(deadline_cursor_ms == expected_next_deadline_ms);
    osal_time_expect(missed_periods == 0u);
}

/**
 * @brief delay_until 过期追赶语义测试
 * @details
 * - 当 deadline 已过期时应返回 `OSAL_OK`，并统计 `missed_periods`
 * - 本轮调用后游标仅推进一个周期（不跨多周期跳跃）
 */
static void osal_time_test_delay_until_catchup_group(void)
{
    OsalTimeMs now_ms = osal_time_now_monotonic();
    OsalTimeMs old_deadline_ms = (OsalTimeMs)(now_ms - 25u);
    OsalTimeMs deadline_cursor_ms = old_deadline_ms;
    uint32_t missed_periods = 0u;

    osal_time_expect(osal_delay_until(&deadline_cursor_ms, 10u, &missed_periods) == OSAL_OK);
    osal_time_expect(missed_periods >= 2u);
    osal_time_expect(deadline_cursor_ms == (OsalTimeMs)(old_deadline_ms + 10u));

    now_ms = osal_time_now_monotonic();
    old_deadline_ms = (OsalTimeMs)(now_ms - 8u);
    deadline_cursor_ms = old_deadline_ms;
    osal_time_expect(osal_delay_until(&deadline_cursor_ms, 4u, NULL) == OSAL_OK);
    osal_time_expect(deadline_cursor_ms == (OsalTimeMs)(old_deadline_ms + 4u));
}

/**
 * @brief time 语义测试线程
 * @details 验证点分五组：
 * 1) 单位转换边界
 * 2) 单调时钟与回绕比较
 * 3) sleep 合同
 * 4) delay_until 基础行为
 * 5) delay_until 过期追赶行为
 */
static void osal_time_test_thread_entry(void* arg)
{
    (void)arg;

    osal_time_test_conversion_group();
    osal_time_test_monotonic_group();
    osal_time_test_sleep_group();
    osal_time_test_delay_until_base_group();
    osal_time_test_delay_until_catchup_group();

    g_time_result.done = 1u;
    for (;;)
        osal_sleep_ms(1000u);
}

/**
 * @brief 例程入口
 * @return 创建测试线程失败返回 -1；成功后启动调度
 */
int main(void)
{
    OsalThreadAttr test_attr = {
        "osal_time_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_time_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}

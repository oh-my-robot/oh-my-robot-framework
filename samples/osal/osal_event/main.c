/**
 * @file main.c
 * @brief OSAL event_flags 独立例程
 * @details
 * - 本例程用于验证 event_flags 的核心语义与边界行为。
 * - 观测变量：
 *   - `g_event_result.total`：累计断言数量
 *   - `g_event_result.failed`：失败断言数量
 *   - `g_event_result.done`：用例是否执行完成（1 表示完成）
 */
#include "osal/osal.h"
#include "osal/osal_config.h"

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} OsalEventTestResult;

static OsalEventFlags* g_event              = NULL;
static OsalThread* g_test_thread             = NULL;
static OsalThread* g_setter_thread           = NULL;
static OsalEventTestResult g_event_result = {0u, 0u, 0u};

/**
 * @brief 简单断言计数器
 * @param condition 非 0 表示断言通过
 */
static void osal_event_expect(int condition)
{
    g_event_result.total++;
    if (!condition)
        g_event_result.failed++;
}

/**
 * @brief 辅助线程：延时后设置事件位
 * @note 用于触发 wait 的唤醒路径
 */
static void osal_event_setter_thread_entry(void *arg)
{
    uint32_t flags_to_set = (uint32_t)(uintptr_t)arg;

    (void)osal_sleep_ms(20u);
    (void)osal_event_flags_set(g_event, flags_to_set);
    osal_thread_exit();
}

/**
 * @brief event_flags 语义测试线程
 * @details 验证点分七组：
 * 1) 创建参数校验与基础创建/删除
 * 2) wait 参数与超时边界
 * 3) wait_any + clear 语义
 * 4) wait_all 语义
 * 5) NO_CLEAR 语义
 * 6) 可用位掩码非法输入校验
 * 7) from_isr 在线程上下文误用（assert 关闭时执行）
 */
static void osal_event_test_thread_entry(void *arg)
{
    OsalEventFlags* event_temp  = NULL;
    OsalThreadAttr setter_attr = {
        "osal_event_setter",
        512u * OSAL_STACK_WORD_BYTES,
        2u,
    };
    uint32_t out_value          = 0u;
    uint32_t invalid_flags      = ~OSAL_EVENT_FLAGS_USABLE_MASK;
    uint32_t invalid_single_bit = invalid_flags & (0u - invalid_flags);

    (void)arg;

    /* 组1：创建参数与基础行为 */
    osal_event_expect(osal_event_flags_create(NULL) == OSAL_INVALID);
    osal_event_expect(osal_event_flags_create(&g_event) == OSAL_OK);
    osal_event_expect(osal_event_flags_delete(NULL) == OSAL_INVALID);

    /* 组2：wait 参数与超时边界 */
    osal_event_expect(osal_event_flags_wait(g_event, 0u, &out_value, 0u, 0u) == OSAL_INVALID);
    osal_event_expect(osal_event_flags_wait(g_event, 0x01u, &out_value, 0u, 0u) == OSAL_WOULD_BLOCK);

    /* 组3：wait_any + clear 语义 */
    osal_event_expect(osal_thread_create(&g_setter_thread, &setter_attr, osal_event_setter_thread_entry, (void *)0x01u) ==
                      OSAL_OK);
    osal_event_expect(osal_event_flags_wait(g_event, 0x01u, &out_value, OSAL_WAIT_FOREVER, 0u) == OSAL_OK);
    osal_event_expect((out_value & 0x01u) != 0u);
    osal_event_expect(osal_event_flags_wait(g_event, 0x01u, &out_value, 0u, 0u) == OSAL_WOULD_BLOCK);

    /* 组4：wait_all 语义 */
    osal_event_expect(osal_event_flags_set(g_event, 0x03u) == OSAL_OK);
    osal_event_expect(osal_event_flags_wait(g_event, 0x03u, &out_value, 0u, OSAL_EVENT_FLAGS_WAIT_ALL) == OSAL_OK);
    osal_event_expect((out_value & 0x03u) == 0x03u);

    /* 组5：NO_CLEAR 语义 */
    osal_event_expect(osal_event_flags_set(g_event, 0x04u) == OSAL_OK);
    osal_event_expect(osal_event_flags_wait(g_event, 0x04u, &out_value, 0u, OSAL_EVENT_FLAGS_NO_CLEAR) == OSAL_OK);
    osal_event_expect(osal_event_flags_wait(g_event, 0x04u, &out_value, 0u, 0u) == OSAL_OK);
    osal_event_expect(osal_event_flags_clear(g_event, 0x04u) == OSAL_OK);
    osal_event_expect(osal_event_flags_wait(g_event, 0x04u, &out_value, 0u, 0u) == OSAL_WOULD_BLOCK);

    /* 组6：可用位掩码非法输入校验 */
    if (invalid_single_bit != 0u) {
        osal_event_expect(osal_event_flags_set(g_event, invalid_single_bit) == OSAL_INVALID);
        osal_event_expect(osal_event_flags_clear(g_event, invalid_single_bit) == OSAL_INVALID);
        osal_event_expect(osal_event_flags_wait(g_event, invalid_single_bit, &out_value, 0u, 0u) == OSAL_INVALID);
    }

    /* 组7：from_isr 误用返回码（仅在禁用 assert 时验证） */
#ifndef __OM_USE_ASSERT
    osal_event_expect(osal_event_flags_set_from_isr(g_event, 0x08u) == OSAL_INVALID);
#endif

    osal_event_expect(osal_event_flags_delete(g_event) == OSAL_OK);
    g_event = NULL;

    /* 补充：局部对象生命周期 */
    osal_event_expect(osal_event_flags_create(&event_temp) == OSAL_OK);
    osal_event_expect(osal_event_flags_delete(event_temp) == OSAL_OK);

    g_event_result.done = 1u;
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
        "osal_event_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_event_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}

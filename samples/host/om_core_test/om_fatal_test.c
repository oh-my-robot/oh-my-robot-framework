/**
 * @file  om_fatal_test.c
 * @brief om_fatal_error 主机侧测试（handler 调用链 / reason+cause / ctx 传递 / NULL ctx）
 * @details
 * - 测试注入：include om_fatal.c——static g_fatal_entered 同 TU 可见，用例间可重置
 *   （一次 fatal 后 g_fatal_entered 永久置位，不重置则后续用例全部直接 halt）；
 * - handler 覆盖：include 前宏重命名 om_fatal_handler → om_fatal_handler_weak_impl
 *   （定义与调用点均改名——om_fatal_error 内部调用同一符号），测试在独立 TU
 *   （om_fatal_handler_override.c）提供 strong 实现，链接期强胜弱覆盖 weak 默认；
 * - 跳出 halt：override handler 内 longjmp 回主流程（入口的禁中断 halt 不执行）；
 * - 重入保护（fatal 中再 fatal → 直接 halt 不再调 handler）无法在单线程 host 上自动
 *   断言（halt 死循环不可中断），由代码结构保证（g_fatal_entered 前置检查）+ 审查。
 */
#define om_fatal_handler om_fatal_handler_weak_impl
#include "om_fatal.c"
#undef om_fatal_handler

#include <string.h>

/* OM_ASSERT 宏测试：需 OM_USE_ASSERT 开关（om_config.h 经 om_cpu.h/omlib.h 引入，
 * 测试直接定义以模拟框架代码含断言的场景） */
#ifndef OM_USE_ASSERT
#define OM_USE_ASSERT
#endif
#include "core/om_assert.h"

#include "om_fatal_test_shared.h"

/* ===== port 桩（host 无架构中断控制，om_interrupt.h 宏展开的目标符号） ===== */
port_critical_key_t port_critical_enter(void)
{
    return 0;
}
void port_critical_exit(port_critical_key_t key)
{
    (void)key;
}
void port_int_disable(void)
{
}
void port_int_enable(void)
{
}

#include "om_core_test.h"

/* ===== 用例 ===== */

static void test_handler_invoked(void)
{
    g_fatal_entered = 0;
    g_handler_calls = 0;

    if (setjmp(g_jmp) == 0) {
        om_fatal_error(OM_FATAL_ASSERT, OM_ERR_CONFLICT, NULL);
        EXPECT(0); /* 永不返回：不应到达 */
    } else {
        EXPECT(g_handler_calls == 1);
        EXPECT(g_reason == OM_FATAL_ASSERT);
        EXPECT(g_cause == OM_ERR_CONFLICT);
        EXPECT(g_ctx.file == NULL && g_ctx.line == 0 && g_ctx.pc == 0 && g_ctx.detail == NULL);
    }
}

static void test_ctx_full(void)
{
    g_fatal_entered = 0;
    g_handler_calls = 0;

    const OmFatalContext ctx = {.file = "test.c", .line = 42, .pc = 0x08001234, .detail = "driver_x"};
    if (setjmp(g_jmp) == 0) {
        om_fatal_error(OM_FATAL_STARTUP, OM_ERR_IO, &ctx);
        EXPECT(0);
    } else {
        EXPECT(g_handler_calls == 1);
        EXPECT(g_ctx.file != NULL && strcmp(g_ctx.file, "test.c") == 0);
        EXPECT(g_ctx.line == 42);
        EXPECT(g_ctx.pc == 0x08001234);
        EXPECT(g_ctx.detail != NULL && strcmp(g_ctx.detail, "driver_x") == 0);
    }
}

static void test_ctx_null(void)
{
    g_fatal_entered = 0;
    g_handler_calls = 0;

    if (setjmp(g_jmp) == 0) {
        om_fatal_error(OM_FATAL_STACK_OVERFLOW, OM_ERR_OVERFLOW, NULL);
        EXPECT(0);
    } else {
        EXPECT(g_handler_calls == 1);
        EXPECT(g_reason == OM_FATAL_STACK_OVERFLOW);
        EXPECT(g_cause == OM_ERR_OVERFLOW);
    }
}

static void test_called_exactly_once(void)
{
    g_fatal_entered = 0;
    g_handler_calls = 0;

    if (setjmp(g_jmp) == 0) {
        om_fatal_error(OM_FATAL_HW_FAULT, OM_ERR_IO, NULL);
        EXPECT(0);
    } else {
        EXPECT(g_handler_calls == 1); /* 正常路径 handler 恰好一次 */
    }
}

static void test_assert_no_trigger(void)
{
    g_fatal_entered = 0;
    g_handler_calls = 0;
    OM_ASSERT(1 == 1); /* 条件成立：不触发 */
    EXPECT(g_handler_calls == 0);
}

static void test_assert_triggers(void)
{
    g_fatal_entered = 0;
    g_handler_calls = 0;

    if (setjmp(g_jmp) == 0) {
        OM_ASSERT(1 == 2); /* 条件不成立：触发 fatal（携带 __FILE__:__LINE__） */
        EXPECT(0);         /* 永不返回 */
    } else {
        EXPECT(g_handler_calls == 1);
        EXPECT(g_reason == OM_FATAL_ASSERT);
        EXPECT(g_cause == OM_ERR_CONFLICT);
        EXPECT(g_ctx.file != NULL); /* __FILE__ 已传递 */
        EXPECT(g_ctx.line > 0);     /* __LINE__ 已传递 */
    }
}

int main(void)
{
    test_handler_invoked();
    test_ctx_full();
    test_ctx_null();
    test_called_exactly_once();
    test_assert_no_trigger();
    test_assert_triggers();
    TEST_DONE();
}

/**
 * @file  om_fatal_handler_override.c
 * @brief fatal 测试的强 handler 覆盖（跨 TU 覆盖 om_fatal.c 的 weak 默认实现）
 * @details om_fatal_test.c 通过宏重命名使 om_fatal_error 内部调用符号变为
 *          om_fatal_handler_weak_impl；本文件提供同名 strong 实现——链接期强胜弱，
 *          om_fatal_error 实际调到这里：记录 reason/cause/ctx 快照后 longjmp
 *          跳出入口的禁中断 halt（测试主流程继续断言）。
 */
#include "om_fatal_test_shared.h"

jmp_buf g_jmp;
int g_handler_calls    = 0;
OmFatalReason g_reason = OM_FATAL_STARTUP;
OmRet g_cause          = OM_OK;
OmFatalContext g_ctx   = {0};

void om_fatal_handler_weak_impl(OmFatalReason reason, OmRet cause, const OmFatalContext *ctx)
{
    g_handler_calls++;
    g_reason = reason;
    g_cause  = cause;
    g_ctx    = ctx ? *ctx : (OmFatalContext){0};
    longjmp(g_jmp, 1);
}

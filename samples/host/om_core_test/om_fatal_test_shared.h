/**
 * @file  om_fatal_test_shared.h
 * @brief fatal 测试的 handler 覆盖层共享声明（om_fatal_test.c 与 override 文件间传递）
 * @details 见 om_fatal_test.c 文件头说明：宏重命名使 om_fatal_error 内部调用
 *          om_fatal_handler_weak_impl 符号——测试在独立 TU 提供 strong 实现覆盖
 *          om_fatal.c 的 weak 默认，记录快照并 longjmp 跳出 halt。
 */
#ifndef __OM_FATAL_TEST_SHARED_H__
#define __OM_FATAL_TEST_SHARED_H__

#include <setjmp.h>
#include "core/om_fatal.h"

extern jmp_buf g_jmp;
extern int g_handler_calls;
extern OmFatalReason g_reason;
extern OmRet g_cause;
extern OmFatalContext g_ctx;

#endif /* __OM_FATAL_TEST_SHARED_H__ */

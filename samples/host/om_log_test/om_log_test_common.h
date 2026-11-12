/**
 * @file om_log_test_common.h
 * @brief om_log_test 公共断言（无宿主端单元测试框架的最小脚手架，om_core_test 同款）
 */

#ifndef __OM_LOG_TEST_COMMON_H__
#define __OM_LOG_TEST_COMMON_H__

#include <stdio.h>

/** @brief 断言宏：条件不成立时打印文件/行号/表达式并置全局失败标志（main 返回非 0） */
#define EXPECT(cond)                                               \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_log_test_failed = 1;                                 \
        }                                                          \
    } while (0)

/** @brief 全局失败标志（EXPECT 失败置 1；main 据此返回非 0 退出码） */
extern int g_log_test_failed;

#endif /* __OM_LOG_TEST_COMMON_H__ */

/**
 * @file  om_core_test.h
 * @brief kernel-core 主机侧测试公共头（无宿主端单元测试框架的最小脚手架）
 * @details 断言计数 + 打印；主流程返回 0=通过、非 0=失败（xmake run 以退出码判结果）。
 */
#ifndef __OM_CORE_TEST_H__
#define __OM_CORE_TEST_H__

#include <stdio.h>

extern int g_test_total;
extern int g_test_failed;

#define EXPECT(cond)                                                 \
    do {                                                             \
        g_test_total++;                                              \
        if (!(cond)) {                                               \
            g_test_failed++;                                         \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                            \
    } while (0)

/** @brief 测试收尾：打印统计并返回退出码（main 使用） */
#define TEST_DONE()                                                           \
    do {                                                                      \
        printf("%d/%d passed\n", g_test_total - g_test_failed, g_test_total); \
        return (g_test_failed == 0) ? 0 : 1;                                  \
    } while (0)

#endif /* __OM_CORE_TEST_H__ */

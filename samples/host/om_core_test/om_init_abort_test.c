/**
 * @file  om_init_abort_test.c
 * @brief OM_INIT_ABORT_ON_FAIL 语义测试（编译期宏：首个失败立即返回，后续不执行）
 * @details 与 om_init_test.c 同一注入模式（include om_init.c 改写 s_level_ranges）；
 *          本文件由独立目标以 -DOM_INIT_ABORT_ON_FAIL 编译（宏在 om_init.c 内生效）。
 */
#define const /* 测试注入：允许改写 s_level_ranges */
#include "om_init.c"
#undef const

#include <string.h>

#include "om_core_test.h"

/* ===== 链接期符号占位（同 om_init_test.c 说明） ===== */
OmInitEntry __om_init_0_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_0_end[]   = {{0, 0, 0, 0}};
OmInitEntry __om_init_1_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_1_end[]   = {{0, 0, 0, 0}};
OmInitEntry __om_init_2_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_2_end[]   = {{0, 0, 0, 0}};
OmInitEntry __om_init_3_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_3_end[]   = {{0, 0, 0, 0}};
OmInitEntry __om_init_4_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_4_end[]   = {{0, 0, 0, 0}};
OmInitEntry __om_init_5_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_5_end[]   = {{0, 0, 0, 0}};
OmInitEntry __om_init_6_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_6_end[]   = {{0, 0, 0, 0}};

static OmInitEntry g_buf[16];
static int g_used = 0;

/** @brief 清空全部级别区间（用例开头调用，防止上一用例残留区间被遍历） */
static void reset_ranges(void)
{
    for (int i = 0; i < OM_INIT_LEVEL_COUNT; i++) {
        s_level_ranges[i].start = &g_buf[0];
        s_level_ranges[i].end   = &g_buf[0];
    }
    g_used = 0;
}

static int set_range(int level, int count)
{
    int base                    = g_used;
    s_level_ranges[level].start = &g_buf[base];
    s_level_ranges[level].end   = &g_buf[base + count];
    g_used += count;
    return base;
}

static char g_order[16];
static int g_order_len;

static OmRet cb_a(void)
{
    g_order[g_order_len++] = 'a';
    return OM_OK;
}
static OmRet cb_fail(void)
{
    g_order[g_order_len++] = 'F';
    return OM_ERR_NO_MEM;
}
static OmRet cb_b(void)
{
    g_order[g_order_len++] = 'b';
    return OM_OK;
}

static void test_abort_on_fail(void)
{
    reset_ranges();
    int base        = set_range(0, 3);
    g_buf[base]     = (OmInitEntry){cb_a, "a", 0, 10};
    g_buf[base + 1] = (OmInitEntry){cb_fail, "fail", 0, 50};
    g_buf[base + 2] = (OmInitEntry){cb_b, "b", 0, 90};

    g_order_len = 0;
    /* ABORT 模式：首个失败立即返回，后续回调不执行 */
    EXPECT(om_do_initcalls(OM_INIT_LEVEL_EARLIEST, OM_INIT_LEVEL_COUNT) == OM_ERR_NO_MEM);
    EXPECT(g_order_len == 2 && g_order[0] == 'a' && g_order[1] == 'F');
    EXPECT(om_init_last_fail_name() != NULL);
    if (om_init_last_fail_name() != NULL) {
        EXPECT(strcmp(om_init_last_fail_name(), "fail") == 0);
    }
}

int main(void)
{
    test_abort_on_fail();
    TEST_DONE();
}

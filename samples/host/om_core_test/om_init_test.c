/**
 * @file  om_init_test.c
 * @brief om_do_initcalls 主机侧测试（级别顺序/区间/prio/空槽/失败记录并继续）
 * @details
 * - 测试注入：`#define const` + include om_init.c——把 static const s_level_ranges
 *   变为可改写，测试直接注入每级区间（指向连续缓冲 g_buf），模拟链接器段布局；
 *   链接期符号 __om_init_N_start/end 以空数组占位（s_level_ranges 初始值被改写覆盖）。
 * - 用例间通过 set_range() 重新注入布局，互不影响。
 */
#define const /* 测试注入：允许改写 s_level_ranges（见文件头说明） */
#include "om_init.c"
#undef const

#include <string.h>

#include "om_core_test.h"

/* ===== 链接期符号占位：s_level_ranges 初始值引用全部 7 级 extern 符号 =====
 * （内容无意义——测试用 set_range() 改写各级区间；须为全局定义以匹配
 *   #define const 宏化后的 extern 声明） */
OmInitEntry __om_init_0_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_0_end[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_1_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_1_end[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_2_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_2_end[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_3_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_3_end[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_4_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_4_end[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_5_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_5_end[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_6_start[] = {{0, 0, 0, 0}};
OmInitEntry __om_init_6_end[] = {{0, 0, 0, 0}};

/* ===== 连续 entry 缓冲（模拟链接器把各级段连续放置；容量覆盖溢出用例 129 项） ===== */
static OmInitEntry g_buf[160];
static int g_used = 0;

/** @brief 清空全部级别区间（用例开头调用，防止上一用例残留区间被遍历） */
static void reset_ranges(void)
{
    for (int i = 0; i < OM_INIT_LEVEL_COUNT; i++)
    {
        s_level_ranges[i].start = &g_buf[0];
        s_level_ranges[i].end = &g_buf[0];
    }
    g_used = 0;
}

static int set_range(int level, int count)
{
    int base = g_used;
    s_level_ranges[level].start = &g_buf[base];
    s_level_ranges[level].end = &g_buf[base + count];
    g_used += count;
    return base;
}

/* ===== 测试回调 ===== */
static char g_order[64];
static int g_order_len;

static void reset_order(void)
{
    g_order_len = 0;
}

static OmRet cb_a(void)
{
    g_order[g_order_len++] = 'a';
    return OM_OK;
}
static OmRet cb_b(void)
{
    g_order[g_order_len++] = 'b';
    return OM_OK;
}
static OmRet cb_c(void)
{
    g_order[g_order_len++] = 'c';
    return OM_OK;
}
static OmRet cb_fail(void)
{
    g_order[g_order_len++] = 'F';
    return OM_ERR_BUSY;
}
static OmRet cb_d(void)
{
    g_order[g_order_len++] = 'd';
    return OM_OK;
}
static int g_count;
static OmRet cb_count(void)
{
    g_count++;
    return OM_OK;
}

/* ===== 用例 ===== */

static void test_level_order(void)
{
    reset_ranges();
    int b0 = set_range(0, 1); /* level 0: a */
    int b1 = set_range(1, 1); /* level 1: b */
    int b2 = set_range(2, 1); /* level 2: c */
    g_buf[b0] = (OmInitEntry){cb_a, "a", 0, 50};
    g_buf[b1] = (OmInitEntry){cb_b, "b", 1, 50};
    g_buf[b2] = (OmInitEntry){cb_c, "c", 2, 50};

    reset_order();
    EXPECT(om_do_initcalls(OM_INIT_LEVEL_EARLIEST, OM_INIT_LEVEL_COUNT) == OM_OK);
    EXPECT(g_order_len == 3 && g_order[0] == 'a' && g_order[1] == 'b' && g_order[2] == 'c');
}

static void test_range_filter(void)
{
    reset_ranges();
    int b0 = set_range(0, 1);
    int b1 = set_range(1, 1);
    int b2 = set_range(2, 1);
    g_buf[b0] = (OmInitEntry){cb_a, "a", 0, 50};
    g_buf[b1] = (OmInitEntry){cb_b, "b", 1, 50};
    g_buf[b2] = (OmInitEntry){cb_c, "c", 2, 50};

    reset_order();
    /* 只跑 level 1..2（2 不含） */
    EXPECT(om_do_initcalls(OM_INIT_LEVEL_BOARD, OM_INIT_LEVEL_DRIVER) == OM_OK);
    EXPECT(g_order_len == 1 && g_order[0] == 'b');
}

static void test_prio_order(void)
{
    reset_ranges();
    int base = set_range(0, 3);
    g_buf[base] = (OmInitEntry){cb_a, "a", 0, 90};
    g_buf[base + 1] = (OmInitEntry){cb_b, "b", 0, 10};
    g_buf[base + 2] = (OmInitEntry){cb_c, "c", 0, 50};

    reset_order();
    EXPECT(om_do_initcalls(OM_INIT_LEVEL_EARLIEST, OM_INIT_LEVEL_COUNT) == OM_OK);
    EXPECT(g_order_len == 3 && g_order[0] == 'b' && g_order[1] == 'c' && g_order[2] == 'a');
}

static void test_null_slot_skipped(void)
{
    reset_ranges();
    int base = set_range(0, 2);
    g_buf[base] = (OmInitEntry){NULL, "null", 0, 0};
    g_buf[base + 1] = (OmInitEntry){cb_a, "a", 0, 50};

    reset_order();
    EXPECT(om_do_initcalls(OM_INIT_LEVEL_EARLIEST, OM_INIT_LEVEL_COUNT) == OM_OK);
    EXPECT(g_order_len == 1 && g_order[0] == 'a');
}

static void test_empty_level(void)
{
    reset_ranges();
    set_range(0, 0); /* 空级 */
    int base = set_range(1, 1);
    g_buf[base] = (OmInitEntry){cb_a, "a", 1, 50};

    reset_order();
    EXPECT(om_do_initcalls(OM_INIT_LEVEL_EARLIEST, OM_INIT_LEVEL_COUNT) == OM_OK);
    EXPECT(g_order_len == 1 && g_order[0] == 'a');
}

static void test_fail_record_and_continue(void)
{
    reset_ranges();
    int base = set_range(0, 3);
    g_buf[base] = (OmInitEntry){cb_a, "a", 0, 10};
    g_buf[base + 1] = (OmInitEntry){cb_fail, "fail", 0, 50};
    g_buf[base + 2] = (OmInitEntry){cb_b, "b", 0, 90};

    reset_order();
    /* 失败记录并继续：后续仍执行，返回首个失败码，名称可查 */
    EXPECT(om_do_initcalls(OM_INIT_LEVEL_EARLIEST, OM_INIT_LEVEL_COUNT) == OM_ERR_BUSY);
    EXPECT(g_order_len == 3 && g_order[1] == 'F');
    EXPECT(om_init_last_fail_name() != NULL);
    if (om_init_last_fail_name() != NULL)
    {
        EXPECT(strcmp(om_init_last_fail_name(), "fail") == 0);
    }
}

static void test_max_entries_overflow(void)
{
    /* 同级表项 > OM_INIT_MAX_ENTRIES(128)：退化为链接顺序全执行（不排序） */
    reset_ranges();
    int base = set_range(0, OM_INIT_MAX_ENTRIES + 1);
    for (int i = 0; i < OM_INIT_MAX_ENTRIES + 1; i++)
    {
        g_buf[base + i] = (OmInitEntry){cb_count, "count", 0, 50};
    }
    g_count = 0;
    EXPECT(om_do_initcalls(OM_INIT_LEVEL_EARLIEST, OM_INIT_LEVEL_COUNT) == OM_OK);
    EXPECT(g_count == OM_INIT_MAX_ENTRIES + 1); /* 129 个全部执行 */
}

static void test_level_arg_boundaries(void)
{
    reset_ranges();
    int base = set_range(0, 1);
    g_buf[base] = (OmInitEntry){cb_a, "a", 0, 50};

    g_order_len = 0;
    /* lo > hi：空区间，不执行任何回调 */
    EXPECT(om_do_initcalls(OM_INIT_LEVEL_SYSTEM, OM_INIT_LEVEL_BOARD) == OM_OK);
    EXPECT(g_order_len == 0);
    /* 空区间 (0, 0) */
    EXPECT(om_do_initcalls(0, 0) == OM_OK);
    EXPECT(g_order_len == 0);
    /* 全区间恢复正常 */
    EXPECT(om_do_initcalls(OM_INIT_LEVEL_EARLIEST, OM_INIT_LEVEL_COUNT) == OM_OK);
    EXPECT(g_order_len == 1 && g_order[0] == 'a');
}

int main(void)
{
    test_level_order();
    test_range_filter();
    test_prio_order();
    test_null_slot_skipped();
    test_empty_level();
    test_fail_record_and_continue();
    test_max_entries_overflow();
    test_level_arg_boundaries();
    TEST_DONE();
}

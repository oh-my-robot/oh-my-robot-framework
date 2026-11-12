/**
 * @file om_log_formatter_test.c
 * @brief 流式格式化器测试：格式符/宽度标志/l 修饰/段切分/任意长/未知降级
 */

#include "log_internal.h"

#include "om_log_test_common.h"

#include <stdarg.h>
#include <string.h>

static char g_buf[1024];     /* 捕获拼接缓冲（整条比对用） */
static size_t g_len;         /* 已捕获字节数 */
static unsigned g_seg_count; /* 段回调次数（段切分验证用） */

/** @brief 段捕获回调：拼接进 g_buf 并计数（模拟 log 服务的扇出目标）
 *  @param ctx 未使用（LogOutFn 签名要求）
 *  @param seg 段数据
 *  @param len 段字节数 */
static void capture(void *ctx, const char *seg, size_t len)
{
    (void)ctx;
    (void)memcpy(g_buf + g_len, seg, len);
    g_len += len;
    g_seg_count++;
}

/** @brief 格式化并捕获：每次清零缓冲后执行一次 log_format，供断言比对
 *  @param fmt 格式串（printf 风格子集）
 *  @note 必须经 varargs 入口包装——log_format 形参是 va_list（x86_64 下为数组
 *        类型），直接传普通实参是 UB（T3 实测段错误后修复） */
static void format_check(const char *fmt, ...)
{
    LogBufWriter w;
    char seg[32];
    va_list ap;
    g_len = 0;
    g_seg_count = 0;
    log_buf_writer_init(&w, capture, NULL, seg, sizeof(seg));
    va_start(ap, fmt);
    log_format(&w, fmt, ap);
    va_end(ap);
    log_buf_flush(&w);
    g_buf[g_len] = '\0';
}

/** @brief 参数数组版格式化并捕获（log_format_args 入口，供参数包语义断言）
 *  @param fmt 格式串（printf 风格子集）
 *  @param args 参数数组（uintptr_t 宽，LOG_ARG 游标按序取）
 *  @param n 参数个数上限（越界返回 0 防御） */
static void format_check_args(const char *fmt, const uintptr_t *args, size_t n)
{
    LogBufWriter w;
    char seg[32];
    g_len = 0;
    g_seg_count = 0;
    log_buf_writer_init(&w, capture, NULL, seg, sizeof(seg));
    log_format_args(&w, fmt, args, n);
    log_buf_flush(&w);
    g_buf[g_len] = '\0';
}

/* ---- log_msg_build（T3） ---- */

/** @brief 临时模块探针（filter 测试的 testmod 经 OM_LOG_MODULE 宏展开——此处用独立
 *         静态实例，避免同名重复定义；type 与 _om_log_module 同构） */
static const OmLogModule fake_mod = {"probe", OM_LOG_LEVEL_INFO};

/** @brief build 快照（build_check 输出，供 main 断言语：argBuf/argCount/fmt/module/level）
 *  @note format_check 同款约束：log_msg_build 形参为 va_list（x86_64 数组类型），
 *        必须经 varargs 包装入口调用，不能直接传参 */
static OmLogMsg g_built_msg;

/** @brief build → format_args 链路检查：打包成功则格式化捕获（等价性屏障）
 *  @param fmt 格式串
 *  @return 打包结果（false = 超限丢弃，不取参；g_buf 不在断言范围内） */
static bool build_check(const char *fmt, ...)
{
    OmLogMsg m;
    va_list ap;
    va_start(ap, fmt);
    bool ok = log_msg_build(&m, &fake_mod, OM_LOG_LEVEL_INFO, 0U, fmt, ap); /* ts 桩 0 */

    va_end(ap);
    if (ok)
    {
        g_built_msg = m;
        format_check_args(m.fmt, m.argBuf, m.argCount);
    }
    return ok;
}

int main(void)
{
    format_check("%d", -42);
    EXPECT(strcmp(g_buf, "-42") == 0);
    format_check("x=%d", 7);
    EXPECT(strcmp(g_buf, "x=7") == 0);
    format_check("%u", 4294967295U);
    EXPECT(strcmp(g_buf, "4294967295") == 0);
    format_check("%x", 0x1A2B);
    EXPECT(strcmp(g_buf, "1a2b") == 0);
    format_check("%X", 0x1A2B);
    EXPECT(strcmp(g_buf, "1A2B") == 0);
    format_check("%lx", 0x1A2B3C4DUL);
    EXPECT(strcmp(g_buf, "1a2b3c4d") == 0);
    format_check("%p", (void *)0x1234);
    EXPECT(strcmp(g_buf, "1234") == 0);
    format_check("%c%c", 'A', 'B');
    EXPECT(strcmp(g_buf, "AB") == 0);
    format_check("%s", "hello");
    EXPECT(strcmp(g_buf, "hello") == 0);
    format_check("%s", (const char *)NULL);
    EXPECT(strcmp(g_buf, "(null)") == 0);
    format_check("%5d", -1);
    EXPECT(strcmp(g_buf, "   -1") == 0);
    format_check("%05d", -1);
    EXPECT(strcmp(g_buf, "-0001") == 0);
    format_check("%-5d", -1);
    EXPECT(strcmp(g_buf, "-1   ") == 0);
    format_check("%10s", "abc");
    EXPECT(strcmp(g_buf, "       abc") == 0);
    format_check("%-10s", "abc");
    EXPECT(strcmp(g_buf, "abc       ") == 0);
    format_check("100%%");
    EXPECT(strcmp(g_buf, "100%") == 0);
    format_check("%d", 0);
    EXPECT(strcmp(g_buf, "0") == 0);
    format_check(""); /* 空格式串 */
    EXPECT(g_len == 0);
    format_check("%.2f", 1.5); /* 未知转换符：整段规格字面输出 */
    EXPECT(strcmp(g_buf, "%.2f") == 0);
    format_check("%5f", 1.5); /* 宽度被解析仍整段字面输出（不丢宽度前缀） */
    EXPECT(strcmp(g_buf, "%5f") == 0);
    format_check("%", 1); /* 尾部不完整规格：整体字面输出 */
    EXPECT(strcmp(g_buf, "%") == 0);

    /* 段边界：>32B 消息多段回调，拼接后与原文一致 */
    {
        char long_msg[300];
        size_t i;
        for (i = 0; i < sizeof(long_msg) - 1; i++)
            long_msg[i] = (char)('a' + (i % 26));
        long_msg[sizeof(long_msg) - 1] = '\0';
        /* log_format 形参为 va_list（x86_64 下为数组类型），必须经 varargs 包装调用 */
        format_check("%s", long_msg);
        EXPECT(strcmp(g_buf, long_msg) == 0);
        EXPECT(g_seg_count > 1);
    }

    /* log_format_args（参数数组版）：与 va_list 版语义等价 */
    {
        uintptr_t a1[4] = {(uintptr_t)-42, (uintptr_t)7, (uintptr_t)0x1A2B, (uintptr_t)(const char *)"hi"};
        format_check_args("%d %u %x %s", a1, 4);
        EXPECT(strcmp(g_buf, "-42 7 1a2b hi") == 0);
    }
    {
        uintptr_t a2[3] = {(uintptr_t)(long)-5, (uintptr_t)0xCAFE, (uintptr_t)(void *)0x1234};
        format_check_args("%ld %lx %p", a2, 3);
        EXPECT(strcmp(g_buf, "-5 cafe 1234") == 0);
    }
    {
        uintptr_t a3[2] = {(uintptr_t)'A', (uintptr_t)(const char *)NULL};
        format_check_args("%c %s", a3, 2);
        EXPECT(strcmp(g_buf, "A (null)") == 0);
    }
    {
        uintptr_t a4[3] = {(uintptr_t)1, (uintptr_t)2, (uintptr_t)3};
        format_check_args("%5d %-5d %05d", a4, 3); /* 宽度/零填充/左对齐走同路径 */
        EXPECT(strcmp(g_buf, "    1 2     00003") == 0);
    }
    /* 越界防御：n=1 但 fmt 用 2 参 → 第二参返回 0 */
    {
        uintptr_t a5[1] = {(uintptr_t)7};
        format_check_args("%d %d", a5, 1);
        EXPECT(strcmp(g_buf, "7 0") == 0);
    }
    /* 等价性：args 版 vs va 版（同参数同输出——防两版解析漂移） */
    {
        uintptr_t a[4] = {(uintptr_t)-42, (uintptr_t)7, (uintptr_t)0x1A2B, (uintptr_t)(const char *)"hi"};
        char buf_args[64], buf_va[64];
        format_check_args("%d %u %x %s", a, 4);
        memcpy(buf_args, g_buf, g_len + 1);
        format_check("%d %u %x %s", -42, 7, 0x1A2B, "hi");
        memcpy(buf_va, g_buf, g_len + 1);
        EXPECT(strcmp(buf_args, buf_va) == 0);
    }

    /* log_msg_build：抓取与 argCount（build → format_args 链路 == va 版语义，等价性屏障） */
    EXPECT(build_check("%d %s %x", 42, "w", 0xAB) == true);
    EXPECT(strcmp(g_built_msg.fmt, "%d %s %x") == 0);
    EXPECT(g_built_msg.module == &fake_mod && g_built_msg.level == OM_LOG_LEVEL_INFO);
    EXPECT(g_built_msg.argCount == 3);
    EXPECT(g_built_msg.argBuf[0] == 42 && g_built_msg.argBuf[2] == 0xAB);
    EXPECT(strcmp(g_buf, "42 w ab") == 0);
    /* %u/%lx/%p：无 l 无符号零扩展、l 宽类型、指针直存 */
    EXPECT(build_check("%u %lx %p", 4294967295U, 0x1A2B3C4DUL, (void *)0x1234) == true);
    EXPECT(g_built_msg.argCount == 3);
    EXPECT(g_built_msg.argBuf[0] == (uintptr_t)4294967295U);
    EXPECT(g_built_msg.argBuf[1] == (uintptr_t)0x1A2B3C4DUL);
    EXPECT(g_built_msg.argBuf[2] == (uintptr_t)(void *)0x1234);
    EXPECT(strcmp(g_buf, "4294967295 1a2b3c4d 1234") == 0);
    /* 超限：OM_LOG_MAX_ARGS+1 参 → false 丢弃（不取参） */
    EXPECT(build_check("%d %d %d %d %d %d %d %d %d", 1, 2, 3, 4, 5, 6, 7, 8, 9) == false);
    /* %c 提升 + %ld 宽类型；%% 不取参 */
    EXPECT(build_check("%c %ld", 'Z', -7L) == true);
    EXPECT(g_built_msg.argCount == 2);
    EXPECT(g_built_msg.argBuf[0] == (uintptr_t)'Z' && g_built_msg.argBuf[1] == (uintptr_t)-7L);
    EXPECT(strcmp(g_buf, "Z -7") == 0);
    EXPECT(build_check("%d%%", 7) == true);
    EXPECT(g_built_msg.argCount == 1);
    EXPECT(g_built_msg.argBuf[0] == 7);
    EXPECT(strcmp(g_buf, "7%") == 0);
    /* 尾部不完整规格（"%" 结尾）：log_spec_next 返回 '\0' 不前进，余下字面输出 */
    EXPECT(build_check("%d %", 7) == true);
    EXPECT(g_built_msg.argCount == 1);
    EXPECT(strcmp(g_buf, "7 %") == 0);

    /* log_time_format：HH:MM:SS.mmm 换算（T5） */
    {
        char ts[13];
        size_t n = log_time_format(ts, (12U * 3600U + 34U * 60U + 56U) * 1000U + 789U);
        EXPECT(n == 12 && strcmp(ts, "12:34:56.789") == 0);
        log_time_format(ts, 0);
        EXPECT(strcmp(ts, "00:00:00.000") == 0);
        log_time_format(ts, (23U * 3600U + 59U * 60U + 59U) * 1000U + 999U);
        EXPECT(strcmp(ts, "23:59:59.999") == 0);
        log_time_format(ts, 1U); /* 毫秒补零 */
        EXPECT(strcmp(ts, "00:00:00.001") == 0);
        log_time_format(ts, 120U * 3600000U + 4000U + 567U); /* >99h 截断防御：%100 回绕显示 */
        EXPECT(strcmp(ts, "20:00:04.567") == 0);
    }

    if (g_log_test_failed)
    {
        printf("om_log_formatter_test: FAIL\n");
        return 1;
    }
    printf("om_log_formatter_test: ALL PASS\n");
    return 0;
}

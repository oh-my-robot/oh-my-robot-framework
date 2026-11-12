/**
 * @file formatter.c
 * @brief log 流式格式化器（printf 风格子集，逐段回调输出）
 * @details 支持：标志 -/0、宽度、长度修饰 l；转换符 d i u x X p c s %。
 *          未知/不完整转换符降级为整段规格字面输出（"%5f" 原样打印）；任意长
 *          消息分段回调，栈占用 = 段缓冲 + 常量状态，不随消息长度增长。
 *          LONG_MIN 的取负溢出未处理（嵌入式整型格式化惯例，文档约束）。
 *          另含时间戳换算 log_time_format（纯字符填数，无 OS 依赖——host 可测）。
 */

#include "core/om_config.h"

#if OM_USE_LOG

#include "core/om_def.h"

#include "log_internal.h"

#include <stdarg.h>
#include <string.h>

/* 参数游标（args 数组索引取；与 va_list 版本同序/同类型——取值顺序必须与 log_msg_build 抓取一致） */
#define LOG_ARG(args, ai) ((ai) < (n) ? (args)[(ai)++] : 0)

void log_buf_writer_init(LogBufWriter *w, LogOutFn out, void *out_ctx, char *seg, size_t seg_size)
{
    w->out = out;
    w->outCtx = out_ctx;
    w->seg = seg;
    w->segSize = seg_size;
    w->segLen = 0;
}

/** @brief 刷出当前段：段非空时回调 out 一次，随后复位段长
 *  @param w 写器 */
static void fmt_flush(LogBufWriter *w)
{
    if (w->segLen > 0)
    {
        w->out(w->outCtx, w->seg, w->segLen);
        w->segLen = 0;
    }
}

void log_buf_flush(LogBufWriter *w)
{
    fmt_flush(w);
}

void log_buf_write(LogBufWriter *w, const char *s, size_t n)
{
    while (n > 0)
    {
        size_t room = w->segSize - w->segLen;
        size_t m = (n < room) ? n : room;
        (void)memcpy(w->seg + w->segLen, s, m);
        w->segLen += m;
        s += m;
        n -= m;
        if (w->segLen >= w->segSize)
        {
            fmt_flush(w);
        }
    }
}

void log_buf_putc(LogBufWriter *w, char c)
{
    log_buf_write(w, &c, 1);
}

/** @brief 填充 n 个 pad 字符（宽度对齐用）
 *  @param w 写器
 *  @param pad 填充字符（空格或 '0'）
 *  @param n 填充数量 */
static void fmt_pad(LogBufWriter *w, char pad, size_t n)
{
    while (n > 0)
    {
        log_buf_putc(w, pad);
        n--;
    }
}

/** @brief 无符号整数转字符串（先逆序填充再反转，避免移位运算）
 *  @param base 进制（10/16）
 *  @param upper 十六进制大写输出
 *  @return 数字长度（不含符号） */
static size_t fmt_utoa(unsigned long v, char *tmp, int base, int upper)
{
    static const char digits[] = "0123456789abcdef";
    static const char digits_up[] = "0123456789ABCDEF";
    const char *table = upper ? digits_up : digits;
    size_t len = 0;
    if (v == 0)
    {
        tmp[len++] = '0';
    }
    else
    {
        while (v > 0)
        {
            tmp[len++] = table[v % (unsigned)base];
            v /= (unsigned)base;
        }
    }
    for (size_t i = 0; i < len / 2; i++)
    {
        char t = tmp[i];
        tmp[i] = tmp[len - 1 - i];
        tmp[len - 1 - i] = t;
    }
    return len;
}

/** @brief 数值转换输出：宽度/零填充/左对齐/符号 统一处理
 *  @param w 写器
 *  @param v 无符号数值
 *  @param sign 符号字符（0 = 无符号，'-' = 负号）
 *  @param base 进制（10/16）
 *  @param upper 十六进制大写输出
 *  @param width 最小宽度（0 = 不填充）
 *  @param pad 填充字符（' ' 或 '0'）
 *  @param left 左对齐（'-' 标志）
 *  @note 零填充时符号先出再补零（"%05d" → "-0001"，与 libc printf 一致） */
static void fmt_num(LogBufWriter *w, unsigned long v, int sign, int base, int upper, int width, char pad, int left)
{
    char tmp[32];
    size_t len = fmt_utoa(v, tmp, base, upper);
    size_t total = len + (sign != 0 ? 1 : 0);
    size_t pad_count = (size_t)(width > (int)total ? width - (int)total : 0);
    if (sign != 0 && pad == '0')
    {
        /* 零填充快路径：符号先出再补零，数字紧随（"    " 与 "-0001" 的差异就在此） */
        log_buf_putc(w, (char)sign);
        fmt_pad(w, '0', pad_count);
        log_buf_write(w, tmp, len);
        return;
    }
    if (!left)
    {
        fmt_pad(w, pad, pad_count);
    }
    if (sign != 0)
    {
        log_buf_putc(w, (char)sign);
    }
    log_buf_write(w, tmp, len);
    if (left)
    {
        fmt_pad(w, ' ', pad_count);
    }
}

/** @brief 规格解析（格式化语法唯一事实源）——从 '%' 起解析：- / 0 / 宽度 / l（聚合输出）
 *  @param fmtp inout：指向 '%'；成功时前进到转换符之后；不完整时前进到 '\0'
 *  @return 聚合规格（conv='\0' = 尾部不完整，余下全为字面） */
LogSpec log_spec_next(const char **fmtp)
{
    LogSpec spec = {'\0', 0, 0, ' ', 0};
    const char *p = *fmtp;
    p++; /* 越过 '%' */
    for (;;)
    {
        char c = *p;
        if (c == '-')
        {
            spec.left = 1;
            spec.pad = ' ';
        }
        else if (c == '0' && spec.width == 0 && !spec.left)
        {
            spec.pad = '0';
        }
        else if (c >= '0' && c <= '9')
        {
            spec.width = spec.width * 10 + (c - '0');
        }
        else if (c == 'l')
        {
            spec.is_long = 1;
        }
        else
        {
            break;
        }
        p++;
    }
    spec.conv = *p;
    *fmtp = (spec.conv == '\0') ? p : p + 1; /* 不完整：前进到 '\0'；成功：转换符之后 */
    return spec;
}

void log_format(LogBufWriter *w, const char *fmt, va_list ap)
{
    while (*fmt != '\0')
    {
        char c = *fmt++;
        if (c != '%')
        {
            log_buf_putc(w, c);
            continue;
        }
        /* 解析规格：- / 0 / 宽度 / l（唯一事实源 log_spec_next）；spec_start 供字面输出 */
        const char *spec_start = fmt - 1; /* '%' 位置 */
        int width = 0;
        char pad = ' ';
        int left = 0;
        int is_long = 0;
        {
            const char *spec_p = spec_start; /* log_spec_next 约定输入指向 '%' */
            LogSpec spec = log_spec_next(&spec_p);
            if (spec.conv == '\0')
            {
                /* 尾部不完整规格：整体字面输出（spec_p 已前进到 '\0'——覆盖已扫字符） */
                log_buf_write(w, spec_start, (size_t)(spec_p - spec_start));
                break;
            }
            fmt = spec_p; /* 转换符之后（log_spec_next 成功时已前进） */
            c = spec.conv;
            is_long = spec.is_long;
            width = spec.width;
            pad = spec.pad;
            left = spec.left;
        }
        switch (c)
        {
        case '%':
            log_buf_putc(w, '%');
            break;
        case 'd':
        case 'i': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            int sign = 0;
            unsigned long u;
            if (v < 0)
            {
                sign = '-';
                u = (unsigned long)(-v);
            }
            else
            {
                u = (unsigned long)v;
            }
            fmt_num(w, u, sign, 10, 0, width, pad, left);
            break;
        }
        case 'u': {
            unsigned long u = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            fmt_num(w, u, 0, 10, 0, width, pad, left);
            break;
        }
        case 'x':
        case 'X': {
            unsigned long u = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            fmt_num(w, u, 0, 16, (c == 'X'), width, pad, left);
            break;
        }
        case 'p': {
            void *p = va_arg(ap, void *);
            fmt_num(w, (unsigned long)(uintptr_t)p, 0, 16, 0, width, pad, left);
            break;
        }
        case 'c':
            log_buf_putc(w, (char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == NULL)
            {
                s = "(null)";
            }
            size_t len = strlen(s);
            if (!left)
            {
                fmt_pad(w, ' ', (size_t)(width > (int)len ? width - (int)len : 0));
            }
            log_buf_write(w, s, len);
            if (left)
            {
                fmt_pad(w, ' ', (size_t)(width > (int)len ? width - (int)len : 0));
            }
            break;
        }
        default:
            /* 未知转换符：整段规格字面输出（含已解析的宽度/标志） */
            log_buf_write(w, spec_start, (size_t)(fmt - spec_start));
            break;
        }
    }
    fmt_flush(w);
}

/** @brief 单调 ms → HH:MM:SS.mmm（"HH:MM:SS.mmm" 12 字符 + NUL；手写填数不依赖 printf）
 *  @param buf 输出缓冲（>=13B）
 *  @param ms 单调毫秒
 *  @return 12（恒满写） */
size_t log_time_format(char *buf, uint32_t ms)
{
    uint32_t h = ms / 3600000U;
    uint32_t m = (ms / 60000U) % 60U;
    uint32_t s = (ms / 1000U) % 60U;
    uint32_t milli = ms % 1000U;
    /* 两位字段补零（>=10 用 2 位，<10 前导 0）——直接字符填 */
    h %= 100U; /* 截断防御：uptime 超 99h 回绕显示（毫秒时基自然回绕，仅显示语义） */
    buf[0] = (char)('0' + (h / 10));
    buf[1] = (char)('0' + (h % 10));
    buf[2] = ':';
    buf[3] = (char)('0' + (m / 10));
    buf[4] = (char)('0' + (m % 10));
    buf[5] = ':';
    buf[6] = (char)('0' + (s / 10));
    buf[7] = (char)('0' + (s % 10));
    buf[8] = '.';
    buf[9] = (char)('0' + (milli / 100));
    buf[10] = (char)('0' + ((milli / 10) % 10));
    buf[11] = (char)('0' + (milli % 10));
    buf[12] = '\0';
    return 12;
}

void log_format_args(LogBufWriter *w, const char *fmt, const uintptr_t *args, size_t n)
{
    size_t ai = 0;
    while (*fmt != '\0')
    {
        char c = *fmt++;
        if (c != '%')
        {
            log_buf_putc(w, c);
            continue;
        }
        /* 解析规格：- / 0 / 宽度 / l（唯一事实源 log_spec_next）；spec_start 供字面输出 */
        const char *spec_start = fmt - 1; /* '%' 位置 */
        int width = 0;
        char pad = ' ';
        int left = 0;
        int is_long = 0;
        {
            const char *spec_p = spec_start; /* log_spec_next 约定输入指向 '%' */
            LogSpec spec = log_spec_next(&spec_p);
            if (spec.conv == '\0')
            {
                /* 尾部不完整规格：整体字面输出（spec_p 已前进到 '\0'——覆盖已扫字符） */
                log_buf_write(w, spec_start, (size_t)(spec_p - spec_start));
                break;
            }
            fmt = spec_p; /* 转换符之后（log_spec_next 成功时已前进） */
            c = spec.conv;
            is_long = spec.is_long;
            width = spec.width;
            pad = spec.pad;
            left = spec.left;
        }
        switch (c)
        {
        case '%':
            log_buf_putc(w, '%');
            break;
        case 'd':
        case 'i': {
            long v = is_long ? (long)LOG_ARG(args, ai) : (long)(int)LOG_ARG(args, ai);
            int sign = 0;
            unsigned long u;
            if (v < 0)
            {
                sign = '-';
                u = (unsigned long)(-v);
            }
            else
            {
                u = (unsigned long)v;
            }
            fmt_num(w, u, sign, 10, 0, width, pad, left);
            break;
        }
        case 'u': {
            unsigned long u = is_long ? (unsigned long)LOG_ARG(args, ai) : (unsigned long)(unsigned int)LOG_ARG(args, ai);
            fmt_num(w, u, 0, 10, 0, width, pad, left);
            break;
        }
        case 'x':
        case 'X': {
            unsigned long u = is_long ? (unsigned long)LOG_ARG(args, ai) : (unsigned long)(unsigned int)LOG_ARG(args, ai);
            fmt_num(w, u, 0, 16, (c == 'X'), width, pad, left);
            break;
        }
        case 'p': {
            void *p = (void *)LOG_ARG(args, ai);
            fmt_num(w, (unsigned long)(uintptr_t)p, 0, 16, 0, width, pad, left);
            break;
        }
        case 'c':
            log_buf_putc(w, (char)LOG_ARG(args, ai));
            break;
        case 's': {
            const char *s = (const char *)LOG_ARG(args, ai);
            if (s == NULL)
            {
                s = "(null)";
            }
            size_t len = strlen(s);
            if (!left)
            {
                fmt_pad(w, ' ', (size_t)(width > (int)len ? width - (int)len : 0));
            }
            log_buf_write(w, s, len);
            if (left)
            {
                fmt_pad(w, ' ', (size_t)(width > (int)len ? width - (int)len : 0));
            }
            break;
        }
        default:
            /* 未知转换符：整段规格字面输出（含已解析的宽度/标志） */
            log_buf_write(w, spec_start, (size_t)(fmt - spec_start));
            break;
        }
    }
    fmt_flush(w);
}

#endif /* OM_USE_LOG */

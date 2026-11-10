/**
 * @file core.c
 * @brief log 核心：om_log_log 入口（模块级过滤 → 打包 → 生产入环）+ om_log_panic 直出 + emit
 * @details 过滤：① 模块级别（初始=宏参数；运行时可调节——入口处，生产前）→
 *          （后端级在消费时刻按 (module, level) 一次求值——accept_mask（per-backend
 *          默认级 + 按模块覆盖，见 backend.c）；位图 = 0 → 零格式化快路径）→
 *          打包（生产时刻时间戳）→ log_ring_produce（见 ring.c：临界区入环——满丢+计数；
 *          消费触发 OM_LOG_ASYNC 决定：日志线程抽环 / 现场判定+drain）。
 *          打日志无失败路径；形态唯一（消息环）——"deferred"即消费未就绪时的环滞留态。
 *          结构：emit 独立（log_emit_build/log_emit_args——内容组合唯一事实源）；
 *          头部生成独立（时间戳统一——来自参数包 ts 或 panic 当场）。
 */

#include "core/om_config.h"

#if OM_USE_LOG

#include "core/om_def.h"
#include "core/om_init.h" /* OM_INIT_SERVICE——消费调度器/服务就绪点（组成层编排） */
#include "core/om_interrupt.h"
#include "osal/osal_time.h" /* 时间戳来源 osal_time_now_monotonic（host 测试经本地桩头） */

#include "log_internal.h"

#include <stdarg.h>
#include <string.h>

/** @brief 消息环实例（服务主体持有——能力-实例分离：状态在此，能力见 ring.c；
 *  静态零初始化——惰性 init 保证早期/最小配置可用） */
static LogRing g_log_ring;

/** @brief 框架内部告警模块实例（"log"——丢弃告警的消息头 module 标注；不占用模块注册表） */
static OmLogModule s_log_module = {"log", OM_LOG_LEVEL_DEBUG, -1};

/** @brief 访问日志服务内部告警模块（log_internal 契约——log_drop_warn 用之） */
const OmLogModule *log_service_module(void)
{
    return &s_log_module;
}

/* 级别名表（3 字符）：尺寸绑定 OM_LOG_LEVEL_OFF——级别枚举扩展必须同步本表，
 * 否则下方编译期断言拦截（运行时无需再校验：emit 入口已保证 level < OFF） */
static const char *const log_level_name[OM_LOG_LEVEL_OFF] = {
    [OM_LOG_LEVEL_DEBUG] = "DBG",
    [OM_LOG_LEVEL_INFO] = "INF",
    [OM_LOG_LEVEL_WARN] = "WRN",
    [OM_LOG_LEVEL_ERROR] = "ERR",
    [OM_LOG_LEVEL_FATAL] = "FTL",
};

_Static_assert(OM_LOG_LEVEL_FATAL + 1 == OM_LOG_LEVEL_OFF,
               "log 级别枚举扩展需同步 log_level_name 级别名表");

/** @brief 扇出回调：把格式化段推给位图接受的后端（逐段仅按位位移——零查表）
 *  @param ctx 编码的接受位图（log_emit_args 求值一次后传入）
 *  @param seg 段数据
 *  @param len 段字节数 */
static void emit_fanout(void *ctx, const char *seg, size_t len)
{
    log_backend_push_mask((uint8_t)(uintptr_t)ctx, seg, len);
}

/** @brief panic 扇出回调：无过滤提满投递（故障上下文——证据保全）
 *  @param ctx 未使用（与 emit_fanout 同形）
 *  @param seg 段数据
 *  @param len 段字节数 */
static void emit_fanout_panic(void *ctx, const char *seg, size_t len)
{
    (void)ctx;
    log_backend_panic_push_all(seg, len);
}

/** @brief 头部生成：输出 "[LVL][HH:MM:SS.mmm][module] "（时间戳统一——参数包 ts 全链一致）
 *  @param w 写器
 *  @param module 模块实例（name 已由入口校验非 NULL）
 *  @param level 消息级别
 *  @param ts_ms 时间戳单调 ms（生产时刻——消息环路径随包传递；panic 当场取）
 *  @note 格式换算 log_time_format（纯函数，逐字符填数——不经过 log_format，避免依赖格式化器）
 *  @note log_level_name[level] 下标安全：入口 OFF 检查保证 level < OM_LOG_LEVEL_OFF，
 *        表尺寸 = OM_LOG_LEVEL_OFF（编译期断言保证枚举与表同步） */
static void emit_header(LogBufWriter *w, const OmLogModule *module, OmLogLevel level, uint32_t ts_ms)
{
    char ts[13];
    size_t n = log_time_format(ts, ts_ms); /* 恒 12 */
    log_buf_putc(w, '[');
    log_buf_write(w, log_level_name[level], 3);
    log_buf_putc(w, ']');
    log_buf_putc(w, '[');
    log_buf_write(w, ts, n);
    log_buf_putc(w, ']');
    log_buf_putc(w, '[');
    log_buf_write(w, module->name, strlen(module->name));
    log_buf_write(w, "] ", 2);
}

/** @brief 日志构建核心：头部 + 流式格式化 + 尾部 \n + flush（一条日志的内容组合唯一事实源）
 *  @param module 模块实例
 *  @param level 消息级别
 *  @param ts 时间戳单调 ms
 *  @param fmt 格式串
 *  @param ap 可变参数
 *  @param out 段输出回调
 *  @param out_ctx 回调上下文
 *  @note 无条件编译（va_list 版）；执行者 = panic 直出（消息环路径经 log_emit_args——参数包
 *        版，ts 随包传递）；栈占用 = 段缓冲（OM_LOG_SEGMENT_SIZE）+ 写器状态 */
void log_emit_build(const OmLogModule *module, OmLogLevel level, uint32_t ts, const char *fmt,
                    va_list ap, LogOutFn out, void *out_ctx)
{
    LogBufWriter w;
    char seg[OM_LOG_SEGMENT_SIZE];
    log_buf_writer_init(&w, out, out_ctx, seg, sizeof(seg));
    emit_header(&w, module, level, ts);
    log_format(&w, fmt, ap);
    log_buf_write(&w, "\n", 1); /* 统一结束符：框架侧一处，广播一致（后端可映射，见 README） */
    log_buf_flush(&w);
}

/** @brief panic emit：无过滤 + panic 投递（提满全出；时间戳 = 当场——panic 无打包）
 *  @param module 模块实例
 *  @param level 消息级别
 *  @param fmt 格式串
 *  @param ap 可变参数
 *  @note emit_header 复用——级别标注自动保留；时间戳取当前时刻（非生产——panic 场景一致） */
static void log_emit_panic(const OmLogModule *module, OmLogLevel level, const char *fmt, va_list ap)
{
    log_emit_build(module, level, osal_time_now_monotonic(), fmt, ap, emit_fanout_panic, NULL);
}

/** @brief emit（消费时刻）：一次求值 accept_mask（per-backend 生效级）→ 头部（ts=参数包
 *        生产时刻）+ 流式格式化 + 尾部 \n + 按位图逐段扇出
 *  @param msg 消息包（module/level/ts/fmt/args/n 全内含）
 *  @note 消息环消费侧唯一执行者（日志线程 / 现场触发 / 服务就绪回放——执行位置不同，
 *        内容与 ts 一致）；管线不变式 = 过滤一次 + 格式化一次 + 逐段扇出 */
void log_emit_args(const OmLogMsg *msg)
{
    uint8_t mask = log_backend_accept_mask(msg->module, msg->level);
    if (mask == 0)
    {
        return; /* ② 被全部后端拒绝（含按模块覆盖）→ 零格式化快路径 */
    }
    LogBufWriter w;
    char seg[OM_LOG_SEGMENT_SIZE];
    log_buf_writer_init(&w, emit_fanout, (void *)(uintptr_t)mask, seg, sizeof(seg));
    emit_header(&w, msg->module, msg->level, msg->ts);
    log_format_args(&w, msg->fmt, msg->argBuf, msg->argCount);
    log_buf_write(&w, "\n", 1); /* 统一结束符：框架侧一处，广播一致（后端可映射，见 README） */
    log_buf_flush(&w);
}

void om_log_log(const OmLogModule *module, OmLogLevel level, const char *fmt, ...)
{
    va_list ap;
    if (module == NULL || module->name == NULL || fmt == NULL)
    {
        return; /* 无失败路径：非法参数静默 */
    }
    if (level >= OM_LOG_LEVEL_OFF)
    {
        return;
    }
    if (module->moduleId < 0)
    {
        (void)log_module_check_in(module); /* 惰性登记（一次性——表满置 -2 抑制重复） */
    }
    if (level < module->level)
    {
        return; /* ① 模块级别（初始=宏参数；运行时调节后按新值过滤——单字段） */
    }
    va_start(ap, fmt);
    {
        OmLogMsg msg;
        if (log_msg_build(&msg, module, level, osal_time_now_monotonic(), fmt, ap))
        {
            log_ring_produce(&g_log_ring, &msg); /* 生产入环：形态唯一（实例属主=服务主体；
                                                  * 满丢+计数；无消费者=滞留=“deferred”回归形态） */
        }
    }
    va_end(ap);
}

/** @brief 服务启动（OM_INIT_SERVICE 统一注册——模式差异仅在实现体）：
 *  OM_LOG_ASYNC=1 启动异步消费调度器（门铃+线程入实例——之前生产流入环滞留，线程首轮
 *  drain 接住）；=0 服务就绪点回放滞留段（调度器后、后端已注册——DRIVER 级）
 *  @return OM_OK；OM_ERR_NO_MEM（仅异步模式——调度器启建失败） */
OmRet log_service_start(void)
{
#if OM_LOG_ASYNC
    return log_async_start(&g_log_ring);
#else
    log_ring_flush(&g_log_ring);
    return OM_OK;
#endif
}
OM_INIT_SERVICE(log_service_start);

/** @brief 服务侧环满丢弃计数（服务主体实例——om_log_stats 汇总用） */
uint32_t log_service_dropped(void)
{
    return log_dropped_ring(&g_log_ring);
}

/** @brief 故障直出（契约见 services/log/log.h）
 *  @param module 模块实例
 *  @param level 消息级别（标注保留；提满——无过滤）
 *  @param fmt 格式串
 *  @note 自身禁中断（嵌套安全——fatal handler 调用时中断可能仍开；嵌套安全原语）→
 *        调用侧同步格式化 + panic 投递（钩子优先/push 兜底）→ 恢复中断 */
void om_log_panic(const OmLogModule *module, OmLogLevel level, const char *fmt, ...)
{
    va_list ap;
    port_critical_key_t key;
    if (module == NULL || module->name == NULL || fmt == NULL || level >= OM_LOG_LEVEL_OFF)
    {
        return; /* 无失败路径：非法参数静默 */
    }
    va_start(ap, fmt);
    key = om_hw_disable_interrupt(); /* panic 自身禁中断（嵌套安全——与正常路径临界区同款原语） */
    log_emit_panic(module, level, fmt, ap);
    om_hw_restore_interrupt(key);
    va_end(ap);
}

#endif /* OM_USE_LOG */

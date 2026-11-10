/**
 * @file log_internal.h
 * @brief log 服务私有共享接口（formatter/core/backend 三源文件内部互调，不进公共头）
 * @details 段缓冲写器（LogBufWriter）与流式格式化归 formatter.c；后端接受判定与扇出
 *          归 backend.c；core.c 在临界区内经本接口编排。公共契约见 services/log/log.h。
 */

#ifndef __LOG_INTERNAL_H__
#define __LOG_INTERNAL_H__

#include "core/om_def.h"
#include "core/om_config.h"         /* OM_LOG_MAX_ARGS（OmLogMsg 参数包宽度，log_internal.h 专属） */
#include "services/log/log.h"       /* OmLogLevel（级别类型，公共头已含 core/om_def.h） */
#include "data_struct/ringbuffer.h" /* LogRing 内嵌 Ringbuf（data_struct——纯原子无 OSAL） */

typedef struct OsalSemHandle_s OsalSem; /* 门铃句柄（未完成类型前置声明——host 无 OSAL 桩
                                         * 亦兼容；ring.c/osal_sem.h 再包含完整定义） */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 段输出回调（每段一次调用；log 服务实现：扇出到通过过滤的后端） */
typedef void (*LogOutFn)(void *ctx, const char *seg, size_t len);

/** @brief 段缓冲写器：格式化/头部/尾部共用；满段自动 flush 回调，任意长不截断 */
typedef struct
{
    LogOutFn out;
    void *outCtx;
    char *seg;
    size_t segSize;
    size_t segLen;
} LogBufWriter;

/** @brief 初始化段缓冲写器（调用方提供段缓冲；满段自动 flush 回调）
 *  @param w 写器
 *  @param out 段输出回调
 *  @param out_ctx 回调上下文
 *  @param seg 段缓冲（大小即分段粒度）
 *  @param seg_size 段缓冲字节数（须 > 0） */
void log_buf_writer_init(LogBufWriter *w, LogOutFn out, void *out_ctx, char *seg, size_t seg_size);

/** @brief 写入 n 字节（超段自动 flush 回调，任意长不截断）
 *  @param w 写器
 *  @param s 数据源
 *  @param n 字节数 */
void log_buf_write(LogBufWriter *w, const char *s, size_t n);

/** @brief 写入单字符（log_buf_write 特例）
 *  @param w 写器
 *  @param c 字符 */
void log_buf_putc(LogBufWriter *w, char c);

/** @brief 强制刷出残留段（一条日志的尾部调用）
 *  @param w 写器 */
void log_buf_flush(LogBufWriter *w);

/** @brief printf 风格子集：标志 -/0、宽度、长度 l；转换符 d i u x X p c s %
 *  @param w 写器（经其段缓冲与 out 回调输出）
 *  @param fmt 格式串
 *  @param ap 可变参数
 *  @note 未知/不完整转换符降级为整段规格字面输出；LONG_MIN 取负未处理（文档约束） */
void log_format(LogBufWriter *w, const char *fmt, va_list ap);

/** @brief 参数包（消息环的唯一 payload —— fmt+args 延后到消费时刻格式化）
 *  @note argBuf 为 uintptr_t 宽参数数组（8 个 = 64B）：整型/指针直接存；
 *        %s 只存指针——字符串生命周期由调用方保证至消费时刻；
 *        ts = 生产时刻单调 ms（消费时刻格式化输出——printk 同款时间戳语义） */
typedef struct
{
    const char *fmt;
    OmLogLevel level;
    const OmLogModule *module;
    uint32_t ts;
    uintptr_t argBuf[OM_LOG_MAX_ARGS];
    uint32_t argCount;
} OmLogMsg;

/** @brief 参数数组版格式化（与 log_format(va_list) 并存——日志线程/同步模式均可） */
void log_format_args(LogBufWriter *w, const char *fmt, const uintptr_t *args, size_t n);

typedef struct
{
    char conv;   /* 转换符；'\0' = 尾部不完整规格（余下全为字面） */
    int is_long; /* 长度修饰 l（类型宽判定） */
    int width;   /* 最小宽度（0 = 无） */
    char pad;    /* 填充字符（' ' 或 '0'） */
    int left;    /* 左对齐（'-' 标志） */
} LogSpec;

/** @brief 规格解析（格式化语法唯一事实源）：从 '%' 起解析标志（-/0/宽度/l）（输出聚合——最小参数形态）
 *  @param fmtp inout：指向 '%'；成功时前进到转换符之后；不完整（解析至 '\0'）时前进到 '\0'
 *  @return 聚合规格（conv='\0' = 尾部不完整，余下全为字面） */
LogSpec log_spec_next(const char **fmtp);

/** @brief 单调 ms → HH:MM:SS.mmm（十进制，"：" 分隔时分、"." 分隔毫秒）
 *  @param buf 输出缓冲（>=13B，含 NUL）
 *  @param ms 单调毫秒（osal_time_now_monotonic）
 *  @return 写入字节数（不含 NUL，恒 12） */
size_t log_time_format(char *buf, uint32_t ms);

/** @brief 打包：按 fmt 解析参数数 → va_list 逐参抓取进 argBuf（超限丢弃）
 *  @param ts 生产时刻单调 ms（调用方取 osal_time_now_monotonic——core.c）
 *  @return true = 打包成功（<= OM_LOG_MAX_ARGS 参）；false = 超限丢弃（已计数） */
bool log_msg_build(OmLogMsg *msg, const OmLogModule *module, OmLogLevel level, uint32_t ts,
                   const char *fmt, va_list ap);

/** @brief 读取丢计数（参数包超限——msg.c 维护；om_log_stats 汇总用）
 *  @return 累计超限丢弃数（自启动以来） */
uint32_t log_dropped_overflow(void);

/** @brief 惰性登记模块（首次日志调用入库——幂等；表满返回 -2（抑制重复尝试））
 *  @param module 模块实例
 *  @return >=0 = moduleId；-2 = 表满；-3 = 参数非法 */
int log_module_check_in(const OmLogModule *module);

/** @brief 按名解析模块 id（只读——backend.c 覆盖表按 id 索引用）
 *  @param name 模块名
 *  @return >=0 = moduleId；-1 = 未登记/不存在（首次打日志后登记——惰性语义） */
int log_module_resolve_id(const char *name);

/** @brief emit（日志线程/就绪路径）：后端接受判定 → 头部 + 流式格式化 + 尾部
 + 扇出
 *  @param msg 消息包（module/level/fmt/args/n 全内含——最小参数形态）
 *  @note 就绪路径 = 日志线程调此函数；兜底路径 = 调用侧（va_list 版 log_emit 或 log_emit_panic） */
void log_emit_args(const OmLogMsg *msg);

/** @brief 日志构建核心：头部 + 流式格式化 + 尾部 \n + flush 到指定 out 回调
 *  @param module 模块实例
 *  @param level 消息级别
 *  @param ts 时间戳单调 ms（生产时刻——panic 当场取）
 *  @param fmt 格式串
 *  @param ap 可变参数
 *  @param out 段输出回调（扇出 / panic 投递——执行者唯一差异；一条日志的内容组合事实源）
 *  @param out_ctx 回调上下文
 *  @note 无条件编译（va_list 版）；执行者 = panic 直出（消息环路径经 log_emit_args——
 *        参数包版，ts 随包传递） */
void log_emit_build(const OmLogModule *module, OmLogLevel level, uint32_t ts, const char *fmt,
                    va_list ap, LogOutFn out, void *out_ctx);

/** @brief 丢弃后验告警状态（每丢弃点一实例；warned_upto=已上报累计，last_warn_ms=上次上报
 *  时刻；ever=已上报过（零初始化兼容——首次放行不依赖哨兵值）） */
typedef struct
{
    uint32_t warned_upto;
    uint32_t last_warn_ms;
    uint8_t ever;
} LogDropWarnState;

/** @brief 消息环实例（服务主体——core.c 持有；静态零初始化——惰性 init 保证早期可用）
 *  @note 实例化与能力分离（框架惯例——Pipe/mpsc 同构）：ring.c 仅提供能力，
 *        实例由使用者持有；丢弃/告警状态随实例；doorbell 仅 OM_LOG_ASYNC（消费调度器） */
typedef struct
{
    Ringbuf rb;
    uint8_t buf[OM_LOG_RING_LEN * sizeof(OmLogMsg)];
    bool inited;
    uint32_t dropped;
    LogDropWarnState warn;
#if OM_LOG_ASYNC
    OsalSem *doorbell; /* 门铃（空→非空才 post——pipe 模式） */
#endif
} LogRing;

/** @brief 生产入环（msg 已打包；临界区内 ringbuf_in——多生产者收敛单写者；满 = 丢新 + 计数）
 *  @param ring 环实例（服务主体持有）
 *  @param msg 参数包（core.c 打包后传入）
 *  @note OM_LOG_ASYNC：门铃空→非空才 post（pipe 模式——节省唤醒）；
 *        =0：后接现场判定（accept_mask != 0 → drain 全量保生产序） */
void log_ring_produce(LogRing *ring, const OmLogMsg *msg);

/** @brief 消费抽环：逐条 out → log_emit_args（格式化+扇出；单消费者——SPSC 读侧）；
 *         尾部检查丢弃后验告警（节流） */
void log_ring_drain(LogRing *ring);

/** @brief 服务就绪点回放：同步模式（OM_INIT_SERVICE 调用）——drain 滞留段；异步模式
 *         线程已接管（no-op） */
void log_ring_flush(LogRing *ring);

/** @brief 读取消息环满丢弃计数（om_log_stats 汇总用） */
uint32_t log_dropped_ring(const LogRing *ring);

#if OM_LOG_ASYNC
/** @brief 异步消费调度器启动（log_async.c）：建门铃（存入实例字段）+ 日志线程（LOW 带）
 *  @param ring 环实例（线程经 arg 引用——无跨文件静态状态）
 *  @return OM_OK；OM_ERR_NO_MEM 门铃/线程创建失败 */
OmRet log_async_start(LogRing *ring);
#endif

/** @brief 服务启动（服务主体实现——OM_INIT_SERVICE 统一注册；模式差异仅在实现体：
 *  async=启动消费调度器（OM_ERR_NO_MEM 可见）/ sync=回放滞留段） */
OmRet log_service_start(void);

/** @brief 服务侧环满丢弃计数（服务主体实例——om_log_stats 汇总用） */
uint32_t log_service_dropped(void);

/** @brief 丢弃后验告警：丢弃只计数 → 补发 WRN 自证（节流 + 增量报告）
 *  @param st 状态（每丢弃点一个）
 *  @param module 告警模块实例（log_service_module——"log"）
 *  @param site 丢弃点描述（静态字符串：如 "ring-full"）
 *  @param dropped 丢弃点累计丢弃数
 *  @param now_ms 单调毫秒（调用方取 osal_time_now_monotonic）
 *  @return true = 已补发 WRN（内部经 log_emit_args——直接 emit，不走环，无递归）
 *  @note 节流：距上次 >= OM_LOG_DROP_WARN_INTERVAL_MS；报告 = 增量 + 累计 */
bool log_drop_warn(LogDropWarnState *st, const OmLogModule *module, const char *site,
                   uint32_t dropped, uint32_t now_ms);

/** @brief 框架内部告警模块实例（"log"——丢弃告警的消息头 module 标注；不进模块注册表） */
const OmLogModule *log_service_module(void);

/** @brief 接受位图求值：bit i = 后端 i 接受该 (module, level)（used && level >= 生效级；
 *         生效级 = 按模块覆盖命中 ? 覆盖值 : 默认级；moduleId < 0 = 未登记/表满 →
 *         覆盖表不可查 → 按默认级（兜底））
 *  @param module 消息模块实例
 *  @param level 消息级别
 *  @return 接受位图（0 = 被全部后端拒绝 → 调用方零格式化快路径；一条消息求值一次，
 *          逐段推送按位图位移——push_mask 零查表） */
uint8_t log_backend_accept_mask(const OmLogModule *module, OmLogLevel level);

/** @brief 扇出：对 mask 置位的后端逐段 push（表序；不做二次过滤判定——mask 即裁判结果）
 *  @param mask 接受位图（log_backend_accept_mask 产物）
 *  @param seg 段数据
 *  @param len 段字节数 */
void log_backend_push_mask(uint8_t mask, const char *seg, size_t len);

/** @brief panic 投递：无过滤遍历全部已注册后端，panic 钩子优先（NULL→push 尽力）
 *  @param seg 段数据
 *  @param len 段字节数 */
void log_backend_panic_push_all(const char *seg, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* __LOG_INTERNAL_H__ */

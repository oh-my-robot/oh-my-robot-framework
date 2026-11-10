/**
 * @file log.h
 * @brief log 服务公共 API（services 层）
 * @details 统一消息环 + 流式格式化 + 模块注册制 + 后端抽象广播（后端级 = 默认级 +
 *          按模块覆盖——每条消息按 (module, level) 交付前求值一次）。
 *          设计文档：services/log/README.md。
 *          用法：
 *            OM_LOG_MODULE(supercap, OM_LOG_LEVEL_INFO);   // 每个 .c 顶部一次
 *            OM_LOG_INFO("电压 %d.%02d V", mv / 1000, mv % 1000);
 *          后端：om_log_backend_register + om_log_backend_set_level。
 *          契约：调用宏引用本 TU 的 _om_log_module——未注册模块就调用 = 编译错误；
 *               每 TU 一次注册；中断上下文可调用（只打 ERROR/FATAL 短消息）；
 *               后端 push 必须快速提交（不得阻塞轮询）、不得假设一次 push = 一条日志。
 *          裁剪契约（OM_USE_LOG=0 零副作用——业界 Zephyr CONFIG_LOG=n 同款）：
 *              类型（OmLogLevel/OmLogModule/OmLogBackend/OmLogStats）无条件——外围
 *              结构体/回调可继续引用；函数声明消失（调用 = 编译错误——信号清晰）；
 *              调用宏 no-op（OM_LOG_* 展开空、OM_LOG_MODULE 不生成实例）。详见 README。
 */

#ifndef __OM_LOG_H__
#define __OM_LOG_H__

#include "core/om_def.h"
#include "core/om_config.h" /* OM_LOG_MAX_ARGS（编译期参数上限宏——调用宏检查用）；
                             * OM_USE_LOG（服务级裁剪开关） */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 日志级别：升序严重度（数值越低越详细）
 *  @note OFF 置顶——设置为 OFF = 全关（msg >= OFF 永不成立）；MAX 为计数哨兵；
 *        枚举只增不改（后补 TRACE 安全） */
typedef enum {
    OM_LOG_LEVEL_DEBUG = 0,
    OM_LOG_LEVEL_INFO,
    OM_LOG_LEVEL_WARN,
    OM_LOG_LEVEL_ERROR,
    OM_LOG_LEVEL_FATAL,
    OM_LOG_LEVEL_OFF,
    OM_LOG_LEVEL_MAX,
} OmLogLevel;

/** @brief 模块实例（OM_LOG_MODULE 生成；level=模块级别——初始=宏参数，运行时经
 *        om_log_module_set_level 按名调节）；
 *        moduleId=-1=未登记（首次日志惰性入库——set_level 可查）；实例可写（登记/调节） */
typedef struct OmLogModule {
    const char *name;
    OmLogLevel level;
    int moduleId;
} OmLogModule;

/** @brief 输出后端：分段友好（一条日志多段回调）+ 快速提交（绝不阻塞轮询）
 *  @note push/flush 携带 backend 指针——实现者经 container_of 取实例状态（支持多实例）；
 *        后端结构体内嵌进实例结构体，位置任意
 *        （container_of 经 offsetof 定位，无需首成员） */
typedef struct OmLogBackend {
    const char *name;                                                             /* 查找/调试用 */
    void (*push)(struct OmLogBackend *backend, const char *segment, size_t len);  /* 流式段推送（线程/中断上下文均可能） */
    void (*flush)(struct OmLogBackend *backend);                                  /* 可选：强制刷出，可为 NULL */
    void (*panic)(struct OmLogBackend *backend, const char *segment, size_t len); /* 可选：故障上下文提交（队列/线程/锁不可信时的最可靠通道——串口=轮询写/DMA 死仍有效；NULL=panic 时退回 push 尽力而为） */
} OmLogBackend;

/** @brief 日志统计（查询 API——丢弃可观测）
 *  @note dropped 累计 = 参数包超限丢弃（log_msg_build）+ 消息环满丢弃（printk 语义）；
 *        丢弃点另由 log 服务侧补发 WRN 自证（节流——见服务 README 丢弃点节） */
typedef struct
{
    uint32_t dropped;
} OmLogStats;

#if OM_USE_LOG

/** @brief 日志入口：模块级过滤 → 打包（生产时刻时间戳）→ 消息环生产
 *  @param module 模块实例（OM_LOG_MODULE 生成；NULL 或 name 为 NULL 时静默返回）
 *  @param level 消息级别（>= OM_LOG_LEVEL_OFF 时静默返回）
 *  @param fmt printf 风格子集格式串（NULL 时静默返回）
 *  @note 无失败路径（打日志不打扰调用方）；后端级过滤在消费时刻（per-backend）；
 *        环满 = 丢弃 + 计数（后验告警）；无消费者/无后端时消息滞留环中（早期日志
 *        = "deferred"回归形态——服务就绪后回放）；线程/中断上下文均可调；
 *        format(printf, 3, 4) 供编译器逐调用点校验 */
void om_log_log(const OmLogModule *module, OmLogLevel level, const char *fmt, ...)
    OM_ATTRIBUTE((format(__printf__, 3, 4)));

/** @brief 故障直出：fatal/崩溃上下文（系统即将停止/残破）中同步输出——不依赖队列/线程/锁
 *  @param module 模块实例（名称标注）
 *  @param level 消息级别（标注保留；**过滤提满**——崩溃现场全出，见语义）
 *  @param fmt printf 风格格式串
 *  @note 语义：自身禁中断（嵌套安全）→ 调用侧同步格式化 → 后端提交 = panic 钩子优先（NULL→push 尽力）；
 *        无 per-backend 过滤（崩溃时证据保全优先）；级别标注保留（如打 ERROR 则头部 [ERR]）；
 *        调用者须在故障上下文（handler/断言失败路径）调用；正常路径请用 OM_LOG_* */
void om_log_panic(const OmLogModule *module, OmLogLevel level, const char *fmt, ...)
    OM_ATTRIBUTE((format(__printf__, 3, 4)));

/** @brief 注册输出后端（携带初始级别；运行时用 om_log_backend_set_level 动态调整）
 *  @param backend 后端实例（name 与 push 不得为 NULL）
 *  @param level 初始级别（过滤语义同 set_level：只接收 level >= 目标级别的消息）
 *  @return OM_OK 成功；OM_ERR_ALREADY 重复注册；OM_ERR_FULL 表满；
 *          OM_ERR_INVALID_ARG 参数非法或级别越界（>= OM_LOG_LEVEL_MAX） */
OmRet om_log_backend_register(OmLogBackend *backend, OmLogLevel level);

/** @brief 注销输出后端
 *  @param backend 已注册的后端实例指针
 *  @return OM_OK 成功；OM_ERR_NOT_FOUND 指针不在表内；OM_ERR_INVALID_ARG 参数非法 */
OmRet om_log_backend_unregister(OmLogBackend *backend);

/** @brief 设置后端级别：只接收 level >= 目标级别的消息；OM_LOG_LEVEL_OFF = 全关
 *  @param backend_name 后端名称（strcmp 按名查找）
 *  @param level 目标级别（>= OM_LOG_LEVEL_MAX 为越界）
 *  @return OM_OK 成功；OM_ERR_NOT_FOUND 名称未找到；OM_ERR_INVALID_ARG 参数非法或级别越界 */
OmRet om_log_backend_set_level(const char *backend_name, OmLogLevel level);

/** @brief 运行时调节模块级别（按名——模块首次打日志后登记；未登记 NOT_FOUND）
 *  @param module_name 模块名（OM_LOG_MODULE 参数）
 *  @param level 目标级别（>= OM_LOG_LEVEL_MAX = INVALID_ARG）
 *  @return OM_OK；OM_ERR_NOT_FOUND；OM_ERR_INVALID_ARG */
OmRet om_log_module_set_level(const char *module_name, OmLogLevel level);

/** @brief 查询模块级别 */
OmRet om_log_module_get_level(const char *module_name, OmLogLevel *level);

/** @brief 设置后端对某模块的覆盖级别（后端过滤级 = 默认级 + 按模块覆盖——
 *  消息命中覆盖即按覆盖值裁判，未命中回默认级）
 *  @param backend_name 后端名称（strcmp 按名查找；未找到 NOT_FOUND）
 *  @param module_name 模块名（模块首次打日志后才登记——未登记 NOT_FOUND，与
 *        om_log_module_set_level 同惰性语义）
 *  @param level 覆盖级别（>= OM_LOG_LEVEL_MAX = INVALID_ARG；OM_LOG_LEVEL_OFF = 显式拒——
 *        该模块对该后端全拒；注册默认 OFF + 覆盖抬升 = 白名单形态）
 *  @return OM_OK 成功；OM_ERR_NOT_FOUND 后端名/模块名未找到；OM_ERR_INVALID_ARG
 *         参数非法或级别越界 */
OmRet om_log_backend_set_module_level(const char *backend_name, const char *module_name,
                                      OmLogLevel level);

/** @brief 清除后端对某模块的覆盖（生效级回退该后端默认级）
 *  @param backend_name 后端名称
 *  @param module_name 模块名（未登记模块 = NOT_FOUND——模块首次打日志后登记，见
 *        om_log_backend_set_module_level）
 *  @return OM_OK 成功；OM_ERR_NOT_FOUND 后端名/模块名未找到；OM_ERR_INVALID_ARG 参数非法 */
OmRet om_log_backend_clear_module_level(const char *backend_name, const char *module_name);

/** @brief 查询后端对某模块的生效级（覆盖命中 → 覆盖值；未覆盖 → 该后端默认级）
 *  @param backend_name 后端名称
 *  @param module_name 模块名（未登记模块 = NOT_FOUND——模块首次打日志后登记，见
 *        om_log_backend_set_module_level）
 *  @param level 输出（NULL → OM_ERR_INVALID_ARG）
 *  @return OM_OK 成功；OM_ERR_NOT_FOUND 后端名/模块名未找到；OM_ERR_INVALID_ARG 参数非法 */
OmRet om_log_backend_get_module_level(const char *backend_name, const char *module_name,
                                      OmLogLevel *level);

/** @brief 读取日志统计（累计丢弃数——超限 + 消息环满）
 *  @param stats 输出（NULL → OM_ERR_INVALID_ARG）
 *  @return OM_OK 成功；OM_ERR_INVALID_ARG 参数非法 */
OmRet om_log_stats(OmLogStats *stats);

#else /* !OM_USE_LOG：裁剪 = 接口消失（编译期信号）+ 调用宏 no-op（零副作用） */

#define OM_LOG_MODULE(name, level) /* 日志裁剪：无模块实例 */

#define OM_LOG_DEBUG(...)          ((void)0)
#define OM_LOG_INFO(...)           ((void)0)
#define OM_LOG_WARN(...)           ((void)0)
#define OM_LOG_ERROR(...)          ((void)0)
#define OM_LOG_FATAL(...)          ((void)0)

#endif /* OM_USE_LOG */

#ifdef __cplusplus
} /* extern "C" */
#endif

#if OM_USE_LOG

/**
 * @brief 模块注册：每个 TU 顶部一次，生成静态 _om_log_module
 * @param name 模块名（诊断/查找用）
 * @param level 模块级别（初始=宏参数；运行时经 om_log_module_set_level 调节——同一字段）
 * @note 同一 TU 重复调用 = 重复定义；未注册就使用调用宏 = 编译错误（特性）；
 *       实例可写（moduleId 登记与 level 调节）——勿 const 化 */
#define OM_LOG_MODULE(name, level) OM_USED static OmLogModule _om_log_module = {(#name), (level), -1}

/* 编译期参数数计数（实参计数宏技巧——与格式串内容无关，数 __VA_ARGS__ 个数；
 * 支持 1..16 个参数（表上限 = OM_LOG_MAX_ARGS 配置上限——om_config.h #error 守卫配套），
 * 0 参数（空 __VA_ARGS__）经 helper 折叠为 0。仅需配置 OM_LOG_MAX_ARGS（≤16 自动同步） */
#define OM_LOG_VA_COUNT_HELPER(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N
#define OM_LOG_VA_COUNT(...)                                                                                  OM_LOG_VA_COUNT_HELPER(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

/* 编译期参数上限检查：负数组 = 编译错误（仓库 bsp 负数组先例）；超限不再静默丢弃
 * （配合 OM_LOG_MAX_ARGS 调大 / 拆分，见服务 README） */
#define OM_LOG_ARG_LIMIT_OK(...)     \
    typedef char om_log_arg_limit_ok \
        [(OM_LOG_VA_COUNT(__VA_ARGS__) <= OM_LOG_MAX_ARGS) ? 1 : -1]

/** @brief 调用宏（引用本 TU 的 _om_log_module；fmt 为 printf 风格子集）
 *  @note 编译期双重防线（调用者零感知）：① format(printf) 属性——编译器逐调用点校验
 *        格式串与实参类型/个数匹配；② __VA_ARGS__ 计数负数组——超 OM_LOG_MAX_ARGS
 *        直接编译报错（而非运行期静默丢弃）。展开含 do-while 块（负数组 typedef 作用域隔离） */
#define OM_LOG_DEBUG(fmt, ...)                                               \
    do {                                                                     \
        OM_LOG_ARG_LIMIT_OK(__VA_ARGS__);                                    \
        (void)sizeof(om_log_arg_limit_ok);                                   \
        om_log_log(&_om_log_module, OM_LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__); \
    } while (0)
#define OM_LOG_INFO(fmt, ...)                                               \
    do {                                                                    \
        OM_LOG_ARG_LIMIT_OK(__VA_ARGS__);                                   \
        (void)sizeof(om_log_arg_limit_ok);                                  \
        om_log_log(&_om_log_module, OM_LOG_LEVEL_INFO, fmt, ##__VA_ARGS__); \
    } while (0)
#define OM_LOG_WARN(fmt, ...)                                               \
    do {                                                                    \
        OM_LOG_ARG_LIMIT_OK(__VA_ARGS__);                                   \
        (void)sizeof(om_log_arg_limit_ok);                                  \
        om_log_log(&_om_log_module, OM_LOG_LEVEL_WARN, fmt, ##__VA_ARGS__); \
    } while (0)
#define OM_LOG_ERROR(fmt, ...)                                               \
    do {                                                                     \
        OM_LOG_ARG_LIMIT_OK(__VA_ARGS__);                                    \
        (void)sizeof(om_log_arg_limit_ok);                                   \
        om_log_log(&_om_log_module, OM_LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__); \
    } while (0)
#define OM_LOG_FATAL(fmt, ...)                                               \
    do {                                                                     \
        OM_LOG_ARG_LIMIT_OK(__VA_ARGS__);                                    \
        (void)sizeof(om_log_arg_limit_ok);                                   \
        om_log_log(&_om_log_module, OM_LOG_LEVEL_FATAL, fmt, ##__VA_ARGS__); \
    } while (0)

#endif /* OM_USE_LOG */

#endif /* __OM_LOG_H__ */

/**
 * @file main.c
 * @brief 串口日志后端按模块覆盖过滤演示（默认档 / 覆盖放宽 / 覆盖显式拒）
 * @details 组合层接线：OM_INIT_DRIVER 注册串口日志后端（默认级 WARN——调度器前，
 *          早于 SERVICE 级业务日志；更早级别（EARLIEST/BOARD）的日志入消息环滞留，
 *          服务就绪后回放（"deferred"形态，契约见 services/log/README.md））；
 *          OM_INIT_APPLICATION 做按模块覆盖配置（SERVICE 级后执行——后端已注册，
 *          顺序由初始化级别保证）并建周期输出线程。
 *
 *          演示模块：OM_LOG_MODULE 宏每 TU 一次不敷用——手写三个 OmLogModule 静态
 *          实例经公开 om_log_log 调用（moduleId=-1 惰性登记，与宏生成实例同路径）；
 *          模块自身档全设 DEBUG：模块级 gate 恒放行，演示只保留后端过滤一条轴：
 *            [key]       关键进度——每周期 INFO。覆盖放宽（→ INFO）后应收；
 *            [heartbeat] 心跳——每周期 INFO。覆盖 OFF 显式拒（全级不收）；
 *            [warn]      常规告警——每周期 WARN。无覆盖，走后端默认档。
 *
 *          观测预期（串口按行内 [模块] 标签核对）：
 *            [key] 的 INFO 行每周期一条、序号连续（覆盖放宽生效）；
 *            [warn] 的 WARN 行每周期一条、序号连续（默认档 WARN 通过）；
 *            [heartbeat] 任何级别不出（OFF 全级拒收——含配置末一条 OFF×FATAL
 *              探针：覆盖失效时该行会出现在串口，正常演示恒缺席，供失效自检）。
 *          序号语义：三条编号行序号互相独立、各自从 0 起（每模块独立计数器）；
 *          banner/登记触发行等一次性行无序号，不参与序号连续性核对。
 *
 *          惰性登记前置：模块首次打日志才入库，此前按名调覆盖 API 返回
 *          OM_ERR_NOT_FOUND——配置前各模块先打一条 DEBUG 触发行：低于默认档 WARN
 *          亦低于放宽后的 key 覆盖级（INFO），任何过滤状态下均不产出可见行
 *          （登记确定性不依赖消费 drain 时序）。
 *
 *          观测方式：接收端串口工具参数化（COM 口/波特率不硬编码于 sample——
 *          板级日志口选择与接线见 bsp_serial.h）；消息带序号供接收端完整性分析。
 *          周期档位（编译期宏，配合接收端核对）：
 *            LOG_DEMO_PERIOD_MS 输出周期 ms（默认 500）
 *          构建例：xmake f --cxflags="-DLOG_DEMO_PERIOD_MS=50"
 */

#include "core/om_init.h" /* OM_INIT_DRIVER / OM_INIT_APPLICATION */
#include "drivers/peripheral/serial/log_serial_backend.h"
#include "osal/osal.h"
#include "services/log/log.h"

#include "bsp_serial.h" /* BSP_LOG_SERIAL_NAME：板级日志口选择 */

static LogSerialBackend g_log_serial_backend;

/* 输出周期档位（编译期宏；默认 500ms） */
#ifndef LOG_DEMO_PERIOD_MS
#define LOG_DEMO_PERIOD_MS 500
#endif

/* 组合层可裁剪：OM_USE_LOG=0 时 log 公共函数声明消失——含其调用的逻辑须整段随开关
 * 走（类型无条件保留——backend 实例声明才可留在 guard 外） */
#if OM_USE_LOG

/* 演示模块实例：手写静态实例 + 公开 om_log_log 调用（模块自身档全设 DEBUG——
 * 模块级 gate 恒放行，过滤语义全部落在后端覆盖表上，见文件头） */
static OmLogModule g_mod_key = {"key", OM_LOG_LEVEL_DEBUG, -1};
static OmLogModule g_mod_hb = {"heartbeat", OM_LOG_LEVEL_DEBUG, -1};
static OmLogModule g_mod_warn = {"warn", OM_LOG_LEVEL_DEBUG, -1};

/** @brief 接线：device_find 板级日志口 → 注册后端（DRIVER 级，调度器前；默认级 WARN）
 *  @return OM_OK 成功；失败传播（open/注册错误码） */
static OmRet log_port_init(void)
{
    return om_log_serial_backend_register(&g_log_serial_backend,
                                          device_find((char *)BSP_LOG_SERIAL_NAME), "serial", OM_LOG_LEVEL_WARN);
}
OM_INIT_DRIVER(log_port_init);

/** @brief 周期输出线程：key INFO / heartbeat INFO / warn WARN 各一条（各自序号递增）
 *  @param arg 未使用 */
static void mod_filter_cycle(void *arg)
{
    int n_key = 0;
    int n_hb = 0;
    int n_warn = 0;
    (void)arg;
    for (;;)
    {
        om_log_log(&g_mod_key, OM_LOG_LEVEL_INFO, "key %d", n_key++);
        om_log_log(&g_mod_hb, OM_LOG_LEVEL_INFO, "hb %d", n_hb++);
        om_log_log(&g_mod_warn, OM_LOG_LEVEL_WARN, "warn %d", n_warn++);
        osal_sleep_ms(LOG_DEMO_PERIOD_MS);
    }
}

/** @brief 覆盖配置 + 周期线程启动（APPLICATION 级：SERVICE 级后——后端已注册）
 *  @return OM_OK 成功；覆盖配置失败传播错误码（已打一条 ERROR 上报，不打乱主演示） */
static OmRet mod_filter_setup(void)
{
    OmRet ret;
    OmLogLevel eff = OM_LOG_LEVEL_OFF; /* 失败路径不写 eff——ERROR 行打印前先有确定值 */

    /* 惰性登记触发行：DEBUG 低于默认档 WARN 与放宽覆盖级 INFO——任何过滤状态下
     * 均不可见（登记先行，否则按名覆盖 API 返回 OM_ERR_NOT_FOUND） */
    om_log_log(&g_mod_key, OM_LOG_LEVEL_DEBUG, "reg key");
    om_log_log(&g_mod_hb, OM_LOG_LEVEL_DEBUG, "reg heartbeat");
    om_log_log(&g_mod_warn, OM_LOG_LEVEL_DEBUG, "reg warn");

    /* 覆盖配置：key 放宽至 INFO（默认档 WARN 应收不到 INFO）；heartbeat 显式拒
     * OFF（全级）；warn 无覆盖——默认档 WARN */
    ret = om_log_backend_set_module_level("serial", "key", OM_LOG_LEVEL_INFO);
    if (ret != OM_OK)
    {
        om_log_log(&g_mod_warn, OM_LOG_LEVEL_ERROR, "mod filter cfg fail: set key %d", (int)ret);
        return ret;
    }
    ret = om_log_backend_set_module_level("serial", "heartbeat", OM_LOG_LEVEL_OFF);
    if (ret != OM_OK)
    {
        om_log_log(&g_mod_warn, OM_LOG_LEVEL_ERROR, "mod filter cfg fail: set heartbeat %d", (int)ret);
        return ret;
    }

    /* 生效级复核：key 覆盖应已写入（覆盖表生效校验） */
    ret = om_log_backend_get_module_level("serial", "key", &eff);
    if (ret != OM_OK || eff != OM_LOG_LEVEL_INFO)
    {
        om_log_log(&g_mod_warn, OM_LOG_LEVEL_ERROR, "mod filter cfg fail: get key %d eff %d", (int)ret,
                   (int)eff);
        return (ret != OM_OK) ? ret : OM_ERR_CONFLICT;
    }

    /* OFF×FATAL 探针：heartbeat 覆盖 OFF 后连 FATAL 也不出（显式拒全级，含 FATAL）；
     * 覆盖失效时本行会出现在串口——供失效自检，正常演示恒缺席 */
    om_log_log(&g_mod_hb, OM_LOG_LEVEL_FATAL, "hb off-probe FATAL: must never show");

    /* 启动 banner（key 覆盖已生效——INFO 行可见；语义标注供串口核对） */
    om_log_log(&g_mod_key, OM_LOG_LEVEL_INFO,
               "'serial' default=WARN; key=INFO relaxed; heartbeat=OFF rejected; warn=default");
    om_log_log(&g_mod_key, OM_LOG_LEVEL_INFO,
               "expect per-cycle: [key] INFO & [warn] WARN with seq; [heartbeat] silent");

    OsalThread *thread = NULL;
    OsalThreadAttr attr = {"mod_filter", 1024U, OSAL_PRIO_LOW_BASE};
    (void)osal_thread_create(&thread, &attr, mod_filter_cycle, NULL);
    return OM_OK;
}
OM_INIT_APPLICATION(mod_filter_setup);

#endif /* OM_USE_LOG */

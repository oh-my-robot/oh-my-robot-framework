/**
 * @file main.c
 * @brief 串口日志后端接线示范（组合层：drivers 工厂 + 板级日志口 + log 服务）
 * @details OM_INIT_DRIVER 注册后端（调度器前，早于 SERVICE 级业务日志——DRIVER 级
 *          之前（EARLIEST/BOARD）的日志入消息环滞留，服务就绪后回放（"deferred"回归
 *          形态，契约见 services/log/README.md）；OM_INIT_APPLICATION 建心跳线程
 *          持续输出（带序号，供接收端完整性分析）。
 *
 *          压力测试档位（编译期宏，配合接收端分析脚本 stress_analyze.ps1）：
 *            LOG_STRESS_LEN       1=短~40B  2=中~200B  3=长~1KB（默认 1）
 *            LOG_STRESS_PERIOD_MS 消息间隔 ms（默认 500）
 *          构建例：xmake f --cxflags="-DLOG_STRESS_LEN=2 -DLOG_STRESS_PERIOD_MS=5"
 */

#include "core/om_init.h" /* OM_INIT_DRIVER / OM_INIT_APPLICATION */
#include "drivers/peripheral/serial/log_serial_backend.h"
#include "osal/osal.h"
#include "services/log/log.h"

#include "bsp_serial.h" /* BSP_LOG_SERIAL_NAME：板级日志口选择 */

static LogSerialBackend g_log_serial_backend;

OM_LOG_MODULE(log_serial, OM_LOG_LEVEL_INFO);

#if OM_USE_LOG /* 组合层可裁剪（组合逻辑随开关消失） */
/** @brief 接线：device_find 板级日志口 → 注册后端（DRIVER 级，调度器前）
 *  @return OM_OK 成功；失败传播（open/注册错误码） */
static OmRet log_port_init(void)
{
    return om_log_serial_backend_register(&g_log_serial_backend,
                                          device_find((char *)BSP_LOG_SERIAL_NAME), "serial", OM_LOG_LEVEL_INFO);
}
OM_INIT_DRIVER(log_port_init);
#endif /* OM_USE_LOG */

/* 压力档位（编译期宏；默认短消息 + 500ms 心跳） */
#ifndef LOG_STRESS_LEN
#define LOG_STRESS_LEN 1
#endif
#ifndef LOG_STRESS_PERIOD_MS
#define LOG_STRESS_PERIOD_MS 500
#endif

/* 长消息负载（~1KB 数据字段，%s 一次性提交） */
static const char g_long_payload[] =
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";

/** @brief 心跳日志线程：带序号持续输出（压力档位由编译期宏选择）
 *  @param arg 未使用 */
static void log_heartbeat(void *arg)
{
    int i = 0;
    (void)arg;
    for (;;)
    {
#if (LOG_STRESS_LEN == 1)
        OM_LOG_INFO("hb %d", i++);
#elif (LOG_STRESS_LEN == 2)
        OM_LOG_INFO("hb %d payload=0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF", i++);
#else
        OM_LOG_INFO("hb %d payload=%s", i++, g_long_payload);
#endif
        osal_sleep_ms(LOG_STRESS_PERIOD_MS);
    }
}

/** @brief 样例日志（APPLICATION 级：业务就绪后打样 + 建心跳线程）
 *  @return OM_OK */
static OmRet log_demo(void)
{
    OsalThread *thread = NULL;
    OsalThreadAttr attr = {"log_hb", 1024U, OSAL_PRIO_LOW_BASE};
    OM_LOG_INFO("log serial backend demo: %d", 42);
    OM_LOG_ERROR("error path demo");
    (void)osal_thread_create(&thread, &attr, log_heartbeat, NULL);
    return OM_OK;
}
OM_INIT_APPLICATION(log_demo);

/** @brief 消息环滞留示范：EARLIEST 级——调度器前、后端未注册时打日志
 *  @return OM_OK
 *  @note 此级日志无后端接受：入消息环滞留，服务就绪（SERVICE 级）后回放
 *        （生产时刻时间戳/顺序保留——见服务 README 投递形态节） */
static OmRet early_log_demo(void)
{
    OM_LOG_INFO("early demo: pre-scheduler log");
    return OM_OK;
}
OM_INIT_EARLIEST(early_log_demo);

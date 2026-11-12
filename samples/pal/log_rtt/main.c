/**
 * @file main.c
 * @brief RTT 日志后端零接线示范（宏配置驱动——组合层无注册代码）
 * @details 默认后端由 OM_LOG_RTT=1（om_config.h/om_appcfg.h）驱动隐藏注册（OM_INIT_DRIVER，
 *          早于业务日志——EARLIEST 级日志入消息环滞留（无后端无消费者），服务就绪后
 *          回放（发起时间戳保留），契约见 services/log/README.md）；OM_INIT_APPLICATION
 *          建心跳线程持续输出（带序号，供接收侧完整性分析）。
 *          观测：J-Link RTT Viewer / RTTClient 经调试接口读目标 RAM——零引脚、零串口。
 *
 *          压测档位（编译期宏，配合接收端分析脚本——RTT 无波特率，"速率"=接口时钟）：
 *            OM_LOG_RTT=1           内置默认后端开关（本示范必须）
 *            LOG_STRESS_LEN         1=短~40B  2=中~200B  3=长~1KB（默认 1）
 *            RTT_STRESS_PERIOD_MS   消息间隔 ms（默认 500）
 *          构建例：xmake f --cxflags="-DOM_LOG_RTT=1 -DLOG_STRESS_LEN=2 -DRTT_STRESS_PERIOD_MS=5"
 */

#include "core/om_init.h" /* OM_INIT_APPLICATION */
#include "osal/osal.h"
#include "services/log/log.h"

OM_LOG_MODULE(log_rtt, OM_LOG_LEVEL_INFO);

/* 压测档位（编译期宏；默认短消息 + 500ms 心跳） */
#ifndef LOG_STRESS_LEN
#define LOG_STRESS_LEN 1
#endif
#ifndef RTT_STRESS_PERIOD_MS
#define RTT_STRESS_PERIOD_MS 500
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

/** @brief 心跳日志线程：带序号持续输出（压测档位由编译期宏选择）
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
        osal_sleep_ms(RTT_STRESS_PERIOD_MS);
    }
}

/** @brief 样例日志（APPLICATION 级：业务就绪后打样 + 建心跳线程）
 *  @return OM_OK */
static OmRet log_demo(void)
{
    OsalThread *thread = NULL;
    OsalThreadAttr attr = {"log_hb", 1024U, OSAL_PRIO_LOW_BASE};
    OM_LOG_INFO("log rtt backend demo: %d", 42);
    OM_LOG_ERROR("error path demo");
    (void)osal_thread_create(&thread, &attr, log_heartbeat, NULL);
    return OM_OK;
}
OM_INIT_APPLICATION(log_demo);

/** @brief 消息环滞留示范：EARLIEST 级——调度器前、后端未注册时打日志
 *  @return OM_OK
 *  @note 此级日志无后端接受：入消息环滞留，服务就绪（SERVICE 级）后回放
 *        （生产时刻时间戳/顺序保留——"deferred"回归形态；见服务 README 投递形态节） */
static OmRet early_log_demo(void)
{
    OM_LOG_INFO("early demo: pre-scheduler log");
    return OM_OK;
}
OM_INIT_EARLIEST(early_log_demo);

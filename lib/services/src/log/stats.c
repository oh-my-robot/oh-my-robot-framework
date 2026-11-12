/**
 * @file stats.c
 * @brief log 统计查询（丢弃计数汇总——om_log_stats 事实源）
 * @details 汇总两个丢弃点的跨文件访问器：参数包超限（msg.c g_dropped_overflow）+
 *          异步队列满（log_async.c s_dropped_queue，仅 OM_LOG_ASYNC 存在；最小配置/
 *          兜底下调用点 #ifdef 包裹——队列项恒 0）。丢弃可观测（printk 语义）见
 *          services/log/log.h。
 */

#include "core/om_config.h"

#if OM_USE_LOG

#include "core/om_def.h"

#include "log_internal.h"
#include "services/log/log.h"

/** @brief 读取日志统计：dropped = 参数包超限丢弃 + 消息环满丢弃（累计计数）
 *  @param stats 输出（NULL → OM_ERR_INVALID_ARG）
 *  @return OM_OK 成功；OM_ERR_INVALID_ARG 参数非法 */
OmRet om_log_stats(OmLogStats *stats)
{
    if (stats == NULL)
    {
        return OM_ERR_INVALID_ARG;
    }
    stats->dropped = log_dropped_overflow();
    stats->dropped += log_service_dropped();
    return OM_OK;
}

#endif /* OM_USE_LOG */

/**
 * @file om_log_osal_stub.c
 * @brief host OSAL 桩：单调时钟（恒 0——头部时间戳固定 "00:00:00.000"，断言可预测）
 * @note 仅为 host 测试满足链接；目标侧由 platform/osal 提供真实实现
 *       （osal_time_now_monotonic = 系统 tick 单调计数，线程/ISR 双安全）
 */

#include "osal/osal_time.h"

OsalTimeMs osal_time_now_monotonic(void)
{
    return 0U;
}

/**
 * @file osal_time.h
 * @brief OSAL 时间原语接口（32 位毫秒合同）
 * @details
 * - 对外统一使用 `OsalTimeMs`（uint32_t）表示单调毫秒时基。
 * - 时间值允许自然回绕；比较必须使用回绕安全规则。
 */
#ifndef OM_OSAL_TIME_H
#define OM_OSAL_TIME_H

#include <stdint.h>

#include "osal_core.h"

/**
 * @brief OSAL 时间类型（毫秒，32 位）
 * @details
 * - 表示单调时基上的毫秒计数。
 * - 允许自然回绕（`0xFFFFFFFF -> 0x00000000`）。
 */
typedef uint32_t OsalTimeMs;

/**
 * @brief 秒到毫秒的换算常量（1000）
 */
#define OSAL_MS_PER_SEC 1000ULL
/**
 * @brief 毫秒到微秒的换算常量（1000）
 */
#define OSAL_US_PER_MS 1000ULL
/**
 * @brief 毫秒到纳秒的换算常量（1000000）
 */
#define OSAL_NS_PER_MS 1000000ULL
/**
 * @brief 秒到纳秒的换算常量（1000000000）
 */
#define OSAL_NS_PER_SEC 1000000000ULL

/**
 * @brief 向上取整的 us -> ms 转换
 * @param us 输入微秒值
 * @return 转换后的毫秒值（向上取整，`uint32_t`）
 * @note 调用方应保证结果可由 `uint32_t` 表示；超出范围时按 C 转换规则截断。
 */
static inline uint32_t osal_time_us_to_ms_ceil(uint64_t us)
{
    return (uint32_t)((us + OSAL_US_PER_MS - 1ULL) / OSAL_US_PER_MS);
}

/**
 * @brief 向上取整的 ns -> ms 转换
 * @param ns 输入纳秒值
 * @return 转换后的毫秒值（向上取整，`uint32_t`）
 * @note 调用方应保证结果可由 `uint32_t` 表示；超出范围时按 C 转换规则截断。
 */
static inline uint32_t osal_time_ns_to_ms_ceil(uint64_t ns)
{
    return (uint32_t)((ns + OSAL_NS_PER_MS - 1ULL) / OSAL_NS_PER_MS);
}

/**
 * @brief 回绕安全“晚于”判断
 * @param lhs 待比较时间点 A（毫秒）
 * @param rhs 待比较时间点 B（毫秒）
 * @return `1` 表示 `lhs` 晚于 `rhs`；`0` 表示否则
 * @note 仅在两时刻差值小于 2^31 ms 时有定义（约 24.8 天）
 */
static inline int osal_time_after(OsalTimeMs lhs, OsalTimeMs rhs)
{
    return ((int32_t)(lhs - rhs) > 0) ? 1 : 0;
}

/**
 * @brief 回绕安全“早于”判断
 * @param lhs 待比较时间点 A（毫秒）
 * @param rhs 待比较时间点 B（毫秒）
 * @return `1` 表示 `lhs` 早于 `rhs`；`0` 表示否则
 * @note 仅在两时刻差值小于 2^31 ms 时有定义（约 24.8 天）
 */
static inline int osal_time_before(OsalTimeMs lhs, OsalTimeMs rhs)
{
    return ((int32_t)(lhs - rhs) < 0) ? 1 : 0;
}

/**
 * @brief 获取系统单调时钟（毫秒，32 位）
 * @return 当前单调时钟毫秒值（`OsalTimeMs`）
 * @note 返回值会自然回绕；比较必须使用 `osal_time_after/before`。
 */
OsalTimeMs osal_time_now_monotonic(void);

/**
 * @brief 线程休眠指定毫秒
 * @param sleep_ms 休眠时长（毫秒）
 * @return `OSAL_OK` 成功；`OSAL_INVALID` 参数或上下文非法
 * @note 仅允许在线程上下文调用。
 * @note `sleep_ms == OSAL_WAIT_FOREVER` 非法，应使用等待原语实现无限等待
 */
OsalStatus osal_sleep_ms(OsalTimeMs sleep_ms);

/**
 * @brief 周期延时（保持固定周期，过期追赶）
 * @param deadline_cursor_ms 周期游标（毫秒，in/out）
 * @details
 * - 首次调用传入 `*deadline_cursor_ms = 0`，接口内部初始化为 `now + period_ms`。
 * - 后续调用应原样回传该变量，不需要手动计算“下一次唤醒时间”。
 * - 每次调用返回前，接口内部都会将游标推进一个周期（`+period_ms`）。
 * @param period_ms 周期（ms））
 * @param missed_periods 输出本次调用前已过期的周期数量，可为 `NULL`
 * @return `OSAL_OK` 成功；`OSAL_INVALID` 参数或上下文非法
 * @note 仅允许在线程上下文调用。
 * @note 比较语义遵循 32 位回绕安全窗口（< 2^31 ms）。
 * @note 本接口每次只推进一个周期，不主动跨越多个周期。
 */
OsalStatus osal_delay_until(OsalTimeMs* deadline_cursor_ms, OsalTimeMs period_ms, uint32_t* missed_periods);

#endif


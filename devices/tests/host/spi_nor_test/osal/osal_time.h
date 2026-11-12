/**
 * @file   osal_time.h
 * @brief  OSAL time host 裁剪头（spi_nor_test 用，签名与 lib 版一致）
 */

#ifndef OM_OSAL_TIME_H
#define OM_OSAL_TIME_H

#include <stdint.h>

typedef uint32_t OsalTimeMs;

/** @brief 单调毫秒时基（可自然回绕，比较用 osal_time_after/before——host 桩从简） */
OsalTimeMs osal_time_now_monotonic(void);

/** @brief 睡眠指定毫秒（实现见 host_osal.c；片内测试依赖它验证等待路径让出） */
void osal_sleep_ms(uint32_t ms);

#endif /* OM_OSAL_TIME_H */

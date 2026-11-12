/**
 * @file osal_time.h
 * @brief om_log_test host 桩（本地目录 shadow 框架 lib/osal/include 同名头）
 * @details core.c 经 `#include "osal/osal_time.h"` 取时间戳来源的原型——真实头链
 *          需 OS 端口 om_osal_portdef.h（platform/osal/<os>/），host 不可包含，
 *          故提供最小同签名桩：OsalTimeMs + osal_time_now_monotonic 声明，
 *          实现见 om_log_osal_stub.c（恒 0——时间戳头部 "00:00:00.000"，断言可预测）
 * @note 仅该头与实现桩；目标侧构建不含本目录，取真实 osal/osal_time.h
 */
#ifndef OM_HOST_OSAL_TIME_H
#define OM_HOST_OSAL_TIME_H

#include <stdint.h>

typedef uint32_t OsalTimeMs;

OsalTimeMs osal_time_now_monotonic(void);

#endif

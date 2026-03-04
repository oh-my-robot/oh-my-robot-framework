#ifndef OM_OSAL_EVENT_H
#define OM_OSAL_EVENT_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalEventFlagsHandle_s OsalEventFlags;

/*
 * event_flags 可用业务位掩码（稳定合同）：
 * - 对外接口统一使用 uint32_t
 * - 由端口层通过构建系统注入 OM_OSAL_EVENT_FLAGS_USABLE_MASK
 * - OSAL 公共头不做端口分支与默认兜底，避免平台语义泄漏
 */
#ifndef OM_OSAL_EVENT_FLAGS_USABLE_MASK
#error "OM_OSAL_EVENT_FLAGS_USABLE_MASK is not defined. It must be injected by the active OSAL port."
#endif

#define OSAL_EVENT_FLAGS_USABLE_MASK OM_OSAL_EVENT_FLAGS_USABLE_MASK

/* osal_event_flags_wait options */
#define OSAL_EVENT_FLAGS_WAIT_ALL (1u << 0) /* wait_all，否则 wait_any */
#define OSAL_EVENT_FLAGS_NO_CLEAR (1u << 1) /* 不清除位，否则 wait 成功后清除 wait_mask */

/**
 * @brief 创建事件标志组（线程上下文）
 * @param event_flags 输出事件标志组句柄
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 禁止ISR 中调用
 */
OsalStatus osal_event_flags_create(OsalEventFlags** event_flags);

/**
 * @brief 删除事件标志组（线程上下文）
 * @param event_flags 事件标志组句柄
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID`
 * @note 禁止ISR 中调用
 * @note 严格前置条件：调用方需确保无并发访问且无等待者
 */
OsalStatus osal_event_flags_delete(OsalEventFlags* event_flags);

/**
 * @brief 设置事件位（线程上下文）
 * @param event_flags 事件标志组句柄
 * @param flags 待设置事件位掩码，必须满足 `(flags & ~OSAL_EVENT_FLAGS_USABLE_MASK) == 0`
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID`
 * @note 禁止ISR 中调用
 */
OsalStatus osal_event_flags_set(OsalEventFlags* event_flags, uint32_t flags);

/**
 * @brief 设置事件位（中断上下文）
 * @param event_flags 事件标志组句柄
 * @param flags 待设置事件位掩码，必须满足 `(flags & ~OSAL_EVENT_FLAGS_USABLE_MASK) == 0`
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 仅允许在 ISR 中调用；在线程上下文调用返回 `OSAL_INVALID`
 */
OsalStatus osal_event_flags_set_from_isr(OsalEventFlags* event_flags, uint32_t flags);

/**
 * @brief 清除事件位（线程上下文）
 * @param event_flags 事件标志组句柄
 * @param flags 待清除事件位掩码，必须满足 `(flags & ~OSAL_EVENT_FLAGS_USABLE_MASK) == 0`
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID`
 * @note 禁止ISR 中调用
 */
OsalStatus osal_event_flags_clear(OsalEventFlags* event_flags, uint32_t flags);

/**
 * @brief 等待事件标志（线程上下文）
 * @param event_flags 事件标志句柄
 * @param wait_mask 等待的位掩码，必须非 0 且满`(wait_mask & ~OSAL_EVENT_FLAGS_USABLE_MASK) == 0`
 * @param out_value 输出值（可为 NULL）；若非 NULL，仅返回 `OSAL_EVENT_FLAGS_USABLE_MASK` 范围内的位
 * @param timeout_ms 超时时间（ms），可用 `OSAL_WAIT_FOREVER`
 * @param options 等待选项（`OSAL_EVENT_FLAGS_*`）
 * @return `OSAL_OK` 成功；失败返`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID`
 * @note 禁止ISR 中调用
 */
OsalStatus osal_event_flags_wait(OsalEventFlags* event_flags, uint32_t wait_mask, uint32_t* out_value,
                                    uint32_t timeout_ms, uint32_t options);

#endif

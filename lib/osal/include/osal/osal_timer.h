#ifndef OM_OSAL_TIMER_H
#define OM_OSAL_TIMER_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalTimerHandle_s* OsalTimer_t;
typedef void (*OsalTimerCallback_t)(OsalTimer_t timer);
typedef enum
{
    OSAL_TIMER_ONE_SHOT = 0u, /* 单次定时器*/
    OSAL_TIMER_PERIODIC = 1u, /* 周期定时器*/
} OsalTimerMode_e;

/**
 * @brief 创建软件定时器（线程上下文）
 * @param out_timer 输出定时器句柄（必须非 NULL）
 * @param name 定时器名称，可为 NULL（由端口决定默认命名）
 * @param period_ms 周期（ms）
 * @param mode 定时器模式（单次/周期）
 * @param user_id 用户自定义指针
 * @param cb 回调函数
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 禁止ISR 中调用
 * @note 回调函数必须保持非阻塞与短执行；建议仅做通知投递
 */
OsalStatus_t osal_timer_create(OsalTimer_t* out_timer, const char* name, uint32_t period_ms, OsalTimerMode_e mode,
                                void* user_id, OsalTimerCallback_t cb);

/**
 * @brief 启动定时器（线程上下文）
 * @param timer 定时器句柄
 * @param timeout_ms 超时时间（ms），可用 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 成功；失败返`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID`
 * @note 禁止ISR 中调用
 */
OsalStatus_t osal_timer_start(OsalTimer_t timer, uint32_t timeout_ms);

/**
 * @brief 停止定时器（线程上下文）
 * @param timer 定时器句柄
 * @param timeout_ms 超时时间（ms），可用 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 成功；失败返`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID`
 * @note 禁止ISR 中调用
 */
OsalStatus_t osal_timer_stop(OsalTimer_t timer, uint32_t timeout_ms);

/**
 * @brief 重置定时器（线程上下文）
 * @param timer 定时器句柄
 * @param timeout_ms 超时时间（ms），可用 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 成功；失败返`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID`
 * @note 禁止ISR 中调用
 * @note reset 语义为“重装”：以当前时刻重新计时；未运行时等价 start
 */
OsalStatus_t osal_timer_reset(OsalTimer_t timer, uint32_t timeout_ms);

/**
 * @brief 删除定时器（线程上下文）
 * @param timer 定时器句柄
 * @param timeout_ms 超时时间（ms），可用 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 成功；失败返`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID`
 * @note 禁止ISR 中调用
 * @note 严格前置条件：调用方需确保无并发访问且无并发回调
 */
OsalStatus_t osal_timer_delete(OsalTimer_t timer, uint32_t timeout_ms);

/**
 * @brief 获取用户指针（线程上下文）
 * @param timer 定时器句柄
 * @return 用户指针；参数非法或上下文误用时返回 NULL
 * @note 禁止ISR 中调用
 */
void* osal_timer_get_id(OsalTimer_t timer);

/**
 * @brief 设置用户指针（线程上下文）
 * @param timer 定时器句柄
 * @param id 用户指针
 * @note 禁止ISR 中调用
 * @note 参数非法或上下文误用时安全返回，不产生副作用
 */
void osal_timer_set_id(OsalTimer_t timer, void* id);

#endif

#ifndef OM_OSAL_QUEUE_H
#define OM_OSAL_QUEUE_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalQueueHandle_s OsalQueue;

/**
 * @brief 创建消息队列（线程上下文）
 * @param queue 输出队列句柄
 * @param length 队列长度（元素个数）
 * @param item_size 单个元素大小（字节）
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 禁止ISR 中调用
 */
OsalStatus osal_queue_create(OsalQueue** queue, uint32_t length, uint32_t item_size);

/**
 * @brief 删除消息队列（线程上下文）
 * @param queue 队列句柄
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID`
 * @note 禁止ISR 中调用
 * @note 严格前置条件：调用方需确保无并发访问且无等待者无并发生产消费
 */
OsalStatus osal_queue_delete(OsalQueue* queue);

/**
 * @brief 发消息（线程上下文）
 * @param queue 队列句柄
 * @param item 数据指针
 * @param timeout_ms 超时时间（ms），可用 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 成功；失败返`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID/OSAL_INTERNAL`
 */
OsalStatus osal_queue_send(OsalQueue* queue, const void* item, uint32_t timeout_ms);

/**
 * @brief 发消息（中断上下文）
 * @param queue 队列句柄
 * @param item 数据指针
 * @return `OSAL_OK` 成功；失败返`OSAL_WOULD_BLOCK/OSAL_INVALID/OSAL_INTERNAL`
 * @note 仅允许在 ISR 中调用；在线程上下文调用返回 `OSAL_INVALID`
 */
OsalStatus osal_queue_send_from_isr(OsalQueue* queue, const void* item);

/**
 * @brief 接收消息（线程上下文）
 * @param queue 队列句柄
 * @param item 输出数据指针
 * @param timeout_ms 超时时间（ms），可用 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 成功；失败返`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID/OSAL_INTERNAL`
 */
OsalStatus osal_queue_recv(OsalQueue* queue, void* item, uint32_t timeout_ms);

/**
 * @brief 接收消息（中断上下文）
 * @param queue 队列句柄
 * @param item 输出数据指针
 * @return `OSAL_OK` 成功；失败返`OSAL_WOULD_BLOCK/OSAL_INVALID/OSAL_INTERNAL`
 * @note 仅允许在 ISR 中调用；在线程上下文调用返回 `OSAL_INVALID`
 */
OsalStatus osal_queue_recv_from_isr(OsalQueue* queue, void* item);

/**
 * @brief 查看消息（线程上下文，不出队）
 * @param queue 队列句柄
 * @param item 输出数据指针
 * @param timeout_ms 超时时间（ms），可用 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 成功；失败返`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID/OSAL_INTERNAL`
 */
OsalStatus osal_queue_peek(OsalQueue* queue, void* item, uint32_t timeout_ms);

/**
 * @brief 查看消息（中断上下文，不出队）
 * @param queue 队列句柄
 * @param item 输出数据指针
 * @return `OSAL_OK` 成功；失败返`OSAL_WOULD_BLOCK/OSAL_INVALID/OSAL_INTERNAL`
 * @note 仅允许在 ISR 中调用；在线程上下文调用返回 `OSAL_INVALID`
 */
OsalStatus osal_queue_peek_from_isr(OsalQueue* queue, void* item);

/**
 * @brief 复位队列（清空）
 * @param queue 队列句柄
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID/OSAL_INTERNAL`
 * @note 禁止ISR 中调用
 */
OsalStatus osal_queue_reset(OsalQueue* queue);

/**
 * @brief 获取队列中消息数
 * @param queue 队列句柄
 * @param out_count 输出当前消息数量
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID/OSAL_INTERNAL`
 * @note 线程/ISR 上下文均可调用
 */
OsalStatus osal_queue_messages_waiting(OsalQueue* queue, uint32_t* out_count);

/**
 * @brief 获取队列剩余空间数量
 * @param queue 队列句柄
 * @param out_count 输出剩余可用槽位
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID/OSAL_INTERNAL`
 * @note 线程/ISR 上下文均可调用
 */
OsalStatus osal_queue_spaces_available(OsalQueue* queue, uint32_t* out_count);

#endif

#ifndef OM_IPC_PIPE_H
#define OM_IPC_PIPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/data_struct/ringbuffer.h"
#include "core/om_def.h"
#include "osal/osal_sem.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 单向点对点字节流管道（SPSC）
 *
 * 基于 Ringbuf + 双二值信号量的门铃模式实现：
 * - read_sem：写者 post 通知读者数据到达（pipe 从空→非空时触发）
 * - write_sem：读者 post 通知写者空间可用（pipe 从满→非满时触发）
 *
 * 并发约束：严格 SPSC，禁止多写者或多读者同时操作。
 * 适用场景：Task↔Task、ISR→Task、Task→ISR 的字节流传输。
 */
typedef struct Pipe {
    Ringbuf  rb;        /* 底层环形缓冲区 */
    OsalSem* read_sem;  /* 可读通知信号量（max=1, init=0） */
    OsalSem* write_sem; /* 可写通知信号量（max=1, init=1） */
} Pipe;

/**
 * @brief 使用静态缓冲区初始化 Pipe
 * @param pipe   Pipe 实例指针
 * @param buf    外部提供的缓冲区，大小须 >= capacity 字节
 * @param capacity 缓冲区容量，必须为 2 的幂且 > 0
 * @return `OM_OK` 成功；`OM_ERROR_PARAM` 参数非法；`OM_ERROR_MEMORY` 信号量创建失败
 * @note 禁止在 ISR 中调用
 */
OmRet pipe_init(Pipe* pipe, uint8_t* buf, unsigned int capacity);

/**
 * @brief 动态分配缓冲区创建 Pipe
 * @param pipe     Pipe 实例指针
 * @param capacity 缓冲区容量，必须为 2 的幂且 > 0
 * @param pmalloc  内存分配函数，传 NULL 时回退到标准库 malloc
 * @return `OM_OK` 成功；`OM_ERROR_PARAM` 参数非法；`OM_ERROR_MEMORY` 分配或信号量创建失败
 * @note 禁止在 ISR 中调用
 */
OmRet pipe_alloc(Pipe* pipe, unsigned int capacity, void* (*pmalloc)(size_t));

/**
 * @brief 销毁使用 pipe_init 创建的 Pipe
 * @param pipe Pipe 实例指针
 * @note 禁止在 ISR 中调用
 * @note 调用方需确保无并发访问和无等待者
 */
void pipe_deinit(Pipe* pipe);

/**
 * @brief 销毁使用 pipe_alloc 创建的 Pipe 并释放缓冲区
 * @param pipe  Pipe 实例指针
 * @param pfree 内存释放函数，传 NULL 时回退到标准库 free
 * @note 禁止在 ISR 中调用
 * @note 调用方需确保无并发访问和无等待者
 */
void pipe_free(Pipe* pipe, void (*pfree)(void*));

/**
 * @brief 向 Pipe 写入字节数据（阻塞超时）
 * @param pipe       Pipe 实例指针
 * @param data       待写入数据指针
 * @param len        待写入字节数
 * @param timeout_ms 超时时间（ms），0 表示非阻塞，OSAL_WAIT_FOREVER 表示无限等待
 * @return >= 0 实际写入字节数；< 0 错误码（OM_ERROR_WOULD_BLOCK / OM_ERROR_TIMEOUT / OM_ERROR_PARAM）
 * @note 禁止在 ISR 中调用，请使用 pipe_write_from_isr
 * @note SPSC：同一时刻仅允许一个写者
 */
int pipe_write(Pipe* pipe, const void* data, int len, uint32_t timeout_ms);

/**
 * @brief 向 Pipe 写入字节数据（ISR 安全，非阻塞）
 * @param pipe Pipe 实例指针
 * @param data 待写入数据指针
 * @param len  待写入字节数
 * @return >= 0 实际写入字节数；< 0 错误码（OM_ERROR_WOULD_BLOCK / OM_ERROR_PARAM）
 * @note 仅允许在 ISR 中调用
 * @note SPSC：同一时刻仅允许一个写者
 */
int pipe_write_from_isr(Pipe* pipe, const void* data, int len);

/**
 * @brief 从 Pipe 读取字节数据（消费式，阻塞超时）
 * @param pipe       Pipe 实例指针
 * @param buf        输出缓冲区
 * @param len        期望读取的最大字节数
 * @param timeout_ms 超时时间（ms），0 表示非阻塞，OSAL_WAIT_FOREVER 表示无限等待
 * @return >= 0 实际读取字节数；< 0 错误码（OM_ERROR_WOULD_BLOCK / OM_ERROR_TIMEOUT / OM_ERROR_PARAM）
 * @note 禁止在 ISR 中调用
 * @note SPSC：同一时刻仅允许一个读者
 */
int pipe_read(Pipe* pipe, void* buf, int len, uint32_t timeout_ms);

/**
 * @brief 从 Pipe 窥视字节数据（非消费式，阻塞超时）
 * @param pipe       Pipe 实例指针
 * @param buf        输出缓冲区
 * @param len        期望窥视的最大字节数
 * @param timeout_ms 超时时间（ms），0 表示非阻塞，OSAL_WAIT_FOREVER 表示无限等待
 * @return >= 0 实际窥视字节数；< 0 错误码（OM_ERROR_WOULD_BLOCK / OM_ERROR_TIMEOUT / OM_ERROR_PARAM）
 * @note 禁止在 ISR 中调用
 * @note 窥视后数据保留在 Pipe 中，需配合 pipe_skip 消费
 */
int pipe_peek(Pipe* pipe, void* buf, int len, uint32_t timeout_ms);

/**
 * @brief 跳过 Pipe 中的数据（移动读指针，不拷贝）
 * @param pipe Pipe 实例指针
 * @param len  期望跳过的最大字节数
 * @return `OM_OK` 至少跳过了 1 字节；`OM_ERROR_EMPTY` Pipe 为空；`OM_ERROR_PARAM` 参数非法
 * @note 禁止在 ISR 中调用，请使用 pipe_skip_from_isr
 */
OmRet pipe_skip(Pipe* pipe, int len);

/**
 * @brief 从 Pipe 读取字节数据（ISR 安全，非阻塞，消费式）
 * @param pipe Pipe 实例指针
 * @param buf  输出缓冲区
 * @param len  期望读取的最大字节数
 * @return >= 0 实际读取字节数；< 0 错误码（OM_ERROR_WOULD_BLOCK / OM_ERROR_PARAM）
 * @note 仅允许在 ISR 中调用
 * @note 非阻塞：Pipe 为空时返回 `OM_ERROR_WOULD_BLOCK`
 * @note 读取后若 Pipe 从满变为非满，通过 `osal_sem_post_from_isr` 通知写端
 * @note SPSC：同一时刻仅允许一个读者
 */
int pipe_read_from_isr(Pipe* pipe, void* buf, int len);

/**
 * @brief 从 Pipe 窥视字节数据（ISR 安全，非阻塞，非消费式）
 * @param pipe Pipe 实例指针
 * @param buf  输出缓冲区
 * @param len  期望窥视的最大字节数
 * @return >= 0 实际窥视字节数；< 0 错误码（OM_ERROR_WOULD_BLOCK / OM_ERROR_PARAM）
 * @note 仅允许在 ISR 中调用
 * @note 非阻塞：Pipe 为空时返回 `OM_ERROR_WOULD_BLOCK`
 * @note 窥视后数据保留在 Pipe 中，需配合 pipe_skip_from_isr 消费
 * @note SPSC：同一时刻仅允许一个读者
 */
int pipe_peek_from_isr(Pipe* pipe, void* buf, int len);

/**
 * @brief 跳过 Pipe 中的数据（ISR 安全，非阻塞，移动读指针不拷贝）
 * @param pipe Pipe 实例指针
 * @param len  期望跳过的最大字节数
 * @return `OM_OK` 至少跳过了 1 字节；`OM_ERROR_EMPTY` Pipe 为空；`OM_ERROR_PARAM` 参数非法
 * @note 仅允许在 ISR 中调用
 * @note 跳过成功后若 Pipe 从满变为非满，通过 `osal_sem_post_from_isr` 通知写端
 * @note SPSC：同一时刻仅允许一个读者
 */
OmRet pipe_skip_from_isr(Pipe* pipe, int len);

/* ---- 状态查询（inline，零系统调用开销） ---- */

static inline int pipe_len(Pipe* pipe)
{
    return (int)ringbuf_len(&pipe->rb);
}

static inline int pipe_avail(Pipe* pipe)
{
    return (int)ringbuf_avail(&pipe->rb);
}

static inline int pipe_cap(Pipe* pipe)
{
    return (int)ringbuf_cap(&pipe->rb);
}

static inline bool pipe_is_empty(Pipe* pipe)
{
    return ringbuf_is_empty(&pipe->rb);
}

static inline bool pipe_is_full(Pipe* pipe)
{
    return ringbuf_is_full(&pipe->rb);
}

#ifdef __cplusplus
}
#endif

#endif /* OM_IPC_PIPE_H */

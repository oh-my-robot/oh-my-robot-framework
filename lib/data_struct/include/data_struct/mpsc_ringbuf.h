/**
 * @file mpsc_ringbuf.h
 * @brief 多生产者单消费者无锁环形缓冲区
 * @details
 * 基于 CAS + Per-slot Ready Flag 实现的 MPSC 定长元素环形队列。
 * - 多个生产者通过 CAS 原子预留写入槽位
 * - 单消费者按顺序消费，通过 ready flag 保证只读取已完成写入的数据
 * - 不依赖 OSAL，纯原子操作，可在任意上下文（线程/ISR）中使用
 */

#ifndef MPSC_RINGBUF_H
#define MPSC_RINGBUF_H

#include "core/atomic/atomic_simple.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MPSCRB_NOT_READY 0 /* 槽位数据尚未写入完毕 */
#define MPSCRB_READY     1 /* 槽位数据已写入完毕，可被消费者读取 */

typedef OM_ATOMIC_T(unsigned char) OmAtomicU8;

/**
 * @brief MPSC 环形缓冲区
 *
 * 多生产者单消费者定长元素环形队列。
 * - writePos：多生产者通过 CAS（Acquire/Release）竞争更新
 * - readPos：单消费者独占更新（Release）
 * - ready：per-slot 就绪标志，生产者写入数据后置位，消费者消费后清除
 *
 * 并发约束：多写者、单读者。写者之间通过 CAS 互斥，读者独占 readPos。
 * 适用场景：ISR→Task、Task→Task、ISR+Task→Task 的定长消息投递。
 */
typedef struct MpscRingbuf {
    unsigned char *buf;    /* 数据缓冲区 */
    OmAtomicUint writePos; /* 写位置（单调递增，多生产者 CAS 竞争） */
    OmAtomicUint readPos;  /* 读位置（单调递增，单消费者独占） */
    OmAtomicU8 *ready;     /* per-slot 就绪标志数组（1 字节/slot） */
    unsigned int mask;     /* capacity - 1，capacity 必须为 2 的幂 */
    unsigned int esize;    /* 元素大小（字节） */
} MpscRingbuf;

/**
 * @brief 使用静态缓冲区初始化 MPSC 环形缓冲区
 * @param rb         MpscRingbuf 实例指针
 * @param buf        外部提供的数据缓冲区，大小须 >= item_size * item_count 字节
 * @param ready      外部提供的就绪标志数组，大小须 >= item_count 个 OmAtomicU8
 * @param item_size  单个元素大小（字节）
 * @param item_count 槽位数量，必须为 2 的幂
 * @return `true` 成功；`false` 参数非法
 * @note item_count 非 2 的幂时会触发死循环断言
 */
bool mpscrb_init(MpscRingbuf *rb, uint8_t *buf, OmAtomicU8 *ready, unsigned int item_size,
                 unsigned int item_count);

/**
 * @brief 动态分配缓冲区创建 MPSC 环形缓冲区
 * @param rb         MpscRingbuf 实例指针
 * @param item_size  单个元素大小（字节）
 * @param item_count 槽位数量（自动向上取整到最近的 2 的幂）
 * @param pmalloc    内存分配函数，传 NULL 时回退到标准库 malloc
 * @return `true` 成功；`false` 参数非法或分配失败
 * @note 数据缓冲区和就绪标志数组合并为一次连续分配
 */
bool mpscrb_alloc(MpscRingbuf *rb, unsigned int item_size, unsigned int item_count,
                  void *(*pmalloc)(size_t));

/**
 * @brief 销毁使用 mpscrb_alloc 创建的 MPSC 环形缓冲区并释放缓冲区
 * @param rb    MpscRingbuf 实例指针
 * @param pfree 内存释放函数，传 NULL 时回退到标准库 free
 * @note 调用方需确保无并发访问
 */
void mpscrb_free(MpscRingbuf *rb, void (*pfree)(void *));

/**
 * @brief 生产者写入一个元素（CAS + Ready Flag）
 * @param rb   MpscRingbuf 实例指针
 * @param data 待写入数据指针，大小须 == esize 字节
 * @return `true` 写入成功；`false` 队列已满或参数非法
 * @note 可在任意上下文调用（线程 / ISR）
 * @note MPSC：同一时刻允许多个生产者并发写入
 */
bool mpscrb_in(MpscRingbuf *rb, const void *data);

/**
 * @brief 消费者读取并消费一个元素
 * @param rb  MpscRingbuf 实例指针
 * @param buf 输出缓冲区，大小须 >= esize 字节
 * @return `true` 读取成功；`false` 队列为空或队首槽位尚未就绪
 * @note SPSC 消费端：同一时刻仅允许一个消费者
 * @note 队首槽位已预留但数据尚未写入完毕时返回 `false`（延迟可见）
 */
bool mpscrb_out(MpscRingbuf *rb, void *buf);

/**
 * @brief 消费者窥视一个元素（非消费式）
 * @param rb  MpscRingbuf 实例指针
 * @param buf 输出缓冲区，大小须 >= esize 字节
 * @return `true` 窥视成功；`false` 队列为空或队首槽位尚未就绪
 * @note 窥视后数据保留在队列中，需配合 mpscrb_out 消费
 * @note SPSC 消费端：同一时刻仅允许一个消费者
 */
bool mpscrb_out_peek(MpscRingbuf *rb, void *buf);

/* ---- 状态查询（inline，零系统调用开销） ---- */

/**
 * @brief 获取队列容量
 * @param rb MpscRingbuf 实例指针
 * @return 槽位总数
 */
static inline unsigned int mpscrb_cap(MpscRingbuf *const rb)
{
    return rb->mask + 1U;
}

/**
 * @brief 获取已预留的槽位数量
 * @param rb MpscRingbuf 实例指针
 * @return 已预留数量（含尚未就绪的槽位，是可读元素数的上界）
 */
static inline unsigned int mpscrb_len(MpscRingbuf *const rb)
{
    unsigned int write_pos = OM_LOAD_ACQ(&rb->writePos);
    unsigned int read_pos  = OM_LOAD_ACQ(&rb->readPos);
    return write_pos - read_pos;
}

/**
 * @brief 获取剩余空闲槽位数量
 * @param rb MpscRingbuf 实例指针
 * @return 可用槽位数
 */
static inline unsigned int mpscrb_avail(MpscRingbuf *const rb)
{
    return mpscrb_cap(rb) - mpscrb_len(rb);
}

/**
 * @brief 判断队列是否为空
 * @param rb MpscRingbuf 实例指针
 * @return `true` 为空；`false` 不为空
 */
static inline bool mpscrb_is_empty(MpscRingbuf *const rb)
{
    unsigned int write_pos = OM_LOAD_ACQ(&rb->writePos);
    unsigned int read_pos  = OM_LOAD_ACQ(&rb->readPos);
    return write_pos == read_pos;
}

/**
 * @brief 判断队列是否已满
 * @param rb MpscRingbuf 实例指针
 * @return `true` 已满；`false` 未满
 */
static inline bool mpscrb_is_full(MpscRingbuf *const rb)
{
    return mpscrb_len(rb) > rb->mask;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MPSC_RINGBUF_H */

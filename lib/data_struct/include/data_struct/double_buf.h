#ifndef DATA_STRUCT_DOUBLE_BUF_H
#define DATA_STRUCT_DOUBLE_BUF_H

#include "atomic/atomic_simple.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief 双缓冲区（Ping-Pong Buffer）
 *
 * 将一块连续内存（或两块独立内存）从逻辑上分割为两个等大的 page，
 * 分别称为 Page 0（低地址）和 Page 1（高地址）。
 *
 * 角色由 idx 决定：
 *   - 写 page = page[idx]  （生产者写入）
 *   - 读 page = page[idx^1]（消费者读取）
 *
 * 生产者调用 commit() 翻转 idx，使得刚写完的 page 对消费者可见。
 * 生产者永不阻塞——若消费者未及时消费旧帧，旧帧被静默丢弃，
 * drop_cnt 递增供诊断。
 *
 * 适用场景：DMA 双缓冲、串口/USB CDC 收帧、Sensor 数据采集等
 * SPSC（单生产者/单消费者）场景。
 *
 * @note  双缓冲不是队列。若需无丢数据的流式传输，请使用 Ringbuf 或 Pipe。
 *
 * 典型用法（零拷贝——DMA / ISR）：
 * @code
 *   uint8_t mem[512];
 *   DoubleBuf db;
 *   dbuf_init(&db, mem, 256);
 *
 *   // 生产者（ISR）
 *   uint8_t *buf = dbuf_get_write_ptr(&db);
 *   buf[pos++] = uart_read_byte();
 *   dbuf_commit(&db, pos);
 *
 *   // 消费者（任务）
 *   size_t len;
 *   uint8_t *data = dbuf_get_read_ptr(&db, &len);
 *   if (data) { process(data, len); dbuf_consume(&db); }
 * @endcode
 *
 * 典型用法（拷贝——简单场景）：
 * @code
 *   // 生产者
 *   dbuf_write(&db, src_data, src_len);
 *
 *   // 消费者
 *   uint8_t buf[256];
 *   size_t n = dbuf_read(&db, buf, sizeof(buf));
 *   if (n) process(buf, n);
 * @endcode
 *
 * 与其他模块对比：
 *   - Ringbuf   : 流式 FIFO， 不丢数据，需拷贝进出，容量 N
 *   - Pipe      : Ringbuf + 信号量，阻塞式流式传输
 *   - DoubleBuf : 帧式，零拷贝，最新覆盖，容量恒为 2
 */
typedef struct DoubleBuf
{
    uint8_t     *page[2];    /**< page[0]: Page 0（低地址），page[1]: Page 1（高地址） */
    size_t       page_size;  /**< 单个 page 的字节大小 */
    OmAtomicUint idx;        /**< 当前写 page 索引 (0 或 1)；读 page 索引为 idx ^ 1 */
    size_t       len[2];     /**< len[0]: Page 0 有效数据, len[1]: Page 1 有效数据 */
    OmAtomicUint drop_cnt;   /**< 被覆盖丢弃的帧计数 */
    bool         owned;      /**< 内存是否由 dbuf_alloc 动态分配 */
} DoubleBuf;

/*===========================================================================
 * 初始化 / 销毁
 *===========================================================================*/

/**
 * @brief 用一块连续内存初始化双缓冲区（主接口）
 * @param db        双缓冲区对象指针
 * @param mem       连续内存块，大小 >= 2 * page_size
 * @param page_size 单个 page 的字节大小（> 0）
 * @return true 成功，false 参数非法
 */
bool dbuf_init(DoubleBuf *db, void *mem, size_t page_size);

/**
 * @brief 用两块独立内存初始化双缓冲区
 * @param db        双缓冲区对象指针
 * @param page0     Page 0 内存，大小 >= page_size
 * @param page1     Page 1 内存，大小 >= page_size
 * @param page_size 单个 page 的字节大小（> 0）
 * @return true 成功，false 参数非法
 */
bool dbuf_init_split(DoubleBuf *db, void *page0, void *page1, size_t page_size);

/**
 * @brief 动态分配双缓冲区（内部分配 2 * page_size 连续内存）
 * @note  嵌入式堆易碎片化，优先使用 dbuf_init（静态/栈内存）。
 * @param db        双缓冲区对象指针
 * @param page_size 单个 page 的字节大小（> 0）
 * @param pmalloc   自定义分配函数，传 NULL 使用 malloc
 * @return true 成功，false 内存不足或参数非法
 */
bool dbuf_alloc(DoubleBuf *db, size_t page_size, void *(*pmalloc)(size_t));

/**
 * @brief 释放动态分配的内存（仅 dbuf_alloc 创建的有效，split/init 模式无操作）
 * @param db    双缓冲区对象指针
 * @param pfree 自定义释放函数，传 NULL 使用 free
 */
void dbuf_free(DoubleBuf *db, void (*pfree)(void *));

/*===========================================================================
 * 生产者接口（写入侧）
 *===========================================================================*/

/**
 * @brief 获取写 page 的指针（始终成功，永不阻塞）
 * @param db 双缓冲区对象指针
 * @return 写 page 起始地址，db 无效时返回 NULL
 */
uint8_t *dbuf_get_write_ptr(DoubleBuf *db);

/**
 * @brief 获取写 page 的容量
 * @param db 双缓冲区对象指针
 * @return page_size
 */
static inline size_t dbuf_get_write_size(DoubleBuf *db)
{
    return db->page_size;
}

/**
 * @brief 提交写入的数据并交换 page 角色
 * @param db  双缓冲区对象指针
 * @param len 写入的有效数据长度（超过 page_size 时自动截断）
 */
void dbuf_commit(DoubleBuf *db, size_t len);

/**
 * @brief 仅交换 page 角色（适用于 DMA 等由硬件管理数据长度的场景）
 * @param db 双缓冲区对象指针
 */
void dbuf_swap(DoubleBuf *db);

/*===========================================================================
 * 生产者接口（写入侧）——便捷拷贝
 *===========================================================================*/

/**
 * @brief 将数据拷贝写入双缓冲区（便捷接口）
 *
 * 等价于 get_write_ptr + memcpy + commit。
 * DMA / ISR 等需要零拷贝的场景请使用 get_write_ptr + commit。
 *
 * @param db   双缓冲区对象指针
 * @param data 源数据指针
 * @param len  数据长度（超过 page_size 时自动截断）
 */
static inline void dbuf_write(DoubleBuf *db, const void *data, size_t len)
{
    size_t n = (len < db->page_size) ? len : db->page_size;
    memcpy(dbuf_get_write_ptr(db), data, n);
    dbuf_commit(db, n);
}

/*===========================================================================
 * 消费者接口（读取侧）——零拷贝
 *===========================================================================*/

/**
 * @brief 获取读 page 的指针及有效数据长度
 * @param db      双缓冲区对象指针
 * @param out_len 输出参数，接收有效数据长度（可传 NULL）
 * @return 读 page 起始地址，无数据则返回 NULL
 */
uint8_t *dbuf_get_read_ptr(DoubleBuf *db, size_t *out_len);

/**
 * @brief 标记读 page 中的数据已消费完毕
 * @param db 双缓冲区对象指针
 */
void dbuf_consume(DoubleBuf *db);

/*===========================================================================
 * 消费者接口（读取侧）——便捷拷贝
 *===========================================================================*/

/**
 * @brief 从双缓冲区拷贝读取数据（便捷接口）
 *
 * 等价于 get_read_ptr + memcpy + consume。
 *
 * @param db      双缓冲区对象指针
 * @param dst     目标缓冲区
 * @param max_len 最大读取长度
 * @return 实际拷贝的字节数，无数据返回 0
 */
static inline size_t dbuf_read(DoubleBuf *db, void *dst, size_t max_len)
{
    size_t len;
    uint8_t *src = dbuf_get_read_ptr(db, &len);
    if (!src)
        return 0U;
    size_t n = (len < max_len) ? len : max_len;
    memcpy(dst, src, n);
    dbuf_consume(db);
    return n;
}

/*===========================================================================
 * 状态查询
 *===========================================================================*/

/**
 * @brief 是否有数据可供读取
 * @param db 双缓冲区对象指针
 * @return true 有数据可读
 */
bool dbuf_is_data_ready(DoubleBuf *db);

/**
 * @brief 获取读 page 中的有效数据长度
 * @param db 双缓冲区对象指针
 * @return 有效数据长度（字节），无数据返回 0
 */
size_t dbuf_get_read_len(DoubleBuf *db);

/**
 * @brief 检查当前写入是否会触发丢帧
 *
 * 如果写 page 中仍有未消费数据，下一次 get_write_ptr 会丢弃它。
 * 生产者可在写入前用此接口做选择性丢弃决策。
 *
 * @param db 双缓冲区对象指针
 * @return true  写 page 空闲，写入不会丢帧
 *         false 写 page 中有未消费数据，此时写入将导致丢帧
 */
bool dbuf_is_write_ready(DoubleBuf *db);

/**
 * @brief 获取单个 page 的容量
 * @param db 双缓冲区对象指针
 * @return page_size
 */
static inline size_t dbuf_capacity(DoubleBuf *db)
{
    return db->page_size;
}

/**
 * @brief 获取被静默丢弃的帧计数
 * @param db 双缓冲区对象指针
 * @return 累计丢弃帧数
 */
static inline uint32_t dbuf_drop_count(DoubleBuf *db)
{
    return (uint32_t)OM_LOAD_RLX(&db->drop_cnt);
}

/*===========================================================================
 * 管理接口
 *===========================================================================*/

/**
 * @brief 重置双缓冲区到初始状态（丢弃所有未消费数据）
 * @param db 双缓冲区对象指针
 */
void dbuf_flush(DoubleBuf *db);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DATA_STRUCT_DOUBLE_BUF_H */

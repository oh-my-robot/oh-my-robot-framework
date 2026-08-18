/**
 * @file mpsc_ringbuf.c
 * @brief MPSC 环形缓冲区实现
 */

#include "data_struct/mpsc_ringbuf.h"
#include <stdlib.h>
#include <string.h>

#define IS_NUM_POWER_OF_TWO(x) ((x) && !(((x) & ((x)-1U))))

static unsigned int mpscrb_roundup_pow_of_two(unsigned int v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

/**
 * @brief 环绕写入一个元素到缓冲区
 * @param rb  MpscRingbuf 实例指针
 * @param src 源数据指针
 * @param off 绝对偏移（writePos），内部通过 & mask 映射到环形位置
 */
static void mpscrb_copy_in(MpscRingbuf *rb, const void *src, unsigned int off)
{
    unsigned int size  = rb->mask + 1U;
    unsigned int esize = rb->esize;
    unsigned int copy_len;

    off &= rb->mask;
    if (esize != 1U) {
        off *= esize;
        size *= esize;
    }

    copy_len = (esize < (size - off)) ? esize : (size - off);
    memcpy(rb->buf + off, src, copy_len);
    memcpy(rb->buf, (const unsigned char *)src + copy_len, esize - copy_len);
}

/**
 * @brief 环绕读取一个元素从缓冲区
 * @param rb  MpscRingbuf 实例指针
 * @param dst 目标缓冲区指针
 * @param off 绝对偏移（readPos），内部通过 & mask 映射到环形位置
 */
static void mpscrb_copy_out(MpscRingbuf *rb, void *dst, unsigned int off)
{
    unsigned int size  = rb->mask + 1U;
    unsigned int esize = rb->esize;
    unsigned int copy_len;

    off &= rb->mask;
    if (esize != 1U) {
        off *= esize;
        size *= esize;
    }

    copy_len = (esize < (size - off)) ? esize : (size - off);
    memcpy(dst, rb->buf + off, copy_len);
    memcpy((unsigned char *)dst + copy_len, rb->buf, esize - copy_len);
}

bool mpscrb_init(MpscRingbuf *rb, uint8_t *buf, OmAtomicU8 *ready, unsigned int item_size,
                 unsigned int item_count)
{
    if ((rb == NULL) || (buf == NULL) || (ready == NULL) || (item_size == 0U) || (item_count == 0U))
        return false;

    if (!IS_NUM_POWER_OF_TWO(item_count)) {
        while (1) {
        }
    }

    rb->buf   = buf;
    rb->ready = ready;
    rb->esize = item_size;
    rb->mask  = item_count - 1U;
    OM_STORE_RLX(&rb->writePos, 0U);
    OM_STORE_RLX(&rb->readPos, 0U);

    memset(ready, MPSCRB_NOT_READY, item_count * sizeof(OmAtomicU8));
    return true;
}

bool mpscrb_alloc(MpscRingbuf *rb, unsigned int item_size, unsigned int item_count,
                  void *(*pmalloc)(size_t))
{
    unsigned int cap;
    size_t data_sz;
    size_t ready_sz;

    if ((rb == NULL) || (item_size == 0U) || (item_count == 0U))
        return false;

    cap      = mpscrb_roundup_pow_of_two(item_count);
    data_sz  = (size_t)cap * item_size;
    ready_sz = (size_t)cap * sizeof(OmAtomicU8);

    /* 数据缓冲区和就绪标志数组合并为一次连续分配 */
    if (!pmalloc)
        rb->buf = malloc(data_sz + ready_sz);
    else
        rb->buf = pmalloc(data_sz + ready_sz);

    if (rb->buf == NULL)
        return false;

    rb->ready = (OmAtomicU8 *)(rb->buf + data_sz);

    memset(rb->buf, 0, data_sz);
    memset(rb->ready, MPSCRB_NOT_READY, ready_sz);

    rb->esize = item_size;
    rb->mask  = cap - 1U;
    OM_STORE_RLX(&rb->writePos, 0U);
    OM_STORE_RLX(&rb->readPos, 0U);
    return true;
}

void mpscrb_free(MpscRingbuf *rb, void (*pfree)(void *))
{
    if ((rb == NULL) || (rb->buf == NULL))
        return;

    if (!pfree)
        free(rb->buf);
    else
        pfree(rb->buf);

    rb->buf   = NULL;
    rb->ready = NULL;
}

bool mpscrb_in(MpscRingbuf *rb, const void *data)
{
    unsigned int pos;
    unsigned int read_pos;
    unsigned int free_count;

    if ((rb == NULL) || (data == NULL))
        return false;

    do {
        pos        = OM_LOAD_ACQ(&rb->writePos); /* Acquire：看到其他生产者的最新预留 */
        read_pos   = OM_LOAD_ACQ(&rb->readPos);  /* Acquire：看到消费者释放的最新进度 */
        free_count = (rb->mask + 1U) - (pos - read_pos);
        if (free_count == 0U)
            return false;
    } while (!OM_CAS_AR(&rb->writePos, &pos, pos + 1U));
    /* CAS 成功：Acq_Rel — 获取其他生产者的写入 + 发布自己的预留 */

    mpscrb_copy_in(rb, data, pos);
    OM_STORE_REL(&rb->ready[pos & rb->mask], MPSCRB_READY); /* Release：确保 memcpy 在 ready 之前对消费者可见 */
    return true;
}

bool mpscrb_out(MpscRingbuf *rb, void *buf)
{
    unsigned int pos;

    if ((rb == NULL) || (buf == NULL))
        return false;

    pos = OM_LOAD_RLX(&rb->readPos);       /* Relaxed：只有自己写 readPos */
    if (pos >= OM_LOAD_ACQ(&rb->writePos)) /* Acquire：看到生产者的最新预留 */
        return false;

    if (OM_LOAD_ACQ(&rb->ready[pos & rb->mask]) != MPSCRB_READY) /* Acquire：配合生产者的 Release */
        return false;

    mpscrb_copy_out(rb, buf, pos);
    OM_STORE_RLX(&rb->ready[pos & rb->mask], MPSCRB_NOT_READY); /* Relaxed：生产者不读此标志 */
    OM_STORE_REL(&rb->readPos, pos + 1U);                       /* Release：发布消费进度给生产者 */
    return true;
}

bool mpscrb_out_peek(MpscRingbuf *rb, void *buf)
{
    unsigned int pos;

    if ((rb == NULL) || (buf == NULL))
        return false;

    pos = OM_LOAD_RLX(&rb->readPos);
    if (pos >= OM_LOAD_ACQ(&rb->writePos))
        return false;

    if (OM_LOAD_ACQ(&rb->ready[pos & rb->mask]) != MPSCRB_READY)
        return false;

    mpscrb_copy_out(rb, buf, pos);
    /* 不推进 readPos，不清除 ready */
    return true;
}

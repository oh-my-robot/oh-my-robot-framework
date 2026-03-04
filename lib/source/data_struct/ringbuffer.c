#include "core/data_struct/ringbuffer.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define IS_NUM_POWER_OF_TWO(x) ((x) && !(((x) & ((x) - 1U))))

bool ringbuf_init(Ringbuf* rb, uint8_t *buff, unsigned int item_size, unsigned int item_count)
{
    if ((rb == NULL) || (buff == NULL) || (item_size == 0U) || (item_count == 0U))
        return false;

    if (!IS_NUM_POWER_OF_TWO(item_count))
    {
        while (1)
        {
        }
    }

    rb->buf = buff;
    rb->esize = item_size;
    rb->mask = item_count - 1U;
    OM_STORE_RLX(&rb->writePos, 0U);
    OM_STORE_RLX(&rb->readPos, 0U);
    return true;
}

bool ringbuf_alloc(Ringbuf* rb, unsigned int item_size, unsigned int item_count, void *(pmalloc)(size_t))
{
    unsigned int item_buf_size;

    if ((rb == NULL) || (item_size == 0U) || (item_count == 0U))
        return false;

    item_buf_size = roundup_pow_of_two(item_count);
    if (!pmalloc)
        rb->buf = malloc(item_buf_size * item_size);
    else
        rb->buf = pmalloc(item_buf_size * item_size);

    if (rb->buf == NULL)
        return false;

    memset(rb->buf, 0, item_buf_size * item_size);
    rb->esize = item_size;
    rb->mask = item_buf_size - 1U;
    OM_STORE_RLX(&rb->writePos, 0U);
    OM_STORE_RLX(&rb->readPos, 0U);
    return true;
}

void free_ringbuf(Ringbuf* rb, void (*pfree)(void *))
{
    if ((rb == NULL) || (rb->buf == NULL))
        return;

    if (!pfree)
        free(rb->buf);
    else
        pfree(rb->buf);

    rb->buf = NULL;
}

static void ringbuf_copy_in(Ringbuf* rb, const void *src, unsigned int len, unsigned int off)
{
    unsigned int size = rb->mask + 1U;
    unsigned int esize = rb->esize;
    unsigned int copy_len;

    off &= rb->mask;
    if (esize != 1U)
    {
        off *= esize;
        size *= esize;
        len *= esize;
    }

    copy_len = (len < (size - off)) ? len : (size - off);

    memcpy(rb->buf + off, src, copy_len);
    memcpy(rb->buf, (const unsigned char *)src + copy_len, len - copy_len);
}

unsigned int ringbuf_in(Ringbuf* rb, const void *buf, unsigned int item_count)
{
    unsigned int write_pos;
    unsigned int read_pos;
    unsigned int avail_count;

    write_pos = OM_LOAD_RLX(&rb->writePos);
    read_pos = OM_LOAD_ACQ(&rb->readPos);
    avail_count = ringbuf_cap(rb) - (write_pos - read_pos);

    if (item_count > avail_count)
        item_count = avail_count;

    ringbuf_copy_in(rb, buf, item_count, write_pos);

    /* Publish write index after payload is fully written. */
    OM_STORE_REL(&rb->writePos, write_pos + item_count);
    return item_count;
}

static void ringbuf_copy_out(Ringbuf* rb, void *dst, unsigned int len, unsigned int off)
{
    unsigned int size = rb->mask + 1U;
    unsigned int esize = rb->esize;
    unsigned int copy_len;

    off &= rb->mask;
    if (esize != 1U)
    {
        off *= esize;
        size *= esize;
        len *= esize;
    }

    copy_len = (len < (size - off)) ? len : (size - off);

    memcpy(dst, rb->buf + off, copy_len);
    memcpy((unsigned char *)dst + copy_len, rb->buf, len - copy_len);
}

unsigned int ringbuf_out(Ringbuf* rb, void *buf, unsigned int item_count)
{
    unsigned int read_pos;

    read_pos = OM_LOAD_RLX(&rb->readPos);
    item_count = ringbuf_out_peek(rb, buf, item_count);

    /* Publish read index after payload is fully consumed. */
    OM_STORE_REL(&rb->readPos, read_pos + item_count);
    return item_count;
}

unsigned int ringbuf_out_peek(Ringbuf* rb, void *buf, unsigned int len)
{
    unsigned int write_pos;
    unsigned int read_pos;
    unsigned int used_count;

    write_pos = OM_LOAD_ACQ(&rb->writePos);
    read_pos = OM_LOAD_RLX(&rb->readPos);
    used_count = write_pos - read_pos;

    if (len > used_count)
        len = used_count;

    ringbuf_copy_out(rb, buf, len, read_pos);
    return len;
}

unsigned int ringbuf_get_item_linear_space(Ringbuf* rb, void **dest)
{
    unsigned int write_pos;
    unsigned int read_pos;
    unsigned int out_off;
    unsigned int in_off;
    unsigned int linear_size = 0U;

    write_pos = OM_LOAD_ACQ(&rb->writePos);
    read_pos = OM_LOAD_RLX(&rb->readPos);

    if (write_pos == read_pos)
    {
        *dest = NULL;
        return 0U;
    }

    in_off = write_pos & rb->mask;
    out_off = read_pos & rb->mask;
    *dest = &rb->buf[out_off];

    if (out_off < in_off)
        linear_size = in_off - out_off;
    else if ((out_off > in_off) || ((write_pos - read_pos) > rb->mask))
        linear_size = ringbuf_cap(rb) - out_off;

    return linear_size;
}

unsigned int ringbuf_get_avail_linear_size(Ringbuf* rb, void **dest)
{
    unsigned int write_pos;
    unsigned int read_pos;
    unsigned int out_off;
    unsigned int in_off;
    unsigned int linear_size;

    write_pos = OM_LOAD_RLX(&rb->writePos);
    read_pos = OM_LOAD_ACQ(&rb->readPos);

    if ((write_pos - read_pos) > rb->mask)
    {
        *dest = NULL;
        return 0U;
    }

    in_off = write_pos & rb->mask;
    out_off = read_pos & rb->mask;
    *dest = &rb->buf[in_off];

    if (out_off < in_off)
        linear_size = ringbuf_cap(rb) - in_off;
    else if (out_off > in_off)
        linear_size = out_off - in_off;
    else
        linear_size = ringbuf_cap(rb);

    return linear_size;
}

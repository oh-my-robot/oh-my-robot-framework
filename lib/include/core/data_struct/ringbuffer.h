#ifndef RINGBUF_H
#define RINGBUF_H

#include "atomic/atomic_simple.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* CPU memory barriers kept for compatibility with existing callers. */
#define SMP_RMB() om_fence_acq()
#define SMP_WMB() om_fence_rel()

/*
 * Single-producer / single-consumer ring buffer.
 * writePos is only updated by producer.
 * readPos is only updated by consumer.
 */
typedef struct Ringbuf
{
    unsigned char* buf;
    om_atomic_uint_t writePos;
    om_atomic_uint_t readPos;
    unsigned int mask;  /* capacity = mask + 1, capacity must be power-of-two */
    unsigned int esize; /* item size in bytes */
} Ringbuf_s, *Ringbuf_t;

bool ringbuf_alloc(Ringbuf_t rb, unsigned int item_size, unsigned int item_count, void* (pmalloc)(size_t));
bool ringbuf_init(Ringbuf_t rb, uint8_t* buff, unsigned int item_size, unsigned int item_count);
void free_ringbuf(Ringbuf_t rb, void (*pfree)(void*));
unsigned int ringbuf_in(Ringbuf_t rb, const void* buf, unsigned int item_count);
unsigned int ringbuf_out(Ringbuf_t rb, void* buf, unsigned int item_count);
unsigned int ringbuf_out_peek(Ringbuf_t rb, void* buf, unsigned int len);
unsigned int ringbuf_get_item_linear_space(Ringbuf_t rb, void** dest);
unsigned int ringbuf_get_avail_linear_size(Ringbuf_t rb, void** dest);

/* Number of used items in the ring buffer. */
static inline unsigned int ringbuf_len(const Ringbuf_t rb)
{
    unsigned int write_pos = OM_LOAD_ACQ(&rb->writePos);
    unsigned int read_pos = OM_LOAD_ACQ(&rb->readPos);
    return write_pos - read_pos;
}

/* Maximum item count in the ring buffer. */
static inline unsigned int ringbuf_cap(const Ringbuf_t rb)
{
    return rb->mask + 1U;
}

/* Remaining free item count. */
static inline unsigned int ringbuf_avail(const Ringbuf_t rb)
{
    return ringbuf_cap(rb) - ringbuf_len(rb);
}

static inline bool ringbuf_is_full(const Ringbuf_t rb)
{
    return ringbuf_len(rb) > rb->mask;
}

static inline bool ringbuf_is_empty(const Ringbuf_t rb)
{
    unsigned int write_pos = OM_LOAD_ACQ(&rb->writePos);
    unsigned int read_pos = OM_LOAD_ACQ(&rb->readPos);
    return write_pos == read_pos;
}

static inline void ringbuf_update_out(Ringbuf_t rb, unsigned int count)
{
    unsigned int write_pos = OM_LOAD_ACQ(&rb->writePos);
    unsigned int read_pos = OM_LOAD_RLX(&rb->readPos);
    unsigned int used_count = write_pos - read_pos;

    if (count > used_count)
        count = used_count;

    OM_STORE_REL(&rb->readPos, read_pos + count);
}

static inline void ringbuf_update_in(Ringbuf_t rb, unsigned int count)
{
    unsigned int write_pos = OM_LOAD_RLX(&rb->writePos);
    unsigned int read_pos = OM_LOAD_ACQ(&rb->readPos);
    unsigned int avail_count = ringbuf_cap(rb) - (write_pos - read_pos);

    if (count > avail_count)
        count = avail_count;

    OM_STORE_REL(&rb->writePos, write_pos + count);
}

static inline unsigned int roundup_pow_of_two(unsigned int v)
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RINGBUF_H */

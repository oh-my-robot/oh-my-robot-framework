#include "ipc/pipe.h"

#include "osal/osal_core.h"
#include <string.h>

static OmRet pipe_create_sems(Pipe* pipe)
{
    if (osal_sem_create(&pipe->read_sem, 1u, 0u) != OSAL_OK)
        return OM_ERROR_MEMORY;

    if (osal_sem_create(&pipe->write_sem, 1u, 1u) != OSAL_OK)
    {
        osal_sem_delete(pipe->read_sem);
        pipe->read_sem = NULL;
        return OM_ERROR_MEMORY;
    }
    return OM_OK;
}

static bool pipe_is_power_of_two(unsigned int v)
{
    return v != 0u && (v & (v - 1u)) == 0u;
}

OmRet pipe_init(Pipe* pipe, uint8_t* buf, unsigned int capacity)
{
    if (!pipe)
        return OM_ERROR_PARAM;

    memset(pipe, 0, sizeof(*pipe));

    if (!ringbuf_init(&pipe->rb, buf, 1u, capacity))
        return OM_ERROR_PARAM;

    OmRet ret = pipe_create_sems(pipe);
    if (ret != OM_OK)
    {
        memset(&pipe->rb, 0, sizeof(pipe->rb));
        return ret;
    }

    return OM_OK;
}

OmRet pipe_alloc(Pipe* pipe, unsigned int capacity, void* (*pmalloc)(size_t))
{
    if (!pipe || !pipe_is_power_of_two(capacity))
        return OM_ERROR_PARAM;

    memset(pipe, 0, sizeof(*pipe));

    if (!ringbuf_alloc(&pipe->rb, 1u, capacity, pmalloc))
        return OM_ERROR_MEMORY;

    OmRet ret = pipe_create_sems(pipe);
    if (ret != OM_OK)
    {
        free_ringbuf(&pipe->rb, NULL);
        memset(&pipe->rb, 0, sizeof(pipe->rb));
        return ret;
    }

    return OM_OK;
}

void pipe_deinit(Pipe* pipe)
{
    if (!pipe)
        return;

    if (pipe->read_sem)
    {
        osal_sem_delete(pipe->read_sem);
        pipe->read_sem = NULL;
    }
    if (pipe->write_sem)
    {
        osal_sem_delete(pipe->write_sem);
        pipe->write_sem = NULL;
    }

    memset(&pipe->rb, 0, sizeof(pipe->rb));
}

void pipe_free(Pipe* pipe, void (*pfree)(void*))
{
    if (!pipe)
        return;

    if (pipe->read_sem)
    {
        osal_sem_delete(pipe->read_sem);
        pipe->read_sem = NULL;
    }
    if (pipe->write_sem)
    {
        osal_sem_delete(pipe->write_sem);
        pipe->write_sem = NULL;
    }

    free_ringbuf(&pipe->rb, pfree);
    memset(&pipe->rb, 0, sizeof(pipe->rb));
}

int pipe_write(Pipe* pipe, const void* data, int len, uint32_t timeout_ms)
{
    if (!pipe || !data || len <= 0)
        return OM_ERROR_PARAM;

    if (osal_is_in_isr())
        return OM_ERROR_PARAM;

    bool was_empty = ringbuf_is_empty(&pipe->rb);
    unsigned int n = ringbuf_in(&pipe->rb, data, (unsigned int)len);

    if (n > 0u)
    {
        if (was_empty)
            osal_sem_post(pipe->read_sem);
        return (int)n;
    }

    if (timeout_ms == 0u)
        return OM_ERROR_WOULD_BLOCK;

    OsalStatus ws = osal_sem_wait(pipe->write_sem, timeout_ms);
    if (ws != OSAL_OK)
        return (ws == OSAL_WOULD_BLOCK) ? OM_ERROR_WOULD_BLOCK : OM_ERROR_TIMEOUT;

    was_empty = ringbuf_is_empty(&pipe->rb);
    n = ringbuf_in(&pipe->rb, data, (unsigned int)len);
    if (n > 0u)
    {
        if (was_empty)
            osal_sem_post(pipe->read_sem);
        return (int)n;
    }

    return OM_ERROR_WOULD_BLOCK;
}

int pipe_write_from_isr(Pipe* pipe, const void* data, int len)
{
    if (!pipe || !data || len <= 0)
        return OM_ERROR_PARAM;

    bool was_empty = ringbuf_is_empty(&pipe->rb);
    unsigned int n = ringbuf_in(&pipe->rb, data, (unsigned int)len);

    if (n > 0u)
    {
        if (was_empty)
            osal_sem_post_from_isr(pipe->read_sem);
        return (int)n;
    }

    return OM_ERROR_WOULD_BLOCK;
}

int pipe_read(Pipe* pipe, void* buf, int len, uint32_t timeout_ms)
{
    if (!pipe || !buf || len <= 0)
        return OM_ERROR_PARAM;

    if (osal_is_in_isr())
        return OM_ERROR_PARAM;

    bool was_full = ringbuf_is_full(&pipe->rb);
    unsigned int n = ringbuf_out(&pipe->rb, buf, (unsigned int)len);

    if (n > 0u)
    {
        if (was_full)
            osal_sem_post(pipe->write_sem);
        return (int)n;
    }

    if (timeout_ms == 0u)
        return OM_ERROR_WOULD_BLOCK;

    OsalStatus ws = osal_sem_wait(pipe->read_sem, timeout_ms);
    if (ws != OSAL_OK)
        return (ws == OSAL_WOULD_BLOCK) ? OM_ERROR_WOULD_BLOCK : OM_ERROR_TIMEOUT;

    was_full = ringbuf_is_full(&pipe->rb);
    n = ringbuf_out(&pipe->rb, buf, (unsigned int)len);
    if (n > 0u)
    {
        if (was_full)
            osal_sem_post(pipe->write_sem);
        return (int)n;
    }

    return OM_ERROR_WOULD_BLOCK;
}

int pipe_peek(Pipe* pipe, void* buf, int len, uint32_t timeout_ms)
{
    if (!pipe || !buf || len <= 0)
        return OM_ERROR_PARAM;

    if (osal_is_in_isr())
        return OM_ERROR_PARAM;

    unsigned int n = ringbuf_out_peek(&pipe->rb, buf, (unsigned int)len);

    if (n > 0u)
        return (int)n;

    if (timeout_ms == 0u)
        return OM_ERROR_WOULD_BLOCK;

    OsalStatus ws = osal_sem_wait(pipe->read_sem, timeout_ms);
    if (ws != OSAL_OK)
        return (ws == OSAL_WOULD_BLOCK) ? OM_ERROR_WOULD_BLOCK : OM_ERROR_TIMEOUT;

    n = ringbuf_out_peek(&pipe->rb, buf, (unsigned int)len);
    if (n > 0u)
        return (int)n;

    return OM_ERROR_WOULD_BLOCK;
}

OmRet pipe_skip(Pipe* pipe, int len)
{
    if (!pipe || len <= 0)
        return OM_ERROR_PARAM;

    if (osal_is_in_isr())
        return OM_ERROR_PARAM;

    if (ringbuf_is_empty(&pipe->rb))
        return OM_ERROR_EMPTY;

    bool was_full = ringbuf_is_full(&pipe->rb);
    unsigned int avail = ringbuf_len(&pipe->rb);
    unsigned int skip = ((unsigned int)len < avail) ? (unsigned int)len : avail;
    ringbuf_update_out(&pipe->rb, skip);

    if (was_full && skip > 0u)
        osal_sem_post(pipe->write_sem);

    return OM_OK;
}

int pipe_read_from_isr(Pipe* pipe, void* buf, int len)
{
    if (!pipe || !buf || len <= 0)
        return OM_ERROR_PARAM;

    bool was_full = ringbuf_is_full(&pipe->rb);
    unsigned int n = ringbuf_out(&pipe->rb, buf, (unsigned int)len);

    if (n > 0u)
    {
        if (was_full)
            osal_sem_post_from_isr(pipe->write_sem);
        return (int)n;
    }

    return OM_ERROR_WOULD_BLOCK;
}

int pipe_peek_from_isr(Pipe* pipe, void* buf, int len)
{
    if (!pipe || !buf || len <= 0)
        return OM_ERROR_PARAM;

    unsigned int n = ringbuf_out_peek(&pipe->rb, buf, (unsigned int)len);

    if (n > 0u)
        return (int)n;

    return OM_ERROR_WOULD_BLOCK;
}

OmRet pipe_skip_from_isr(Pipe* pipe, int len)
{
    if (!pipe || len <= 0)
        return OM_ERROR_PARAM;

    if (ringbuf_is_empty(&pipe->rb))
        return OM_ERROR_EMPTY;

    bool was_full = ringbuf_is_full(&pipe->rb);
    unsigned int avail = ringbuf_len(&pipe->rb);
    unsigned int skip = ((unsigned int)len < avail) ? (unsigned int)len : avail;
    ringbuf_update_out(&pipe->rb, skip);

    if (was_full && skip > 0u)
        osal_sem_post_from_isr(pipe->write_sem);

    return OM_OK;
}

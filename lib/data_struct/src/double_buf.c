#include "data_struct/double_buf.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*===========================================================================
 * 初始化 / 销毁
 *===========================================================================*/

bool dbuf_init(DoubleBuf *db, void *mem, size_t page_size)
{
    if ((db == NULL) || (mem == NULL) || (page_size == 0U))
        return false;

    db->page[0] = (uint8_t *)mem;
    db->page[1] = (uint8_t *)mem + page_size;
    db->page_size = page_size;
    db->len[0] = 0U;
    db->len[1] = 0U;
    db->owned = false;
    OM_STORE_RLX(&db->idx, 0U);
    OM_STORE_RLX(&db->drop_cnt, 0U);

    return true;
}

bool dbuf_init_split(DoubleBuf *db, void *page0, void *page1, size_t page_size)
{
    if ((db == NULL) || (page0 == NULL) || (page1 == NULL) || (page_size == 0U))
        return false;

    db->page[0] = (uint8_t *)page0;
    db->page[1] = (uint8_t *)page1;
    db->owned = false;
    db->page_size = page_size;
    db->len[0] = 0U;
    db->len[1] = 0U;
    OM_STORE_RLX(&db->idx, 0U);
    OM_STORE_RLX(&db->drop_cnt, 0U);

    return true;
}

bool dbuf_alloc(DoubleBuf *db, size_t page_size, void *(*pmalloc)(size_t))
{
    uint8_t *mem;

    if ((db == NULL) || (page_size == 0U))
        return false;

    if (!pmalloc)
        mem = (uint8_t *)malloc(2U * page_size);
    else
        mem = (uint8_t *)pmalloc(2U * page_size);

    if (mem == NULL)
        return false;

    memset(mem, 0, 2U * page_size);

    db->page[0] = mem;
    db->page[1] = mem + page_size;
    db->page_size = page_size;
    db->len[0] = 0U;
    db->len[1] = 0U;
    db->owned = true;
    OM_STORE_RLX(&db->idx, 0U);
    OM_STORE_RLX(&db->drop_cnt, 0U);

    return true;
}

void dbuf_free(DoubleBuf *db, void (*pfree)(void *))
{
    if ((db == NULL) || (!db->owned) || (db->page[0] == NULL))
        return;

    if (!pfree)
        free(db->page[0]);
    else
        pfree(db->page[0]);

    db->page[0] = NULL;
    db->page[1] = NULL;
    db->page_size = 0U;
    db->owned = false;
}

/*===========================================================================
 * 生产者接口
 *===========================================================================*/

uint8_t *dbuf_get_write_ptr(DoubleBuf *db)
{
    unsigned int w_idx;

    if (db == NULL)
        return NULL;

    w_idx = OM_LOAD_ACQ(&db->idx);

    /*
     * 写 page 中如果还有未消费数据，说明消费者没跟上。
     * 在此处（生产者独占的写入侧）清零并计数，避免在 commit 中
     * 与消费者的 get_read_ptr 产生竞争。
     */
    if (db->len[w_idx] != 0U)
    {
        OM_INC_RLX(&db->drop_cnt);
        db->len[w_idx] = 0U;
    }

    return db->page[w_idx];
}

void dbuf_commit(DoubleBuf *db, size_t len)
{
    unsigned int w_idx;

    if (db == NULL)
        return;

    w_idx = OM_LOAD_RLX(&db->idx);

    if (len > db->page_size)
        len = db->page_size;

    db->len[w_idx] = len;

    /* release idx 保证消费者通过 acquire 看到完整的 len[] 更新 */
    OM_STORE_REL(&db->idx, w_idx ^ 1U);
}

void dbuf_swap(DoubleBuf *db)
{
    unsigned int w_idx;

    if (db == NULL)
        return;

    w_idx = OM_LOAD_RLX(&db->idx);
    OM_STORE_REL(&db->idx, w_idx ^ 1U);
}

/*===========================================================================
 * 消费者接口
 *===========================================================================*/

uint8_t *dbuf_get_read_ptr(DoubleBuf *db, size_t *out_len)
{
    unsigned int r_idx;

    if (db == NULL)
        return NULL;

    /* acquire idx 保证看到生产者在 commit 中设置的 len[] */
    r_idx = OM_LOAD_ACQ(&db->idx) ^ 1U;

    if (db->len[r_idx] == 0U)
        return NULL;

    if (out_len)
        *out_len = db->len[r_idx];

    return db->page[r_idx];
}

void dbuf_consume(DoubleBuf *db)
{
    unsigned int r_idx;

    if (db == NULL)
        return;

    r_idx = OM_LOAD_ACQ(&db->idx) ^ 1U;
    db->len[r_idx] = 0U;
}

/*===========================================================================
 * 状态查询
 *===========================================================================*/

bool dbuf_is_data_ready(DoubleBuf *db)
{
    unsigned int r_idx;

    if (db == NULL)
        return false;

    r_idx = OM_LOAD_ACQ(&db->idx) ^ 1U;
    return db->len[r_idx] != 0U;
}

size_t dbuf_get_read_len(DoubleBuf *db)
{
    unsigned int r_idx;

    if (db == NULL)
        return 0U;

    r_idx = OM_LOAD_ACQ(&db->idx) ^ 1U;
    return db->len[r_idx];
}

bool dbuf_is_write_ready(DoubleBuf *db)
{
    unsigned int w_idx;

    if (db == NULL)
        return false;

    w_idx = OM_LOAD_ACQ(&db->idx);
    return db->len[w_idx] == 0U;
}

/*===========================================================================
 * 管理接口
 *===========================================================================*/

void dbuf_flush(DoubleBuf *db)
{
    if (db == NULL)
        return;

    db->len[0] = 0U;
    db->len[1] = 0U;
    OM_STORE_RLX(&db->idx, 0U);
    OM_STORE_RLX(&db->drop_cnt, 0U);
}

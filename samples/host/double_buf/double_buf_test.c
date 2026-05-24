#include "data_struct/double_buf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PAGE_SZ 64

static int test_basic_ping_pong(void)
{
    DoubleBuf db;
    uint8_t mem[2 * PAGE_SZ];
    assert(dbuf_init(&db, mem, PAGE_SZ));

    assert(!dbuf_is_data_ready(&db));
    assert(dbuf_get_read_ptr(&db, NULL) == NULL);

    uint8_t *ptr = dbuf_get_write_ptr(&db);
    memset(ptr, 0xAB, 32);
    dbuf_commit(&db, 32);

    size_t len;
    ptr = dbuf_get_read_ptr(&db, &len);
    assert(ptr && len == 32 && ptr[0] == 0xAB);
    dbuf_consume(&db);
    assert(!dbuf_is_data_ready(&db));

    printf("  [PASS] test_basic_ping_pong\n");
    return 0;
}

static int test_is_write_ready(void)
{
    DoubleBuf db;
    uint8_t mem[2 * PAGE_SZ];
    assert(dbuf_init(&db, mem, PAGE_SZ));

    assert(dbuf_is_write_ready(&db));   /* 初始空闲 */

    /* 正常写入一帧并消费 */
    uint8_t *ptr = dbuf_get_write_ptr(&db);
    memset(ptr, 0xAA, PAGE_SZ);
    dbuf_commit(&db, PAGE_SZ);
    assert(dbuf_is_write_ready(&db));   /* 翻转后新写 page 空闲 */

    /* 再写一帧，不消费 → 两个 page 都有数据 */
    ptr = dbuf_get_write_ptr(&db);
    memset(ptr, 0xBB, PAGE_SZ);
    dbuf_commit(&db, PAGE_SZ);
    assert(!dbuf_is_write_ready(&db));  /* 下一次写入将触发丢弃 */

    /* 消费后仍有旧帧残留在写 page 中，write_ready 仍为 false */
    dbuf_get_read_ptr(&db, NULL);
    dbuf_consume(&db);
    assert(!dbuf_is_write_ready(&db));  /* 写 page 中还残留旧帧 */

    /* get_write_ptr 丢弃旧帧后恢复 */
    dbuf_get_write_ptr(&db);
    assert(dbuf_drop_count(&db) == 1U);
    assert(dbuf_is_write_ready(&db));

    printf("  [PASS] test_is_write_ready\n");
    return 0;
}

static int test_drop_on_get_write_ptr(void)
{
    DoubleBuf db;
    uint8_t mem[2 * PAGE_SZ];
    assert(dbuf_init(&db, mem, PAGE_SZ));

    /* commit 不会触发丢弃 */
    dbuf_commit(&db, PAGE_SZ);
    dbuf_commit(&db, PAGE_SZ);
    assert(dbuf_drop_count(&db) == 0U);

    /* get_write_ptr 发现旧数据 → 触发丢弃 */
    uint8_t *ptr = dbuf_get_write_ptr(&db);
    assert(ptr != NULL);
    assert(dbuf_drop_count(&db) == 1U);

    dbuf_commit(&db, PAGE_SZ);
    assert(dbuf_drop_count(&db) == 1U);

    printf("  [PASS] test_drop_on_get_write_ptr\n");
    return 0;
}

static int test_convenience_api(void)
{
    DoubleBuf db;
    uint8_t mem[2 * PAGE_SZ];
    assert(dbuf_init(&db, mem, PAGE_SZ));

    const char *msg = "hello double buffer";
    dbuf_write(&db, msg, strlen(msg) + 1);

    uint8_t buf[64];
    size_t n = dbuf_read(&db, buf, sizeof(buf));
    assert(n == strlen(msg) + 1);
    assert(strcmp((char *)buf, msg) == 0);
    assert(!dbuf_is_data_ready(&db));

    /* 无数据时 read 返回 0 */
    n = dbuf_read(&db, buf, sizeof(buf));
    assert(n == 0U);

    printf("  [PASS] test_convenience_api\n");
    return 0;
}

static int test_split_and_flush(void)
{
    DoubleBuf db;
    uint8_t p0[PAGE_SZ], p1[PAGE_SZ];
    assert(dbuf_init_split(&db, p0, p1, PAGE_SZ));
    assert(dbuf_capacity(&db) == PAGE_SZ);

    dbuf_commit(&db, 10);
    assert(dbuf_is_data_ready(&db));

    dbuf_flush(&db);
    assert(!dbuf_is_data_ready(&db));
    assert(dbuf_drop_count(&db) == 0U);

    printf("  [PASS] test_split_and_flush\n");
    return 0;
}

static int test_alloc_free(void)
{
    DoubleBuf db;
    assert(dbuf_alloc(&db, 128, NULL));
    assert(dbuf_capacity(&db) == 128);

    dbuf_write(&db, "test", 5);
    uint8_t buf[16];
    assert(dbuf_read(&db, buf, sizeof(buf)) == 5);

    dbuf_free(&db, NULL);
    /* double-free 安全（owned 已置 false） */
    dbuf_free(&db, NULL);

    /* 非 alloc 对象 free 无操作 */
    uint8_t mem[2 * PAGE_SZ];
    assert(dbuf_init(&db, mem, PAGE_SZ));
    dbuf_free(&db, NULL);  /* owned=false，无操作 */
    assert(dbuf_capacity(&db) == PAGE_SZ);

    printf("  [PASS] test_alloc_free\n");
    return 0;
}

int main(void)
{
    printf("DoubleBuf unit tests:\n");
    test_basic_ping_pong();
    test_is_write_ready();
    test_drop_on_get_write_ptr();
    test_convenience_api();
    test_split_and_flush();
    test_alloc_free();
    printf("All tests passed.\n");
    return 0;
}

/**
 * @file   partition_test.c
 * @brief  分区表抽象 host 测试（按名查询/值拷贝隔离/便捷层边界/配置错误显式报错）
 *
 * 夹具：flash_sim 注册 flash0（均匀 256KB、扇区 4KB）+ 本地表定义——
 * 表符号契约（om_partition_table/count 由"板/工程数据"提供）在 host 上
 * 以本地定义实例化验证。器件访问面 = flash_sim（复用同级 flash_dev_test 基础设施）。
 *
 * 退出码 0=全绿；非 0=有 FAIL。
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/model/device.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "drivers/storage/partition.h"
#include "osal/osal_sem.h"

#include "flash_sim.h"

static int g_pass;
static int g_fail;

#define CHECK(cond, ...)         \
    do                           \
    {                            \
        if (cond)                \
        {                        \
            g_pass++;            \
            printf("  PASS: ");  \
            printf(__VA_ARGS__); \
            printf("\n");        \
        }                        \
        else                     \
        {                        \
            g_fail++;            \
            printf("  FAIL: ");  \
            printf(__VA_ARGS__); \
            printf("\n");        \
        }                        \
    } while (0)

/* ===================================================================
 * 夹具：器件 + 表符号（模拟"板/工程 bootcfg 数据"实例）
 * =================================================================== */

#define CAP (256u * 1024u)
#define SECT 4096u

static const FlashGeometry geom_uniform = {
    .capacity = CAP,
    .erasedValue = 0xFF,
    .writeUnit = 4u,
    .pageSize = 256u,
    .sectorSize = SECT,
    .sectorCount = CAP / SECT,
    .sectorRegions = NULL,
};

static const FlashOps sim_ops = {
    .read = flash_sim_read,
    .write = flash_sim_write,
    .erase = flash_sim_erase,
};

static FlashDev g_flash_dev;
static FlashSim g_flash_sim;

/* 表符号（bootcfg 实例的 host 替身）：扇区对齐条目 + 两个刻意错误条目 */
#define P_BOOT "boot"
#define P_APP "app"
#define P_META "meta"
#define P_BAD_ALIGN "bad_align" /* 错误配置 1：大小非扇区对齐 */
#define P_GHOST "ghost"         /* 错误配置 2：器件不存在 */

/* 表实例（模拟板/工程 bootcfg 数据）：const 静态存储期，注册交入模块私有持有。
 * 三表分治：good = 合法对齐表；geom_bad = 非扇区友好条目；cap_bad = 越器件容量 */
static const OmPartitionEntry table_good[] = {
    {P_BOOT, "flash0", 0x00000u, 0x1000u},
    {P_APP, "flash0", 0x01000u, 0x20000u},
    {P_META, "flash0", 0x21000u, 0x1000u},
    {P_GHOST, "no_such_flash", 0x00000u, 0x1000u}, /* 幽灵器件：操作期 NOT_FOUND */
};
static const OmPartitionEntry table_geom_bad[] = {
    {P_APP, "flash0", 0x01000u, 0x20000u},
    {P_BAD_ALIGN, "flash0", 0x22000u, 0x100u}, /* 非 4KB 扇区对齐（size 非整扇区） */
};
static const OmPartitionEntry table_cap_bad[] = {
    {P_APP, "flash0", CAP - 0x1000u, 0x2000u}, /* 越器件容量 */
};
#define G_COUNT (sizeof(table_good) / sizeof(table_good[0]))
#define GB_COUNT (sizeof(table_geom_bad) / sizeof(table_geom_bad[0]))
#define CB_COUNT (sizeof(table_cap_bad) / sizeof(table_cap_bad[0]))

/* ===================================================================
 * T0: 注册防线（未注册空表 / 结构校验 / 顺序解耦跳过 / 几何 fail-fast /
 *      操作期兜底）——flash 器件注册点在此函数内按防线顺序插入
 * =================================================================== */

static void test_register(void)
{
    printf("[T0] registration defenses\n");
    OmPartitionEntry e;

    /* 防线 0：未注册 = 空表语义 */
    CHECK(om_partition_query(P_APP, &e) == OM_ERR_NOT_FOUND,
          "query before register -> NOT_FOUND");
    CHECK(om_partition_erase(P_APP) == OM_ERR_NOT_FOUND, "erase before register -> NOT_FOUND");

    /* 防线 0：结构校验（不依赖器件） */
    CHECK(om_partition_register(NULL, 1u) == OM_ERR_INVALID_ARG, "register NULL table rejected");
    CHECK(om_partition_register(table_good, 0u) == OM_ERR_INVALID_ARG,
          "register zero count rejected");
    static const OmPartitionEntry dup_table[] = {
        {P_APP, "flash0", 0x1000u, 0x2000u},
        {P_APP, "flash0", 0x3000u, 0x2000u}, /* 重名 */
    };
    CHECK(om_partition_register(dup_table, 2u) == OM_ERR_INVALID_ARG, "duplicate names rejected");
    static const OmPartitionEntry bad_field[] = {{NULL, "flash0", 0u, 0x1000u}};
    CHECK(om_partition_register(bad_field, 1u) == OM_ERR_INVALID_ARG, "NULL name rejected");
    static const OmPartitionEntry zero_size[] = {{P_APP, "flash0", 0u, 0u}};
    CHECK(om_partition_register(zero_size, 1u) == OM_ERR_INVALID_ARG, "zero size rejected");

    /* 防线 1：顺序解耦——器件未注册时注册几何坏表 → 跳过几何校验（通过） */
    CHECK(om_partition_register(table_geom_bad, GB_COUNT) == OM_OK,
          "register before device exists (geom check deferred)");

    /* 注册 flash 器件（几何真源就位） */
    CHECK(flash_register(&g_flash_dev, "flash0", &geom_uniform, &sim_ops, &g_flash_sim, NULL) ==
              OM_OK,
          "register sim flash0");

    /* 防线 2：几何 fail-fast——器件在，坏表整表拒绝（旧表保留） */
    CHECK(om_partition_register(table_geom_bad, GB_COUNT) == OM_ERR_INVALID_ARG,
          "misaligned partition table rejected at register (sector-friendly)");
    CHECK(om_partition_register(table_cap_bad, CB_COUNT) == OM_ERR_INVALID_ARG,
          "over-capacity partition rejected at register");

    /* 防线 3：操作期兜底——当前仍为 step 前注册的 geom_bad 表（失败注册不改表），
     * erase 经器件层扇区断言拒绝 */
    CHECK(om_partition_erase(P_BAD_ALIGN) == OM_ERR_INVALID_ARG,
          "misaligned erase rejected by device layer (op-time backstop)");

    /* 合法表注册（几何全过） */
    CHECK(om_partition_register(table_good, G_COUNT) == OM_OK, "register valid table");
}

/* ===================================================================
 * T1: 按名查询（值拷贝语义）
 * =================================================================== */

static void test_query(void)
{
    printf("[T1] query by name (value copy)\n");

    OmPartitionEntry e;
    CHECK(om_partition_query(P_APP, &e) == OM_OK, "query existing partition");
    CHECK(e.offset == 0x01000u && e.size == 0x20000u && strcmp(e.devName, "flash0") == 0,
          "entry fields match table");
    CHECK(om_partition_query("no_such", &e) == OM_ERR_NOT_FOUND, "query miss -> NOT_FOUND");
    CHECK(om_partition_query(NULL, &e) == OM_ERR_INVALID_ARG, "query NULL name rejected");
    CHECK(om_partition_query(P_APP, NULL) == OM_ERR_INVALID_ARG, "query NULL out rejected");

    /* 值拷贝隔离：篡改拷贝不影响表/后续查询 */
    OmPartitionEntry forged;
    om_partition_query(P_APP, &forged);
    forged.offset = 0;
    forged.size = CAP; /* 改到乱七八糟 */
    OmPartitionEntry again;
    om_partition_query(P_APP, &again);
    CHECK(again.offset == 0x01000u && again.size == 0x20000u,
          "forged copy does not affect authoritative table");
}

/* ===================================================================
 * T2: 便捷层边界与内容（name-only 契约）
 * =================================================================== */

static void test_io_boundary(void)
{
    printf("[T2] convenience I/O bounds (name-only)\n");
    static uint8_t buf[512];

    /* 读写环：分区内偏移语义 */
    memset(buf, 0x5A, sizeof(buf));
    CHECK(om_partition_write(P_BOOT, 0u, buf, sizeof(buf)) == OM_OK, "write within partition");
    memset(buf, 0, sizeof(buf));
    CHECK(om_partition_read(P_BOOT, 0u, buf, sizeof(buf)) == OM_OK, "read within partition");
    CHECK(buf[0] == 0x5A && buf[511] == 0x5A, "roundtrip content matches");

    /* 双端越界 */
    CHECK(om_partition_read(P_BOOT, 0x1000u, buf, 1u) == OM_ERR_INVALID_ARG,
          "read at partition end rejected");
    CHECK(om_partition_read(P_BOOT, 0x0FF0u, buf, 0x20u) == OM_ERR_INVALID_ARG,
          "read crossing end rejected");
    CHECK(om_partition_write(P_BOOT, 0x0FFCu, buf, 8u) == OM_ERR_INVALID_ARG,
          "write crossing end rejected");
    CHECK(om_partition_read(P_BOOT, 0u, NULL, 1u) == OM_ERR_INVALID_ARG,
          "read NULL buf rejected");
    CHECK(om_partition_write(P_BOOT, 0u, NULL, 1u) == OM_ERR_INVALID_ARG,
          "write NULL data rejected");
    CHECK(om_partition_read("no_such", 0u, buf, 1u) == OM_ERR_NOT_FOUND,
          "read unknown name rejected");
}

/* ===================================================================
 * T3: 擦除语义 + 配置错误显式报错
 * =================================================================== */

static void test_erase_and_misconfig(void)
{
    printf("[T3] erase + misconfiguration rejected loudly\n");

    /* 整分区擦（对齐条目）→ 内容回擦除值 */
    CHECK(om_partition_erase(P_BOOT) == OM_OK, "erase whole aligned partition");
    static uint8_t probe[16];
    memset(probe, 0x11, sizeof(probe));
    om_partition_read(P_BOOT, 0u, probe, sizeof(probe));
    CHECK(probe[0] == 0xFF, "partition erased to 0xFF");

    /* 幽灵器件（合法表内）：操作显式 NOT_FOUND */
    CHECK(om_partition_read(P_GHOST, 0u, probe, 1u) == OM_ERR_NOT_FOUND,
          "ghost-device partition rejected on access");
}

/* ===================================================================
 * main
 * =================================================================== */

int main(void)
{
    printf("=== partition abstraction host test ===\n");

    flash_sim_init(&g_flash_sim, CAP, 0xFFu, 4u);
    CHECK(g_flash_sim.mem != NULL, "sim memory allocated");

    test_register(); /* 内含按防线顺序的器件注册点 */
    test_query();
    test_io_boundary();
    test_erase_and_misconfig();

    flash_sim_deinit(&g_flash_sim);

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

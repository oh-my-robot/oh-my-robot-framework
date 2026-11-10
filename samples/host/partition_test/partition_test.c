/**
 * @file   partition_test.c
 * @brief  分区表抽象 host 测试 v2（注册表+句柄：查询值拷贝/句柄域校验/多表隔离/
 *         不可用注册表语义/边界 I/O/erase_range/配置错误显式报错）
 *
 * 夹具：flash_sim 注册 flash0（均匀 256KB、扇区 4KB）+ 本地 const 注册表定义——
 * v2 表由调用方持有（可整体 const、免运行期注册），模块零状态；host 上以静态
 * 存储期实例化验证"编译期常量注册表"形态。器件访问面 = flash_sim（复用同级
 * flash_dev_test 基础设施）。
 *
 * 跨用例数据态管线（插入/重排用例须保持）：T2 置脏 boot → T3 整擦复位 →
 * T4/T6 复写 boot；T9 置于末尾（改动 boot/meta，无后继消费者）。flash_sim 开启严格编程（落位 = dst & src，写未擦区返回
 * OM_ERR_FLASH_IO），"擦除→复写"的次序是硬约束；每个用例的置脏数据必须
 * 自备（见 T3 的 (prep) 步骤），不得依赖前序用例的遗留字节。
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
#include "osal/osal_thread.h"

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
 * 夹具：器件 + 注册表（模拟"板/工程 bootcfg 数据"实例）
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

/* 裸后端：只用于"注册期拒绝"的坏器件用例（不进数据通路） */
static const FlashOps sim_ops = {
    .read = flash_sim_read,
    .write = flash_sim_write,
    .erase = flash_sim_erase,
};

/* 后端探针（包 sim_ops）：统计"同时处于后端内的调用数"。分区层自身不加锁，
 * 同一物理片上两个分区的并发访问必须由器件层串行——峰值恒为 1 即其证据 */
static volatile long g_inBackend;
static volatile long g_maxInBackend;

/* 构建固定在 gnu11 方言（见 xmake.lua），故直接用 GCC 原子内建，不引平台头 */
static long exc_atomic_inc(volatile long *p)
{
    return __sync_add_and_fetch(p, 1);
}

static long exc_atomic_dec(volatile long *p)
{
    return __sync_sub_and_fetch(p, 1);
}

static OmRet wrap_read(FlashDev *dev, uint32_t addr, void *buf, size_t len)
{
    long n = exc_atomic_inc(&g_inBackend);
    if (n > g_maxInBackend)
    {
        g_maxInBackend = n;
    }
    OmRet r = flash_sim_read(dev, addr, buf, len);
    (void)exc_atomic_dec(&g_inBackend);
    return r;
}

static OmRet wrap_write(FlashDev *dev, uint32_t addr, const void *data, size_t len)
{
    long n = exc_atomic_inc(&g_inBackend);
    if (n > g_maxInBackend)
    {
        g_maxInBackend = n;
    }
    OmRet r = flash_sim_write(dev, addr, data, len);
    (void)exc_atomic_dec(&g_inBackend);
    return r;
}

static OmRet wrap_erase(FlashDev *dev, uint32_t addr, size_t len)
{
    long n = exc_atomic_inc(&g_inBackend);
    if (n > g_maxInBackend)
    {
        g_maxInBackend = n;
    }
    OmRet r = flash_sim_erase(dev, addr, len);
    (void)exc_atomic_dec(&g_inBackend);
    return r;
}

static const FlashOps wrap_ops = {
    .read = wrap_read,
    .write = wrap_write,
    .erase = wrap_erase,
};

static FlashDev g_flash_dev;
static FlashSim g_flash_sim;

/* —— 非均一几何夹具：同一器件内含多种扇区尺寸 ——
 * 通用构造，不绑定任何平台族。擦除单位 API 只有在非均一下才测得出真行为
 * （均一几何下"单位序列"退化为等分，测不出跨尺寸切换） */
#define CAP_MIX (288u * 1024u)
static const FlashSectorRegion regs_mix[] = {
    {0u, 16384u, 2u},      /*   0 ..  32K：16K × 2 */
    {32768u, 65536u, 1u},  /*  32K ..  96K：64K × 1 */
    {98304u, 131072u, 1u}, /*  96K .. 224K：128K × 1 */
    {229376u, 32768u, 2u}, /* 224K .. 288K：32K × 2 */
};
static const FlashGeometry geom_mix = {
    .capacity = CAP_MIX,
    .erasedValue = 0xFF,
    .writeUnit = 4u,
    .pageSize = 0u,
    .sectorSize = 0u, /* 0 = 使用区域表 */
    .sectorCount = 0,
    .sectorRegions = regs_mix,
};

static FlashDev g_mix_dev;
static FlashSim g_mix_sim;

/* 分区名（表符号的 host 替身） */
#define P_BOOT "boot"
#define P_APP "app"
#define P_META "meta"
#define P_BAD_ALIGN "bad_align" /* 错误配置 1：大小非扇区对齐 */
#define P_GHOST "ghost"         /* 错误配置 2：器件不存在 */

/* 表实例（模拟板/工程 bootcfg 数据）：const 静态存储期 + OM_PARTITION_REGISTRY
 * 编译期常量——验证 v2 "免运行期注册、表可驻 ROM" 形态。
 * 分治：good = 合法表（含幽灵器件条目）；geom_bad = 非扇区友好；
 *       cap_bad = 越器件容量；malformed = 含未填名条目 */
static const OmPartitionEntry table_good[] = {
    {P_BOOT, "flash0", 0x00000u, 0x1000u},
    {P_APP, "flash0", 0x01000u, 0x20000u},
    {P_META, "flash0", 0x21000u, 0x1000u},
    {P_GHOST, "no_such_flash", 0x00000u, 0x1000u}, /* 幽灵器件：open 期 NOT_FOUND */
};
static const OmPartitionRegistry reg_good = OM_PARTITION_REGISTRY(table_good);
#define G_COUNT (sizeof(table_good) / sizeof(table_good[0]))

static const OmPartitionEntry table_geom_bad[] = {
    {P_APP, "flash0", 0x01000u, 0x20000u},
    {P_BAD_ALIGN, "flash0", 0x22000u, 0x100u}, /* 非 4KB 扇区对齐（size 非整扇区） */
};
static const OmPartitionRegistry reg_geom_bad = OM_PARTITION_REGISTRY(table_geom_bad);

static const OmPartitionEntry table_cap_bad[] = {
    {P_APP, "flash0", CAP - 0x1000u, 0x2000u}, /* 越器件容量 */
};
static const OmPartitionRegistry reg_cap_bad = OM_PARTITION_REGISTRY(table_cap_bad);

/* 结构坏表（不依赖器件即可判） */
static const OmPartitionEntry dup_table[] = {
    {P_APP, "flash0", 0x1000u, 0x2000u},
    {P_APP, "flash0", 0x3000u, 0x2000u}, /* 重名 */
};
static const OmPartitionRegistry reg_dup = OM_PARTITION_REGISTRY(dup_table);
static const OmPartitionEntry bad_field[] = {{NULL, "flash0", 0u, 0x1000u}};
static const OmPartitionRegistry reg_bad_field = OM_PARTITION_REGISTRY(bad_field);
static const OmPartitionEntry zero_size[] = {{P_APP, "flash0", 0u, 0u}};
static const OmPartitionRegistry reg_zero_size = OM_PARTITION_REGISTRY(zero_size);

/* 非均一表（挂 flash_mix）：三个互不重叠的分区，覆盖单位序列的三态 */
#define P_MIX_SPAN "mix_span" /*   0 ..  96K：16K+16K+64K → 非均一，3 单位，max 64K */
#define P_MIX_WIDE "mix_wide" /*  96K .. 224K：128K       → 均一，1 单位 */
#define P_MIX_FLAT "mix_flat" /* 224K .. 288K：32K×2     → 均一，2 单位 */
static const OmPartitionEntry table_mix[] = {
    {P_MIX_SPAN, "flash_mix", 0u, 98304u},
    {P_MIX_WIDE, "flash_mix", 98304u, 131072u},
    {P_MIX_FLAT, "flash_mix", 229376u, 65536u},
};
static const OmPartitionRegistry reg_mix = OM_PARTITION_REGISTRY(table_mix);

/* 不可用注册表（空表语义）三形态：全空 / NULL 表 / count==0 */
static const OmPartitionRegistry reg_none = {NULL, 0u};
static const OmPartitionRegistry reg_null_table = {NULL, 3u};
static const OmPartitionRegistry reg_zero_count = {table_good, 0u};

/* 畸形表：条目 name == NULL（RAM 半填 / 未经 validate 的典型形态）——
 * 遍历须跳过而非 strcmp 硬故障 */
static const OmPartitionEntry table_malformed[] = {
    {P_BOOT, "flash0", 0x00000u, 0x1000u},
    {NULL, "flash0", 0x01000u, 0x1000u}, /* 半填条目：前后合法条目仍应可命中 */
    {P_META, "flash0", 0x21000u, 0x1000u},
};
static const OmPartitionRegistry reg_malformed = OM_PARTITION_REGISTRY(table_malformed);

/* ===================================================================
 * T0: 注册表面防线——不可用注册表 / 结构校验 / 顺序解耦 / 几何 fail-fast
 *     flash 器件注册点在此函数内按防线顺序插入
 * =================================================================== */

static void test_registry_defenses(void)
{
    printf("[T0] registry defenses (empty semantics / structural / deferred geometry)\n");
    OmPartitionEntry e = {0};
    OmPartitionHandle h = {0};

    /* 不可用注册表：解析面 = 空表语义 NOT_FOUND，校验面 = INVALID_ARG，计数 0
     * （v1 "未注册空表"语义在 v2 由注册表三形态直接表达） */
    CHECK(om_partition_query(&reg_none, P_APP, &e) == OM_ERR_NOT_FOUND,
          "query on empty registry -> NOT_FOUND");
    CHECK(om_partition_open(&reg_none, P_APP, &h) == OM_ERR_NOT_FOUND,
          "open on empty registry -> NOT_FOUND");
    CHECK(om_partition_query(&reg_null_table, P_APP, &e) == OM_ERR_NOT_FOUND,
          "query with NULL table -> NOT_FOUND (empty semantics)");
    CHECK(om_partition_open(&reg_zero_count, P_APP, &h) == OM_ERR_NOT_FOUND,
          "open with count==0 -> NOT_FOUND (empty semantics)");
    CHECK(om_partition_registry_validate(&reg_none) == OM_ERR_INVALID_ARG,
          "validate empty registry rejected");
    CHECK(om_partition_registry_validate(&reg_null_table) == OM_ERR_INVALID_ARG,
          "validate NULL table rejected");
    CHECK(om_partition_registry_validate(&reg_zero_count) == OM_ERR_INVALID_ARG,
          "validate zero count rejected");
    CHECK(om_partition_registry_validate(NULL) == OM_ERR_INVALID_ARG, "validate(NULL) rejected");
    CHECK(om_partition_registry_count(&reg_none) == 0u, "count empty registry == 0");
    CHECK(om_partition_registry_count(&reg_zero_count) == 0u, "count zero-count registry == 0");
    CHECK(om_partition_registry_count(NULL) == 0u, "count(NULL) == 0");

    /* 结构校验（不依赖器件）：重名 / name==NULL / size==0 */
    CHECK(om_partition_registry_validate(&reg_dup) == OM_ERR_INVALID_ARG,
          "duplicate names rejected");
    CHECK(om_partition_registry_validate(&reg_bad_field) == OM_ERR_INVALID_ARG,
          "NULL name rejected");
    CHECK(om_partition_registry_validate(&reg_zero_size) == OM_ERR_INVALID_ARG,
          "zero size rejected");

    /* 顺序解耦：器件未注册 → 几何校验跳过，坏几何表暂判合法（与 v1 注册期一致） */
    CHECK(om_partition_registry_validate(&reg_geom_bad) == OM_OK,
          "geometry check deferred while flash0 not registered");
    CHECK(om_partition_registry_validate(&reg_cap_bad) == OM_OK,
          "capacity check deferred while flash0 not registered");

    /* 注册 flash 器件（几何真源就位） */
    CHECK(flash_register(&g_flash_dev, "flash0", &geom_uniform, &wrap_ops, &g_flash_sim) ==
              OM_OK,
          "register sim flash0");

    /* 几何 fail-fast：器件在 → 坏表 INVALID_ARG；校验是纯函数，好表不受影响 */
    CHECK(om_partition_registry_validate(&reg_geom_bad) == OM_ERR_INVALID_ARG,
          "misaligned partition table rejected (sector-friendly)");
    CHECK(om_partition_registry_validate(&reg_cap_bad) == OM_ERR_INVALID_ARG,
          "over-capacity partition rejected");
    CHECK(om_partition_registry_validate(&reg_good) == OM_OK,
          "valid table passes (ghost-device entry skipped: device not registered)");

    /* open 期几何兜底：坏条目产生不了句柄（v1 的操作期兜底在 v2 前移至 open） */
    CHECK(om_partition_open(&reg_geom_bad, P_BAD_ALIGN, &h) == OM_ERR_INVALID_ARG,
          "open misaligned partition rejected");
    CHECK(om_partition_open(&reg_cap_bad, P_APP, &h) == OM_ERR_INVALID_ARG,
          "open over-capacity partition rejected");
}

/* ===================================================================
 * T1: 按名查询（by-value 值拷贝语义；纯信息不碰器件）
 * =================================================================== */

static void test_query(void)
{
    printf("[T1] query by name (value copy)\n");

    OmPartitionEntry e = {0};
    CHECK(om_partition_query(&reg_good, P_APP, &e) == OM_OK, "query existing partition");
    CHECK(e.offset == 0x01000u && e.size == 0x20000u && strcmp(e.name, P_APP) == 0 &&
              strcmp(e.devName, "flash0") == 0,
          "entry fields match table");
    CHECK(om_partition_query(&reg_good, "no_such", &e) == OM_ERR_NOT_FOUND,
          "query miss -> NOT_FOUND");
    CHECK(om_partition_query(&reg_good, NULL, &e) == OM_ERR_INVALID_ARG, "query NULL name rejected");
    CHECK(om_partition_query(&reg_good, P_APP, NULL) == OM_ERR_INVALID_ARG,
          "query NULL out rejected");

    /* query 不碰器件：幽灵器件条目可查到（open 才拒——T3 验） */
    CHECK(om_partition_query(&reg_good, P_GHOST, &e) == OM_OK,
          "query ghost-device entry ok (no device touch)");

    /* 值拷贝隔离：篡改拷贝（含字段源头）不影响表/后续查询 */
    OmPartitionEntry forged;
    CHECK(om_partition_query(&reg_good, P_APP, &forged) == OM_OK, "query for forgery setup");
    forged.offset = 0;
    forged.size = CAP;
    forged.name = "hacked";
    forged.devName = "no_such";
    OmPartitionEntry again = {0}; /* 零初始化：query 失败时 *out 不动，防后续 CHECK 读未初始化内存 */
    CHECK(om_partition_query(&reg_good, P_APP, &again) == OM_OK, "re-query after forgery");
    CHECK(again.offset == 0x01000u && again.size == 0x20000u && strcmp(again.name, P_APP) == 0 &&
              strcmp(again.devName, "flash0") == 0,
          "forged copy does not affect authoritative table");
}

/* ===================================================================
 * T2: 句柄数据通路——open + 分区内双端边界 + len==0 契约
 * =================================================================== */

static void test_io_boundary(void)
{
    printf("[T2] handle I/O bounds\n");
    static uint8_t buf[512];
    OmPartitionHandle h = {0};

    CHECK(om_partition_open(&reg_good, P_BOOT, &h) == OM_OK, "open boot partition");
    CHECK(h.reg == &reg_good && h.index < G_COUNT, "handle carries registry + in-domain index");

    /* 读写环：分区内偏移语义 */
    memset(buf, 0x5A, sizeof(buf));
    CHECK(om_partition_write(&h, 0u, buf, sizeof(buf)) == OM_OK, "write within partition");
    memset(buf, 0, sizeof(buf));
    CHECK(om_partition_read(&h, 0u, buf, sizeof(buf)) == OM_OK, "read within partition");
    CHECK(buf[0] == 0x5A && buf[511] == 0x5A, "roundtrip content matches");

    /* 双端越界 → OM_ERR_RANGE（与 INVALID_ARG 分开：改偏移即可，行动不同） */
    CHECK(om_partition_read(&h, 0x1000u, buf, 1u) == OM_ERR_RANGE,
          "read at partition end rejected");
    CHECK(om_partition_read(&h, 0x0FF0u, buf, 0x20u) == OM_ERR_RANGE,
          "read crossing end rejected");
    CHECK(om_partition_write(&h, 0x0FFCu, buf, 8u) == OM_ERR_RANGE,
          "write crossing end rejected");
    CHECK(om_partition_read(&h, 0u, NULL, 1u) == OM_ERR_INVALID_ARG, "read NULL buf rejected");
    CHECK(om_partition_write(&h, 0u, NULL, 1u) == OM_ERR_INVALID_ARG, "write NULL data rejected");

    /* 恰好贴合分区尾：合法（域为 [0, size) 闭包） */
    CHECK(om_partition_write(&h, 0x0FF0u, buf, 16u) == OM_OK, "write flush to partition end ok");
    CHECK(om_partition_read(&h, 0x0FF0u, buf, 16u) == OM_OK, "read flush to partition end ok");

    /* len == 0：合法调用，但 buf/data 仍须非空、off 仍须在域内 */
    CHECK(om_partition_read(&h, 0u, buf, 0u) == OM_OK, "read len==0 ok");
    CHECK(om_partition_write(&h, 0u, buf, 0u) == OM_OK, "write len==0 ok");
    CHECK(om_partition_read(&h, 0u, NULL, 0u) == OM_ERR_INVALID_ARG,
          "read len==0 NULL buf rejected");
    CHECK(om_partition_write(&h, 0u, NULL, 0u) == OM_ERR_INVALID_ARG,
          "write len==0 NULL data rejected");
    CHECK(om_partition_read(&h, 0x1000u, buf, 0u) == OM_ERR_RANGE,
          "read len==0 at partition end rejected (off must be in range)");

    /* v1 的"按名未命中"在 v2 由 open 表达 */
    CHECK(om_partition_open(&reg_good, "no_such", &h) == OM_ERR_NOT_FOUND,
          "open unknown name -> NOT_FOUND");
    CHECK(om_partition_open(&reg_good, NULL, &h) == OM_ERR_INVALID_ARG, "open NULL name rejected");
    CHECK(om_partition_open(&reg_good, P_APP, NULL) == OM_ERR_INVALID_ARG,
          "open NULL handle out rejected");
}

/* ===================================================================
 * T3: 整分区擦 + 幽灵器件（配置错误显式报错）
 * =================================================================== */

static void test_erase_and_misconfig(void)
{
    printf("[T3] whole-partition erase + ghost device rejected loudly\n");

    static uint8_t pat[512];
    static uint8_t probe[16];
    OmPartitionHandle h = {0};

    CHECK(om_partition_open(&reg_good, P_BOOT, &h) == OM_OK, "open boot for erase");

    /* (prep) 让擦除证明自带前置状态：先复位为空白，再由本用例写入已知图样并
     * 读回确认。否则"擦后 = 0xFF"可能只是命中原本就空白的区间——擦除被短路
     * （如实现退化为直接 return OM_OK）也照样通过（同 flash_dev_test 的
     * (prep) 模式）。 */
    CHECK(om_partition_erase(&h) == OM_OK, "(prep) erase reset before pattern");
    memset(pat, 0x3C, sizeof(pat));
    CHECK(om_partition_write(&h, 0u, pat, sizeof(pat)) == OM_OK, "(prep) write known pattern");
    memset(probe, 0, sizeof(probe));
    CHECK(om_partition_read(&h, 0u, probe, sizeof(probe)) == OM_OK, "(prep) read back pattern");
    CHECK(probe[0] == 0x3C, "(prep) pattern present before erase");

    /* 整分区擦（对齐条目）→ 内容回擦除值 */
    CHECK(om_partition_erase(&h) == OM_OK, "erase whole aligned partition");
    memset(probe, 0x11, sizeof(probe));
    CHECK(om_partition_read(&h, 0u, probe, sizeof(probe)) == OM_OK, "probe erased partition");
    CHECK(probe[0] == 0xFF, "partition erased to 0xFF");

    /* 幽灵器件（合法表内）：访问入口 open 显式 NOT_FOUND（T1 的 query 对照项） */
    OmPartitionHandle ghost = {0};
    CHECK(om_partition_open(&reg_good, P_GHOST, &ghost) == OM_ERR_NOT_FOUND,
          "ghost-device partition rejected at open");
}

/* ===================================================================
 * T4: 多注册表隔离 + 句柄域校验 + 枚举面
 * =================================================================== */

/* 第二张表：与 reg_good 同名不同址（分区名只在表内唯一，跨表各自解析） */
static const OmPartitionEntry table_b[] = {
    {P_BOOT, "flash0", 0x10000u, 0x1000u}, /* 同名 boot，器件偏移不同 */
    {P_APP, "flash0", 0x11000u, 0x10000u},
};
static const OmPartitionRegistry reg_b = OM_PARTITION_REGISTRY(table_b);
#define B_COUNT (sizeof(table_b) / sizeof(table_b[0]))

static void test_multi_registry_and_handle(void)
{
    printf("[T4] multi-registry isolation + handle domain check + enumeration\n");

    OmPartitionHandle ha = {0};
    OmPartitionHandle hb = {0};
    OmPartitionEntry ea = {0};
    OmPartitionEntry eb = {0};

    /* 同名条目按各自注册表解析（偏移不同 → 无串扰） */
    CHECK(om_partition_open(&reg_good, P_BOOT, &ha) == OM_OK, "open boot in reg_good");
    CHECK(om_partition_open(&reg_b, P_BOOT, &hb) == OM_OK, "open boot in reg_b");
    CHECK(om_partition_query(&reg_good, P_BOOT, &ea) == OM_OK &&
              om_partition_query(&reg_b, P_BOOT, &eb) == OM_OK,
          "query same name in both registries");
    CHECK(ea.offset == 0x00000u && eb.offset == 0x10000u,
          "same name resolves per-registry (no cross-talk)");

    /* 跨注册表操作互不影响：两 boot 物理区不重叠（器件 [0,0x1000) vs [0x10000,0x11000)） */
    static uint8_t w[16];
    static uint8_t r[16];
    memset(w, 0x33, sizeof(w));
    CHECK(om_partition_write(&ha, 0u, w, sizeof(w)) == OM_OK, "write via handle A");
    CHECK(om_partition_read(&hb, 0u, r, sizeof(r)) == OM_OK, "read via handle B");
    CHECK(r[0] == 0xFF, "reg_b region untouched by reg_good write");

    memset(w, 0x44, sizeof(w));
    CHECK(om_partition_write(&hb, 0u, w, sizeof(w)) == OM_OK, "write via handle B");
    CHECK(om_partition_read(&ha, 0u, r, sizeof(r)) == OM_OK, "read back via handle A");
    CHECK(r[0] == 0x33, "reg_good region untouched by reg_b write");

    /* 句柄域校验：伪造 index / NULL reg / 不可用 reg / NULL 句柄一律 INVALID_ARG */
    OmPartitionHandle forged = {&reg_good, 99u};
    CHECK(om_partition_read(&forged, 0u, r, 1u) == OM_ERR_INVALID_ARG,
          "forged index read rejected");
    CHECK(om_partition_write(&forged, 0u, r, 1u) == OM_ERR_INVALID_ARG,
          "forged index write rejected");
    CHECK(om_partition_erase(&forged) == OM_ERR_INVALID_ARG, "forged index erase rejected");
    CHECK(om_partition_erase_range(&forged, 0u, 0u) == OM_ERR_INVALID_ARG,
          "forged erase_range rejected (handle check precedes len==0 no-op)");

    OmPartitionHandle null_reg = {NULL, 0u};
    CHECK(om_partition_read(&null_reg, 0u, r, 1u) == OM_ERR_INVALID_ARG,
          "handle with NULL reg rejected");

    OmPartitionHandle empty_reg = {&reg_zero_count, 0u};
    CHECK(om_partition_read(&empty_reg, 0u, r, 1u) == OM_ERR_INVALID_ARG,
          "handle with unusable registry rejected");

    CHECK(om_partition_read(NULL, 0u, r, 1u) == OM_ERR_INVALID_ARG, "NULL handle read rejected");
    CHECK(om_partition_write(NULL, 0u, r, 1u) == OM_ERR_INVALID_ARG, "NULL handle write rejected");
    CHECK(om_partition_erase(NULL) == OM_ERR_INVALID_ARG, "NULL handle erase rejected");

    /* 枚举面 */
    CHECK(om_partition_registry_count(&reg_good) == G_COUNT, "registry_count(reg_good) == G_COUNT");
    CHECK(om_partition_registry_count(&reg_b) == B_COUNT, "registry_count(reg_b) == B_COUNT");
    OmPartitionEntry e0 = {0};
    CHECK(om_partition_registry_at(&reg_good, 0u, &e0) == OM_OK && strcmp(e0.name, P_BOOT) == 0,
          "registry_at(0) is boot");
    CHECK(om_partition_registry_at(&reg_good, G_COUNT - 1u, &e0) == OM_OK &&
              strcmp(e0.name, P_GHOST) == 0,
          "registry_at(G_COUNT-1) is ghost (last, in range)");
    CHECK(om_partition_registry_at(&reg_good, G_COUNT, &e0) == OM_ERR_NOT_FOUND,
          "registry_at out of range -> NOT_FOUND");
    CHECK(om_partition_registry_at(&reg_good, 0u, NULL) == OM_ERR_INVALID_ARG,
          "registry_at NULL out rejected");
    CHECK(om_partition_registry_at(NULL, 0u, &e0) == OM_ERR_INVALID_ARG,
          "registry_at(NULL) rejected");
    CHECK(om_partition_registry_at(&reg_zero_count, 0u, &e0) == OM_ERR_INVALID_ARG,
          "registry_at unusable registry rejected");
}

/* ===================================================================
 * T5: RAM 来源注册表（运行期填表，行为与 const 注册表一致）
 * =================================================================== */

static OmPartitionEntry g_ram_table[2];
static OmPartitionRegistry g_ram_reg;

static void test_ram_sourced_registry(void)
{
    printf("[T5] RAM-sourced registry\n");

    g_ram_table[0] = (OmPartitionEntry){P_META, "flash0", 0x21000u, 0x1000u};
    g_ram_table[1] = (OmPartitionEntry){P_APP, "flash0", 0x01000u, 0x20000u};
    g_ram_reg.table = g_ram_table;
    g_ram_reg.count = 2u;

    CHECK(om_partition_registry_validate(&g_ram_reg) == OM_OK, "validate RAM registry");
    OmPartitionHandle h = {0};
    CHECK(om_partition_open(&g_ram_reg, P_META, &h) == OM_OK, "open in RAM registry");
    static uint8_t b[8];
    CHECK(om_partition_read(&h, 0u, b, sizeof(b)) == OM_OK, "read via RAM registry handle");
    OmPartitionEntry e = {0};
    CHECK(om_partition_registry_at(&g_ram_reg, 1u, &e) == OM_OK && strcmp(e.name, P_APP) == 0,
          "enumerate RAM registry");

    /* 零状态：改调用方 count 立即可见（模块不持有副本/缓存） */
    g_ram_reg.count = 1u;
    CHECK(om_partition_registry_count(&g_ram_reg) == 1u, "count change visible immediately");
    CHECK(om_partition_open(&g_ram_reg, P_APP, &h) == OM_ERR_NOT_FOUND,
          "shrunk RAM registry no longer resolves app");
    g_ram_reg.count = 2u;
}

/* ===================================================================
 * T6: erase_range——范围擦（扇区对齐由器件层强制，不静默扩擦）
 * =================================================================== */

static void test_erase_range(void)
{
    printf("[T6] erase_range\n");
    OmPartitionHandle h = {0};  /* app：器件 0x01000 起 0x20000 = 32 个 4K 扇区 */
    OmPartitionHandle hb = {0}; /* boot：app 的前邻分区，验证不向下扩擦 */

    CHECK(om_partition_open(&reg_good, P_APP, &h) == OM_OK, "open app partition");
    CHECK(om_partition_open(&reg_good, P_BOOT, &hb) == OM_OK, "open boot for erase_range sentinel");

    static uint8_t pat[0x1000];
    static uint8_t edge[16];
    static uint8_t probe[8];
    memset(pat, 0x77, sizeof(pat));
    memset(edge, 0xAB, sizeof(edge));

    /* 预置：app 前两扇区 + boot 尾 16 字节（前后邻区哨兵） */
    CHECK(om_partition_write(&h, 0u, pat, sizeof(pat)) == OM_OK, "write app sector 0");
    CHECK(om_partition_write(&h, 0x1000u, pat, sizeof(pat)) == OM_OK, "write app sector 1");
    CHECK(om_partition_write(&hb, 0x0FF0u, edge, sizeof(edge)) == OM_OK,
          "write boot tail sentinel");

    /* 只擦第一扇区：第二扇区（上邻）与 boot 尾（下邻）必须原样 */
    CHECK(om_partition_erase_range(&h, 0u, 0x1000u) == OM_OK, "erase_range first sector");
    CHECK(om_partition_read(&h, 0u, probe, sizeof(probe)) == OM_OK, "read erased sector");
    CHECK(probe[0] == 0xFF, "first sector erased to 0xFF");
    CHECK(om_partition_read(&h, 0x1000u, probe, sizeof(probe)) == OM_OK, "read kept sector");
    CHECK(probe[0] == 0x77, "second sector untouched (no silent over-erase)");
    CHECK(om_partition_read(&hb, 0x0FF0u, probe, sizeof(probe)) == OM_OK, "read boot tail");
    CHECK(probe[0] == 0xAB, "preceding partition untouched");

    /* 非扇区对齐 / 错位偏移：器件层显式拒绝（范围内，故报 INVALID_ARG） */
    CHECK(om_partition_erase_range(&h, 0u, 0x800u) == OM_ERR_INVALID_ARG,
          "half-sector length rejected by device layer");
    CHECK(om_partition_erase_range(&h, 2u, 0x800u) == OM_ERR_INVALID_ARG,
          "misaligned offset rejected by device layer");

    /* 越界优先于未对齐：off 错位（0x1F002 % 0x1000 != 0）且长度越尾
     * （0x1F002 + 0x2000 > 0x20000）→ 报 RANGE，不是 INVALID_ARG。
     * 与上一条成对，钉住两者的优先级：越界是更根本的错误 */
    CHECK(om_partition_erase_range(&h, 0x1F002u, 0x2000u) == OM_ERR_RANGE,
          "OOB precedes misalignment");

    /* 越分区尾：器件层会放行（0x20000/0x22000 都是扇区边界），只有本层双端断言
     * 能拦住"擦穿邻分区"——分区层的边界是防静默扩擦的最后一道 */
    CHECK(om_partition_erase_range(&h, 0x1F000u, 0x2000u) == OM_ERR_RANGE,
          "range crossing partition end rejected (would spill into meta)");

    /* len == 0：无操作，但句柄有效性先行 */
    CHECK(om_partition_erase_range(&h, 0u, 0u) == OM_OK, "len==0 no-op with valid handle");
    CHECK(om_partition_erase_range(NULL, 0u, 0u) == OM_ERR_INVALID_ARG,
          "NULL handle rejected before len==0 no-op");
}

/* ===================================================================
 * T7: 畸形条目（name == NULL）——跳过而非硬故障，未命中报配置错
 * =================================================================== */

static void test_malformed_entries(void)
{
    printf("[T7] malformed entry (name==NULL) handling\n");

    OmPartitionHandle h = {0};
    OmPartitionEntry e = {0};

    /* 全表校验是畸形条件的报告者 */
    CHECK(om_partition_registry_validate(&reg_malformed) == OM_ERR_INVALID_ARG,
          "validate rejects malformed entry");

    /* open 不是全表校验器：畸形条目前后两端的合法条目都可命中 */
    CHECK(om_partition_open(&reg_malformed, P_BOOT, &h) == OM_OK,
          "valid entry before malformed still opens");
    OmPartitionHandle h2 = {0};
    CHECK(om_partition_open(&reg_malformed, P_META, &h2) == OM_OK,
          "valid entry after malformed still opens");

    /* 未命中：表中存在畸形条目 → 配置错 INVALID_ARG（而非查找未命中） */
    CHECK(om_partition_open(&reg_malformed, "no_such", &h) == OM_ERR_INVALID_ARG,
          "open miss with malformed present -> INVALID_ARG");
    /* 对照：干净表未命中仍是 NOT_FOUND（防"一律 INVALID_ARG"回归） */
    CHECK(om_partition_open(&reg_good, "no_such", &h) == OM_ERR_NOT_FOUND,
          "clean-registry miss stays NOT_FOUND");

    /* query 跳过畸形条目：未命中一律 NOT_FOUND（畸形条件由 validate 报，query 不报） */
    CHECK(om_partition_query(&reg_malformed, P_BOOT, &e) == OM_OK,
          "query valid entry before malformed");
    CHECK(om_partition_query(&reg_malformed, P_META, &e) == OM_OK,
          "query valid entry after malformed");
    CHECK(om_partition_query(&reg_malformed, "no_such", &e) == OM_ERR_NOT_FOUND,
          "query miss with malformed present -> NOT_FOUND");
}

/* ===================================================================
 * main
 * =================================================================== */

/* ===================================================================
 * T9: 能力与几何查询面——非均一几何是主战场
 * =================================================================== */

static void test_capability_queries(void)
{
    printf("[T9] capability & geometry queries\n");

    CHECK(flash_register(&g_mix_dev, "flash_mix", &geom_mix, &sim_ops, &g_mix_sim) == OM_OK,
          "register non-uniform device");

    OmPartitionHandle h;

    /* —— 查询面：分区只回答容量；器件属性一律经器件几何查，不存副本 —— */
    CHECK(om_partition_open(&reg_mix, P_MIX_SPAN, &h) == OM_OK, "open non-uniform partition");
    CHECK(om_partition_capacity(&h) == 98304u, "capacity = partition size (not device capacity)");
    const FlashGeometry *dg = om_partition_dev_geom(&h);
    CHECK(dg != NULL, "dev geom non-NULL for valid handle");
    CHECK(dg->writeUnit == 4u && dg->erasedValue == 0xFFu, "device facts come from the device");
    /* 能力位取否定式：零值 = 保守默认（须擦）。用 bool 的话零值恰好是危险侧 */
    CHECK((dg->caps & FLASH_CAP_NO_ERASE_NEEDED) == 0u,
          "cap: NO_ERASE_NEEDED unset (conservative default)");
    CHECK(flash_geom_needs_erase(&geom_uniform), "cap: device requires erase before write");

    /* —— 非均一：单位序列必须逐段正确（本 API 存在的理由） —— */
    CHECK(om_partition_erase_unit_count(&h) == 3u, "non-uniform: 3 units");
    CHECK(!om_partition_is_uniform(&h), "non-uniform detected");
    CHECK(om_partition_max_erase_unit(&h) == 65536u, "max unit = 64K (explicit, not silent)");

    OmPartitionEraseUnit u;
    CHECK(om_partition_erase_unit_at(&h, 0u, &u) == OM_OK && u.offset == 0u && u.size == 16384u,
          "unit[0] = {0, 16K}");
    CHECK(om_partition_erase_unit_at(&h, 1u, &u) == OM_OK && u.offset == 16384u &&
              u.size == 16384u,
          "unit[1] = {16K, 16K}");
    CHECK(om_partition_erase_unit_at(&h, 2u, &u) == OM_OK && u.offset == 32768u &&
              u.size == 65536u,
          "unit[2] = {32K, 64K}");
    CHECK(om_partition_erase_unit_at(&h, 3u, &u) == OM_ERR_NOT_FOUND, "unit[3] out of range");
    CHECK(om_partition_erase_unit_at(&h, 0u, NULL) == OM_ERR_INVALID_ARG, "at(NULL out) rejected");

    /* —— 点查询：跨尺寸边界必须切换 —— */
    CHECK(om_partition_erase_unit_covering(&h, 0u, &u) == OM_OK && u.offset == 0u &&
              u.size == 16384u,
          "covering(0) -> unit[0]");
    CHECK(om_partition_erase_unit_covering(&h, 0x3FFFu, &u) == OM_OK && u.offset == 0u,
          "covering(16K-1) stays in unit[0]");
    CHECK(om_partition_erase_unit_covering(&h, 0x4000u, &u) == OM_OK && u.offset == 16384u,
          "covering(16K) -> unit[1]");
    CHECK(om_partition_erase_unit_covering(&h, 0x7FFFu, &u) == OM_OK && u.offset == 16384u,
          "covering(32K-1) stays in unit[1]");
    CHECK(om_partition_erase_unit_covering(&h, 0x8000u, &u) == OM_OK && u.offset == 32768u &&
              u.size == 65536u,
          "covering(32K) -> unit[2] (64K)");
    CHECK(om_partition_erase_unit_covering(&h, 0x17FFFu, &u) == OM_OK && u.offset == 32768u,
          "covering(96K-1) stays in unit[2]");
    CHECK(om_partition_erase_unit_covering(&h, 0x18000u, &u) == OM_ERR_RANGE,
          "covering(partition end) -> RANGE");
    CHECK(om_partition_erase_unit_covering(&h, 0u, NULL) == OM_ERR_INVALID_ARG,
          "covering(NULL out) rejected");

    /* —— 其余两态：均一多单位 / 均一单单位 —— */
    CHECK(om_partition_open(&reg_mix, P_MIX_FLAT, &h) == OM_OK, "open uniform 2-unit partition");
    CHECK(om_partition_is_uniform(&h), "uniform detected");
    CHECK(om_partition_erase_unit_count(&h) == 2u, "uniform: 2 units");
    CHECK(om_partition_max_erase_unit(&h) == 32768u, "uniform max = 32K");
    CHECK(om_partition_open(&reg_mix, P_MIX_WIDE, &h) == OM_OK, "open 1-unit partition");
    CHECK(om_partition_is_uniform(&h), "1-unit partition is uniformly sized");
    CHECK(om_partition_erase_unit_count(&h) == 1u, "1-unit partition count");
    CHECK(om_partition_max_erase_unit(&h) == 131072u, "1-unit partition max");

    /* —— 非法句柄：查询面一律不崩，返回保守值 —— */
    OmPartitionHandle junk;
    memset(&junk, 0, sizeof(junk));
    CHECK(om_partition_capacity(&junk) == 0u, "capacity(junk handle) -> 0");
    CHECK(om_partition_dev_geom(&junk) == NULL, "dev geom(junk handle) -> NULL");
    CHECK(om_partition_erase_unit_count(&junk) == 0u, "count(junk handle) -> 0");
    CHECK(!om_partition_is_uniform(&junk), "is_uniform(junk) -> false (conservative)");
    CHECK(om_partition_max_erase_unit(&junk) == 0u, "max(junk handle) -> 0");
    CHECK(om_partition_erase_unit_at(&junk, 0u, &u) == OM_ERR_INVALID_ARG,
          "at(junk handle) rejected");

    /* —— 已擦自检 —— */
    CHECK(om_partition_open(&reg_mix, P_MIX_FLAT, &h) == OM_OK, "open for is_erased");
    bool erased = false;
    CHECK(om_partition_is_erased(&h, 0u, 4096u, &erased) == OM_OK && erased,
          "fresh partition reads as erased");
    uint8_t pat[64];
    memset(pat, 0x00, sizeof(pat));
    CHECK(om_partition_write(&h, 0u, pat, sizeof(pat)) == OM_OK, "(prep) dirty first 64 bytes");
    CHECK(om_partition_is_erased(&h, 0u, 4096u, &erased) == OM_OK && !erased,
          "dirtied partition not erased");
    CHECK(om_partition_is_erased(&h, 64u, 4032u, &erased) == OM_OK && erased,
          "undirtied tail still erased");
    CHECK(om_partition_is_erased(&h, 0u, 0u, &erased) == OM_OK && erased,
          "len==0 vacuously erased");
    CHECK(om_partition_is_erased(&h, 0x10000u, 1u, &erased) == OM_ERR_RANGE,
          "is_erased OOB -> RANGE");
    CHECK(om_partition_is_erased(&h, 0u, 1u, NULL) == OM_ERR_INVALID_ARG,
          "is_erased NULL out rejected");
    CHECK(om_partition_erase(&h) == OM_OK, "(prep) erase partition");
    CHECK(om_partition_is_erased(&h, 0u, 65536u, &erased) == OM_OK && erased,
          "erased partition reads as erased again");

    /* —— 擦后校验：后端"报告成功但未落位" ⇒ 器件层判该区结构性不可用 ——
     * 这是"以为已擦"的防线：擦除返回 OK 不再等于"确实处于已擦状态" */
    CHECK(om_partition_open(&reg_mix, P_MIX_FLAT, &h) == OM_OK, "open for erase-verify test");
    memset(pat, 0x00, sizeof(pat));
    CHECK(om_partition_write(&h, 0u, pat, sizeof(pat)) == OM_OK, "(prep) dirty for verify test");
    uint32_t opsBefore = g_mix_sim.opCount;
    flash_sim_set_silent_erase_fail(&g_mix_sim, true);
    CHECK(om_partition_erase(&h) == OM_ERR_FLASH_UNUSABLE,
          "silent erase failure -> UNUSABLE (not OK)");
    /* 默认 OM_FLASH_ERASE_VERIFY_ATTEMPTS == 1 ⇒ 恰好 1 次擦 + 1 次校验读
     * （首块即失配，校验提前返回）。调大该宏时此断言应同步改成 2*N */
    CHECK(g_mix_sim.opCount == opsBefore + 2u,
          "attempts=1: exactly one erase + one verify read");
    flash_sim_set_silent_erase_fail(&g_mix_sim, false);
    CHECK(om_partition_erase(&h) == OM_OK, "erase OK again once fault cleared");
    CHECK(om_partition_is_erased(&h, 0u, 4096u, &erased) == OM_OK && erased,
          "verified erase really erased");

    /* —— 缓存指针守卫：句柄内 dev 为空即拒（零初始化/半填句柄的廉价保险） —— */
    CHECK(om_partition_open(&reg_mix, P_MIX_WIDE, &h) == OM_OK, "open for cached-field guard");
    FlashDev *keptDev = h.dev;
    h.dev = NULL;
    CHECK(om_partition_read(&h, 0u, pat, 1u) == OM_ERR_INVALID_ARG,
          "NULL cached dev rejected");
    CHECK(om_partition_dev_geom(&h) == NULL, "dev geom query rejected too");
    h.dev = keptDev;
    CHECK(om_partition_read(&h, 0u, pat, 1u) == OM_OK, "handle restored");
}

/* ===================================================================
 * T9: 排他性——分区层不持锁，排他由器件层承担
 *
 * 两个线程各持一个句柄、各占同一物理片上的一块分区，同时做擦→写→读校验：
 * 分区窗口不同，后端却是同一个器件。若分区层自行其是（或器件层不互斥），
 * 两线程的后端调用就会交错。判据：后端并发度峰值恒为 1，且各自分区内容
 * 不被对方破坏。
 * =================================================================== */

typedef struct
{
    const char *name; /* 分区名（两线程取不同分区） */
    uint8_t pattern;  /* 本线程自己的写入模式 */
    uint32_t rounds;
    uint32_t fails;
} ExcArg;

static void exclusion_worker(void *arg)
{
    ExcArg *a = (ExcArg *)arg;
    OmPartitionHandle h = {0};
    if (om_partition_open(&reg_good, a->name, &h) != OM_OK)
    {
        a->fails++;
        return;
    }
    uint8_t wbuf[64];
    uint8_t rbuf[64];
    memset(wbuf, a->pattern, sizeof(wbuf));
    for (uint32_t r = 0; r < a->rounds; r++)
    {
        if (om_partition_erase(&h) != OM_OK)
        {
            a->fails++;
            break;
        }
        if (om_partition_write(&h, 0u, wbuf, sizeof(wbuf)) != OM_OK)
        {
            a->fails++;
            break;
        }
        memset(rbuf, 0x00, sizeof(rbuf));
        if (om_partition_read(&h, 0u, rbuf, sizeof(rbuf)) != OM_OK)
        {
            a->fails++;
            break;
        }
        if (memcmp(wbuf, rbuf, sizeof(wbuf)) != 0)
        {
            a->fails++;
        }
    }
}

static void test_exclusion(void)
{
    printf("[T9] exclusivity (device-level, across partition handles)\n");

    /* 4KB 分区：擦除带擦后校验，后端调用次数足够多，交错窗口够宽 */
    flash_sim_set_delay(&g_flash_sim, 1u);
    g_inBackend = 0;
    g_maxInBackend = 0;

    ExcArg a1 = {P_BOOT, 0x11u, 2u, 0u};
    ExcArg a2 = {P_META, 0x22u, 2u, 0u};
    OsalThread *t1 = NULL;
    OsalThread *t2 = NULL;
    OsalThreadAttr attr = {"exc", 2048u, OSAL_PRIO_NORMAL_BASE};
    CHECK(osal_thread_create(&t1, &attr, exclusion_worker, &a1) == OSAL_OK, "spawn worker boot");
    CHECK(osal_thread_create(&t2, &attr, exclusion_worker, &a2) == OSAL_OK, "spawn worker meta");
    (void)osal_thread_join(t1, OSAL_WAIT_FOREVER);
    (void)osal_thread_join(t2, OSAL_WAIT_FOREVER);
    flash_sim_set_delay(&g_flash_sim, 0u);

    CHECK(a1.fails == 0 && a2.fails == 0, "both partitions clean (no torn operation)");
    CHECK(g_maxInBackend == 1, "backend never entered concurrently across handles (peak=%ld)",
          g_maxInBackend);
    CHECK(g_inBackend == 0, "in-flight counter balanced");

    /* 各自分区内容归各自线程所有 */
    OmPartitionHandle h = {0};
    uint8_t b = 0;
    CHECK(om_partition_open(&reg_good, P_BOOT, &h) == OM_OK &&
              om_partition_read(&h, 0u, &b, 1u) == OM_OK && b == 0x11u,
          "boot holds its own pattern (%02X)", b);
    CHECK(om_partition_open(&reg_good, P_META, &h) == OM_OK &&
              om_partition_read(&h, 0u, &b, 1u) == OM_OK && b == 0x22u,
          "meta holds its own pattern (%02X)", b);
}

int main(void)
{
    printf("=== partition abstraction host test (v2) ===\n");

    flash_sim_init(&g_flash_sim, CAP, 0xFFu, 4u);
    CHECK(g_flash_sim.mem != NULL, "sim memory allocated");
    flash_sim_init(&g_mix_sim, CAP_MIX, 0xFFu, 4u);
    CHECK(g_mix_sim.mem != NULL, "non-uniform sim memory allocated");

    test_registry_defenses(); /* 内含按防线顺序的器件注册点 */
    test_query();
    test_io_boundary();
    test_erase_and_misconfig();
    test_multi_registry_and_handle();
    test_ram_sourced_registry();
    test_erase_range();
    test_malformed_entries();
    test_capability_queries();
    test_exclusion(); /* 置于末尾：改动 boot/meta 内容，不影响前序用例管线 */

    flash_sim_deinit(&g_flash_sim);
    flash_sim_deinit(&g_mix_sim);

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

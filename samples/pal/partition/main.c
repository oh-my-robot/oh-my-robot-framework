/**
 * @file main.c
 * @brief 分区表抽象 v2 真机验证（rm-a/F427：真实几何 / 跨尺寸边界 / 真实通路）
 *
 * 覆盖 host 测试触不到的四项（验证线程 NORMAL 串行执行；心跳线程 HIGH 全程
 * 打点 = 长擦除期间 log 活性证据——128K 擦除 ms 级，让出式等待不应断流）：
 *   R1 真实非均匀扇区几何：每 bank 16K×4 / 64K / 128K×7、24 扇区；bank2 线性
 *      扇区 12..23 在适配器内映射为 SNB 16..27（+4）——真机几何表逐 region 打印
 *   R2 跨尺寸边界（16K→64K→128K）的分区判定：合法跨段 vs 终点落扇区中部；region
 *      表逐段消费分支（is_partition_sector_aligned 最易错路径）
 *   R3 真实 FlashDev 通路：XIP 读 / 逐字 program（适配器写后回读校验）/
 *      EOP 中断驱动擦除——全部经 partition 句柄入口
 *   R4 真实擦除耗时（ms 级）下 erase_range / 整分区 erase 的同步语义
 *
 * 安全性（照搬 samples/pal/flash/main.c 范式）：验证专用区 = bank2 尾 128K 扇区
 * [0x1E0000, 0x200000)（app 镜像只占低地址）；任何擦/写前先 blank 检查（读
 * 256B 全 erasedValue；读失败/几何不可得同样 fail-closed）；非空白或不可读 →
 * 跳过全部破坏性用例、按因报告，绝不擦。VERIFY_FORCE_CLEAN（编译期显式开启，
 * 默认关闭）提供中断残迹的自愈路径。表内区外条目（sizespan / crossseg）只
 * open/read，绝不写/擦。观测：串口（-DOM_LOG_SERIAL=1）。
 */

#include <string.h>

#include "core/om_init.h"
#include "drivers/model/device.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "drivers/peripheral/serial/log_serial_backend.h"
#include "drivers/storage/partition.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h"
#include "services/log/log.h"

#include "bsp_serial.h" /* BSP_LOG_SERIAL_NAME：板级日志口 */

#if defined(STM32F427xx)
#include "stm32f4xx_hal.h" /* R1：读 FLASH_OPTCR（DB1M/dual-bank 状态） */
#endif

OM_LOG_MODULE(log_part, OM_LOG_LEVEL_INFO);

static LogSerialBackend g_log_serial_backend;

#if OM_USE_LOG
/** @brief 串口日志后端接线（DRIVER 级注册板级日志口） */
static OmRet part_verify_log_port_init(void)
{
    return om_log_serial_backend_register(&g_log_serial_backend,
                                          device_find((char *)BSP_LOG_SERIAL_NAME), "serial",
                                          OM_LOG_LEVEL_INFO);
}
OM_INIT_DRIVER(part_verify_log_port_init);
#endif /* OM_USE_LOG */

static FlashDev *g_flash;
static int g_pass;
static int g_fail;

#define CHECK(cond, ...)                                                               \
    do                                                                                 \
    {                                                                                  \
        if (cond)                                                                      \
        {                                                                              \
            g_pass++;                                                                  \
            OM_LOG_INFO("  PASS: " __VA_ARGS__);                                       \
        }                                                                              \
        else                                                                           \
        {                                                                              \
            g_fail++;                                                                  \
            OM_LOG_ERROR("  FAIL: " __VA_ARGS__);                                      \
        }                                                                              \
        osal_sleep_ms(20); /* 输出节流：串口后端非阻塞提交（txFifo 满即静默截断）， */ \
        /* 爆发式输出会被通道吃掉——逐条让出，验证结果才完整 */                         \
    } while (0)

/* ===================================================================
 * 安全范式：验证专用区 + 分区表实例
 * =================================================================== */

/* 验证专用区（rm-a/F427 2MB）：bank2 尾 128K 扇区（线性 s23 @0x1E0000，适配器
 * SNB=27）——bank2 擦写不冻结 bank1 取指，与低地址 app 镜像隔离。全部破坏性
 * 操作只经 tail128 句柄，爆炸半径锁定在 blank 检查过的本扇区。 */
#define REG_TAIL 0x1E0000u
#define TAIL_SIZE 0x20000u
#define CAP_FULL 0x200000u

/* 分区表实例：static const + OM_PARTITION_REGISTRY 编译期常量——本身即 v2 的
 * 核心形态（免运行期注册、表可驻 ROM）。
 *
 * 分治（真机断言可归因）：
 *   good  = 全合法：R1/R3/R4 正例 + R2 合法跨尺寸条目（区外条目只读）
 *   bad   = 单条非扇区友好（128K 扇区的一半）→ 整表 fail-fast 拒绝
 *   mixed = 畸形条目前置 + 合法条目 → R2 反例（跨尺寸、终点落扇区中部），并证明
 *           open 逐条校验：命中项不因表中别处畸形被拒（open 非全表校验器） */
static const OmPartitionEntry g_table_good[] = {
    {"tail128", "flash0", REG_TAIL, TAIL_SIZE},  /* 恰一个 128K 扇区：唯一可擦写 */
    {"crossseg", "flash0", 0x1C0000u, 0x40000u}, /* s22+s23：与安全区后半重叠（只读） */
    {"sizespan", "flash0", 0x100000u, 0x40000u}, /* 16K×4+64K+128K：跨两处尺寸边界（只读） */
};
static const OmPartitionRegistry g_reg_good = OM_PARTITION_REGISTRY(g_table_good);

static const OmPartitionEntry g_table_bad[] = {
    {"misalign", "flash0", REG_TAIL, 0x10000u}, /* 128K 扇区的一半：非扇区友好 */
};
static const OmPartitionRegistry g_reg_bad = OM_PARTITION_REGISTRY(g_table_bad);

static const OmPartitionEntry g_table_mixed[] = {
    /* 同起点反例：0x30000 使终点停在 0x130000 = 128K 扇区 s17 [0x120000,0x140000)
     * 中部（比合法跨段 sizespan 的 0x40000 少 64K 半扇区） */
    {"span_short", "flash0", 0x100000u, 0x30000u},
    {"tail128", "flash0", REG_TAIL, TAIL_SIZE}, /* 合法：open 命中项 */
};
static const OmPartitionRegistry g_reg_mixed = OM_PARTITION_REGISTRY(g_table_mixed);

/** @brief 验证区是否空白（前 256B 全 erasedValue）
 *  @param addr    区域起始
 *  @param readRet 输出 flash_read 返回码（失败诊断用；可为 NULL）
 *  @retval true   空白（前 256B 全 erasedValue）
 *  @retval false  非空白，或读失败/几何不可得（几何 NULL 时无读取发生，
 *                 readRet 置 OM_ERR_INVALID_ARG）——fail-closed：不得据此动区
 *  @note 走 raw flash 读——不依赖被测的 partition 层 */
static bool part_is_blank(uint32_t addr, OmRet *readRet)
{
    static uint8_t buf[256];
    OmRet ret = OM_ERR_INVALID_ARG; /* 几何不可得：无读取结果可用 */
    const FlashGeometry *g = flash_geometry(g_flash);
    if (g)
    {
        ret = flash_read(g_flash, addr, buf, sizeof(buf));
    }
    if (readRet)
    {
        *readRet = ret;
    }
    if (!g || ret != OM_OK)
    {
        return false;
    }
    for (uint32_t i = 0; i < sizeof(buf); i++)
    {
        if (buf[i] != g->erasedValue)
        {
            return false;
        }
    }
    return true;
}

/* ===================================================================
 * R1: 真实非均匀扇区几何
 * =================================================================== */

static void verify_geometry(void)
{
    OM_LOG_INFO("--- R1 real non-uniform geometry ---");
    const FlashGeometry *g = flash_geometry(g_flash);
    if (!g)
    {
        OM_LOG_ERROR("geometry NULL");
        g_fail++;
        return;
    }
    OM_LOG_INFO("capacity=%u (0x%X) erased=%02X writeUnit=%u pageSize=%u sectorSize=%u",
                (unsigned)g->capacity, (unsigned)g->capacity, g->erasedValue,
                (unsigned)g->writeUnit, (unsigned)g->pageSize, (unsigned)g->sectorSize);

    /* 非均一几何 = region 表：逐 region 打印真机表（防御上限 8，容量覆盖即止） */
    uint32_t nreg = 0u;
    uint32_t sectors = 0u;
    uint32_t cover = 0u;
    for (uint32_t i = 0u; i < 8u; i++)
    {
        const FlashSectorRegion *r = &g->sectorRegions[i];
        if (r->size == 0u || r->count == 0u)
        {
            break; /* 非法/终止 region：不越过表尾 */
        }
        cover = r->offset + r->size * r->count;
        OM_LOG_INFO("region[%u]: off=0x%06X size=%uK count=%u end=0x%06X", (unsigned)nreg,
                    (unsigned)r->offset, (unsigned)(r->size / 1024u), (unsigned)r->count,
                    (unsigned)cover);
        sectors += r->count;
        nreg++;
        if (cover >= g->capacity)
        {
            break;
        }
    }
    OM_LOG_INFO("regions=%u sectors=%u coveredEnd=0x%X", (unsigned)nreg, (unsigned)sectors,
                (unsigned)cover);
    /* 线性扇区 → SNB 映射无公开查询面：bank2 线性 12..23 → SNB 16..27（F42x
     * SNB 12-15 保留）由安全区擦除（s23 → SNB 27）隐式验证——映射错即擦错扇区。
     * 该 +4 编码的前提是 2MB dual-bank 模式（DB1M=0，2×1MB）；DB1M=1（dual-bank
     * 1MB 变体）下此几何前提失效——故下行 OPTCR 须可读到 DB1M=0。本 sample 只
     * 打印不硬判：选项字节不匹配时安全区擦除会先失败，无需在此重复裁判 */
    OM_LOG_INFO("note: bank2 linear sector 12..23 -> SNB +4 (requires DB1M=0, 2MB dual-bank)");

#if defined(STM32F427xx)
    OM_LOG_INFO("OPTCR=0x%08X DB1M=%d (must be 0) nWRP=0x%03X", (unsigned)FLASH->OPTCR,
                ((FLASH->OPTCR & FLASH_OPTCR_DB1M) != 0u) ? 1 : 0,
                (unsigned)((FLASH->OPTCR & FLASH_OPTCR_nWRP_Msk) >> 16u));
#endif

    CHECK(g->sectorSize == 0u, "non-uniform geometry: sectorSize == 0 (region table)");
    CHECK(g->capacity == CAP_FULL, "capacity == 2MB (got %u)", (unsigned)g->capacity);
    CHECK(g->writeUnit == 4u, "writeUnit == 4 (word program)");
    CHECK(g->erasedValue == 0xFFu, "erasedValue == 0xFF");
    CHECK(nreg == 6u, "6 regions: per bank 16K*4 + 64K + 128K*7 (got %u)", (unsigned)nreg);
    CHECK(sectors == 24u, "24 sectors total (got %u)", (unsigned)sectors);
    CHECK(cover == g->capacity, "region table covers device exactly (end=0x%X)", (unsigned)cover);
}

/* ===================================================================
 * R2: 注册表面 + 跨尺寸边界判定
 * =================================================================== */

static void verify_registry_and_align(void)
{
    OM_LOG_INFO("--- R2 registry + size-boundary decisions ---");
    OmPartitionHandle h = {0};
    OmPartitionEntry e = {0};

    /* 全表合法（含跨尺寸跨段条目）→ validate 通过 */
    CHECK(om_partition_registry_validate(&g_reg_good) == OM_OK,
          "good table (incl. cross-size spans) passes full registry_validate");

    /* 非扇区友好（128K 扇区的一半）→ 整表 fail-fast 拒绝 */
    CHECK(om_partition_registry_validate(&g_reg_bad) == OM_ERR_INVALID_ARG,
          "half-128K entry rejects whole table (INVALID_ARG)");
    CHECK(om_partition_open(&g_reg_bad, "misalign", &h) == OM_ERR_INVALID_ARG,
          "half-128K entry rejected at open");

    /* R2 正例：sizespan 跨 16K×4→64K→128K，两端均扇区边界（region 表逐段消费） */
    CHECK(om_partition_open(&g_reg_good, "sizespan", &h) == OM_OK,
          "span across 16K->64K->128K size changes opens (both ends aligned)");
    CHECK(h.index == 2u, "span handle indexes the sizespan entry");

    /* R2 反例：同起点 0x100000、size 0x30000——终点 0x130000 落 128K 扇区 s17
     * [0x120000,0x140000) 中部（比合法跨段 sizespan 少 64K = 半个扇区） */
    CHECK(om_partition_open(&g_reg_mixed, "span_short", &h) == OM_ERR_INVALID_ARG,
          "span ending 64K into final 128K sector rejected at open");
    CHECK(om_partition_registry_validate(&g_reg_mixed) == OM_ERR_INVALID_ARG,
          "mixed table rejected by full registry_validate (fail-fast)");

    /* open 逐条语义：畸形条目前置，命中项仍 OK——open 不是全表校验器 */
    CHECK(om_partition_open(&g_reg_mixed, "tail128", &h) == OM_OK,
          "open hit OK despite malformed entry elsewhere (per-entry check)");

    /* 解析面：名字未命中 / 计数 / 枚举 / by-value 查询 */
    CHECK(om_partition_open(&g_reg_good, "no_such", &h) == OM_ERR_NOT_FOUND,
          "unknown name -> NOT_FOUND");
    CHECK(om_partition_registry_count(&g_reg_good) == 3u, "registry_count == 3");
    CHECK(om_partition_registry_at(&g_reg_good, 0u, &e) == OM_OK && strcmp(e.name, "tail128") == 0,
          "registry_at(0) == tail128");
    CHECK(om_partition_registry_at(&g_reg_good, 3u, &e) == OM_ERR_NOT_FOUND,
          "registry_at(count) -> NOT_FOUND");
    CHECK(om_partition_query(&g_reg_good, "crossseg", &e) == OM_OK && e.offset == 0x1C0000u &&
              e.size == 0x40000u,
          "query(crossseg) returns table values (pure info, no device)");
    CHECK(om_partition_query(&g_reg_good, "no_such", &e) == OM_ERR_NOT_FOUND,
          "query unknown -> NOT_FOUND");
}

/* ===================================================================
 * R3/R4: 真实数据通路 + 真实擦除耗时
 * =================================================================== */

static void verify_data_path(void)
{
    OM_LOG_INFO("--- R3/R4 data path + real erase latency ---");

#ifdef VERIFY_FORCE_CLEAN
    /* 恢复逃生门（编译期显式开启，默认关闭）：上次运行中断留下残迹时无条件擦净
     * 安全区自愈——不加 -DVERIFY_FORCE_CLEAN 则不生成，默认路径恒先 blank 检查 */
    OM_LOG_INFO("FORCE_CLEAN: unconditional tail erase before blank check");
    CHECK(flash_erase(g_flash, REG_TAIL, TAIL_SIZE) == OM_OK, "force clean tail");
#endif

    /* 安全闸：非空白/不可读即跳过全部破坏性用例（按因报告，绝不擦） */
    OmRet readRet = OM_OK;
    if (!part_is_blank(REG_TAIL, &readRet))
    {
        if (readRet != OM_OK)
        {
            OM_LOG_ERROR(
                "tail [0x%X,0x%X) NOT readable (ret=%d): destructive cases SKIPPED, nothing erased",
                (unsigned)REG_TAIL, (unsigned)(REG_TAIL + TAIL_SIZE), (int)readRet);
        }
        else
        {
            OM_LOG_ERROR("tail [0x%X,0x%X) NOT blank: destructive cases SKIPPED, nothing erased",
                         (unsigned)REG_TAIL, (unsigned)(REG_TAIL + TAIL_SIZE));
        }
        g_fail++;
        return;
    }

    OmPartitionHandle h = {0};
    CHECK(om_partition_open(&g_reg_good, "tail128", &h) == OM_OK, "open tail128");
    CHECK(h.reg == &g_reg_good && h.index == 0u, "handle binds registry + entry index");

    /* R4: erase_range 整扇区——真实 ms 级（EOP 中断 + worker 让出等待） */
    uint32_t t0 = (uint32_t)osal_time_now_monotonic();
    CHECK(om_partition_erase_range(&h, 0u, TAIL_SIZE) == OM_OK, "erase_range whole 128K sector");
    uint32_t t1 = (uint32_t)osal_time_now_monotonic();
    OM_LOG_INFO("erase_range 128K took %u ms (R4 real erase latency)", (unsigned)(t1 - t0));

    /* R3: 逐字 program（1KB 字对齐）+ XIP 回读（适配器内部写后回读校验） */
    static uint8_t w[0x400];
    static uint8_t r[0x400];
    for (uint32_t i = 0; i < sizeof(w); i++)
    {
        w[i] = (uint8_t)(0xA5u ^ (i & 0xFFu));
    }
    uint32_t w0 = (uint32_t)osal_time_now_monotonic();
    CHECK(om_partition_write(&h, 0u, w, sizeof(w)) == OM_OK, "write 1KB via handle (word program)");
    uint32_t w1 = (uint32_t)osal_time_now_monotonic();
    OM_LOG_INFO("write 1KB took %u ms", (unsigned)(w1 - w0));

    memset(r, 0, sizeof(r));
    CHECK(om_partition_read(&h, 0u, r, sizeof(r)) == OM_OK, "read back 1KB via handle (XIP)");
    CHECK(memcmp(w, r, sizeof(w)) == 0, "1KB content matches (program + XIP read path)");
    if (memcmp(w, r, sizeof(w)) != 0)
    {
        OM_LOG_INFO("  dbg 1KB: r[0]=%02X w[0]=%02X r[last]=%02X w[last]=%02X", r[0], w[0],
                    r[sizeof(r) - 1u], w[sizeof(w) - 1u]);
    }

    /* 第二块（不同偏移 64K）：分区内偏移映射不只在 0 处成立 */
    static uint8_t w2[64];
    static uint8_t r2[64];
    for (uint32_t i = 0; i < sizeof(w2); i++)
    {
        w2[i] = (uint8_t)(0x5Au + i);
    }
    CHECK(om_partition_write(&h, 0x10000u, w2, sizeof(w2)) == OM_OK, "write 64B @0x10000");
    memset(r2, 0, sizeof(r2));
    CHECK(om_partition_read(&h, 0x10000u, r2, sizeof(r2)) == OM_OK, "read back 64B @0x10000");
    CHECK(memcmp(w2, r2, sizeof(w2)) == 0, "second block matches");

    /* 交叉句柄：crossseg 偏移 0x20000 与 tail128 偏移 0 为同一物理字节
     * （分区层只做 offset 换算——两条目互证，纯读无破坏） */
    OmPartitionHandle hc = {0};
    static uint8_t ra[64];
    static uint8_t rb[64];
    CHECK(om_partition_open(&g_reg_good, "crossseg", &hc) == OM_OK, "open crossseg (read-only)");
    memset(ra, 0, sizeof(ra));
    memset(rb, 0, sizeof(rb));
    CHECK(om_partition_read(&hc, REG_TAIL - 0x1C0000u, ra, sizeof(ra)) == OM_OK,
          "read via crossseg at overlap offset 0x20000");
    CHECK(om_partition_read(&h, 0u, rb, sizeof(rb)) == OM_OK, "read via tail128 at offset 0");
    CHECK(memcmp(ra, rb, sizeof(ra)) == 0, "two handles over same bytes agree");

    /* 直接器件读等价（证明句柄路径 = 分区偏移 + 设备偏移） */
    static uint8_t rd[64];
    memset(rd, 0, sizeof(rd));
    CHECK(flash_read(g_flash, REG_TAIL + 0x10000u, rd, sizeof(rd)) == OM_OK,
          "direct flash_read at tail+0x10000");
    CHECK(memcmp(rd, r2, sizeof(rd)) == 0, "handle read == direct device read");

    /* 契约边界（全部在触达器件前拒绝，无破坏）。两类码刻意分开：
     * 越界 = 调用方请求越出可访问范围 → RANGE（改偏移即可）；
     * 未对齐/空参 = 参数本身不成立 → INVALID_ARG */
    CHECK(om_partition_read(&h, TAIL_SIZE, r2, 4u) == OM_ERR_RANGE, "read off==size rejected (RANGE)");
    CHECK(om_partition_read(&h, 0u, r2, TAIL_SIZE + 1u) == OM_ERR_RANGE,
          "read len crossing partition end rejected (RANGE)");
    CHECK(om_partition_read(&h, 0u, NULL, 0u) == OM_ERR_INVALID_ARG, "read NULL buf rejected");
    CHECK(om_partition_write(&h, TAIL_SIZE, w2, 4u) == OM_ERR_RANGE, "write off==size rejected (RANGE)");
    CHECK(om_partition_write(&h, 0u, w2, 2u) == OM_ERR_INVALID_ARG,
          "write len 2 (non-word) rejected by device layer");
    CHECK(om_partition_write(&h, 2u, w2, 4u) == OM_ERR_INVALID_ARG,
          "write off 2 (non-word) rejected by device layer");
    CHECK(om_partition_erase_range(&h, 0u, 0x400u) == OM_ERR_INVALID_ARG,
          "erase_range 1KB (non-sector) rejected by device layer");
    CHECK(om_partition_erase_range(&h, 0u, 0x10000u) == OM_ERR_INVALID_ARG,
          "erase_range half-128K sector rejected (no silent expand)");
    CHECK(om_partition_erase_range(&h, 1u, TAIL_SIZE) == OM_ERR_RANGE,
          "erase_range off 1 (off+len crosses end) rejected as RANGE");
    CHECK(om_partition_erase_range(&h, 0u, 0u) == OM_OK, "erase_range len==0 no-op");
    CHECK(om_partition_read(&h, 0u, r2, 0u) == OM_OK, "read len==0 in-domain no-op");
    CHECK(om_partition_read(&h, TAIL_SIZE, r2, 0u) == OM_ERR_RANGE,
          "read len==0 still checks off domain (RANGE)");

    /* 句柄信任模型：域校验拒绝伪造/损坏句柄（index >= count） */
    OmPartitionHandle forged = {&g_reg_good, 99u};
    CHECK(om_partition_read(&forged, 0u, r2, 4u) == OM_ERR_INVALID_ARG,
          "forged handle (index >= count) rejected");
    CHECK(om_partition_read(NULL, 0u, r2, 4u) == OM_ERR_INVALID_ARG, "NULL handle rejected");

    /* 复原：整分区 erase，留空白给下次运行 */
    uint32_t e0 = (uint32_t)osal_time_now_monotonic();
    CHECK(om_partition_erase(&h) == OM_OK, "om_partition_erase whole partition");
    uint32_t e1 = (uint32_t)osal_time_now_monotonic();
    OM_LOG_INFO("om_partition_erase 128K took %u ms (R4 second entry)", (unsigned)(e1 - e0));
    CHECK(part_is_blank(REG_TAIL, NULL), "tail blank after restore erase");
}

/* ===================================================================
 * 验证线程入口（APPLICATION：建心跳与验证线程）
 * =================================================================== */

static void part_verify_heartbeat(void *arg)
{
    int i = 0;
    (void)arg;
    for (;;)
    {
        OM_LOG_INFO("hb %d t=%u", i++, (unsigned)osal_time_now_monotonic());
        osal_sleep_ms(200);
    }
}

static void part_verify_thread(void *arg)
{
    (void)arg;
    OM_LOG_INFO("=== partition v2 verify start ===");
    g_flash = flash_find("flash0");
    CHECK(g_flash != NULL, "flash_find(\"flash0\")");
    if (!g_flash)
    {
        OM_LOG_INFO("=== partition v2 verify: %d passed, %d failed ===", g_pass, g_fail);
        for (;;)
        {
            osal_sleep_ms(60000); /* FreeRTOS 任务不得返回：挂起 */
        }
    }

    verify_geometry();
    osal_sleep_ms(150);
    verify_registry_and_align();
    osal_sleep_ms(150);
    verify_data_path();
    osal_sleep_ms(150);

    OM_LOG_INFO("=== partition v2 verify: %d passed, %d failed ===", g_pass, g_fail);
    for (;;)
    {
        osal_sleep_ms(60000); /* FreeRTOS 任务不得返回：挂起 */
    }
}

static OmRet part_verify_main(void)
{
    OsalThread *vthread = NULL;
    OsalThread *hthread = NULL;
    OsalThreadAttr vattr = {"part_vfy", 2048u, OSAL_PRIO_NORMAL_BASE};
    OsalThreadAttr hattr = {"part_hb", 2048u, OSAL_PRIO_HIGH_BASE};

    (void)osal_thread_create(&vthread, &vattr, part_verify_thread, NULL);
    (void)osal_thread_create(&hthread, &hattr, part_verify_heartbeat, NULL);
    return OM_OK;
}
OM_INIT_APPLICATION(part_verify_main);

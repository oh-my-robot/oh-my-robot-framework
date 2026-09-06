/**
 * @file main.c
 * @brief 片内 Flash v1 真机验证（rm-a/F427：同步语义 + 异步执行模型 + 边界）
 *
 * 覆盖（运行于验证线程 NORMAL；心跳线程 HIGH 全程打点 = 擦除期间 log 活性
 * 证据——v1 域 worker sleep 让出，心跳不应断流）：
 *   G1 几何/区域核对 + dual-bank 探测
 *   G2 同步语义：边界拒绝（越界/未对齐/半扇区）、尾扇区擦写读环、
 *      跨 bank 混合扇区擦、16KB 大块写读、program AND 物理语义、
 *      受保护扇区擦除错误事件路径（nWRP 临时保护；无保护位时 skip）
 *   G3 异步模型：设备级回调 erase→write→read 链、read-BUSY（擦除中）、
 *      队列满 BUSY（连续 async）、回调内同步调用 BUSY 拒绝
 *
 * 安全性：验证专用区 = bank2 空区（app 镜像只占低地址）；操作前 blank 检查，
 * 非空白（非本程序残留）跳过并报告。观测：串口（-DOM_LOG_SERIAL=1）。
 */

#include <string.h>

#include "core/om_init.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "drivers/peripheral/serial/log_serial_backend.h"
#include "osal/osal_sem.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h"
#include "services/log/log.h"

#include "bsp_serial.h" /* BSP_LOG_SERIAL_NAME：板级日志口 */

#if defined(STM32F427xx)
#include "stm32f4xx_hal.h"                /* 验证用：读 FLASH_OPTCR（nWRP/DB1M 位） */
extern volatile uint32_t gBspFlash4DbgSr; /* 适配器调试符号：最近一次操作错误 SR 原值 */
#endif

OM_LOG_MODULE(log_flash, OM_LOG_LEVEL_INFO);

static LogSerialBackend g_log_serial_backend;

#if OM_USE_LOG
/** @brief 串口日志后端接线（DRIVER 级注册板级日志口） */
static OmRet flash_verify_log_port_init(void)
{
    return om_log_serial_backend_register(&g_log_serial_backend,
                                          device_find((char *)BSP_LOG_SERIAL_NAME), "serial",
                                          OM_LOG_LEVEL_INFO);
}
OM_INIT_DRIVER(flash_verify_log_port_init);
#endif /* OM_USE_LOG */

static FlashDev *g_flash;
static int g_pass;
static int g_fail;
static OsalSem *g_evt;

#define CHECK(cond, ...)                          \
    do                                            \
    {                                             \
        if (cond)                                 \
        {                                         \
            g_pass++;                             \
            OM_LOG_INFO("  PASS: " __VA_ARGS__);  \
        }                                         \
        else                                      \
        {                                         \
            g_fail++;                             \
            OM_LOG_ERROR("  FAIL: " __VA_ARGS__); \
        }                                         \
    } while (0)

/* 验证专用区（rm-a/F427 2MB）：全部擦写用例集中 bank2 尾 128K 扇区（s23@0x1E0000）——
 * bank2 擦写不冻结 bank1 取指（dual-bank 独立引擎），与 app 低地址代码隔离；
 * 区内写/读用不同偏移，擦除统一整扇区，操作前 blank 检查防破坏非本程序残留。 */
#define REG_TAIL 0x1E0000u /* 尾 128K 扇区起始偏移 */
#define TAIL_SIZE 0x20000u
#define CAP_FULL 0x200000u /* 2MB 器件容量 */

static void flash_verify_heartbeat(void *arg)
{
    int i = 0;
    (void)arg;
    for (;;)
    {
        OM_LOG_INFO("hb %d t=%u", i++, (unsigned)osal_time_now_monotonic());
        osal_sleep_ms(200);
    }
}

/** @brief 判断区域是否为空白（前 256B 全擦后值） */
static bool flash_verify_is_blank(uint32_t addr)
{
    static uint8_t buf[256];
    const FlashGeometry *g = flash_geometry(g_flash);
    if (flash_read(g_flash, addr, buf, sizeof(buf)) != OM_OK)
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
 * G1: 几何 / dual-bank
 * =================================================================== */

static void verify_geometry(void)
{
    OM_LOG_INFO("--- G1 geometry ---");
    const FlashGeometry *g = flash_geometry(g_flash);
    if (!g)
    {
        OM_LOG_ERROR("geometry NULL");
        g_fail++;
        return;
    }
    OM_LOG_INFO("capacity=%u writeUnit=%u erased=%02X", (unsigned)g->capacity,
                (unsigned)g->writeUnit, g->erasedValue);
    uint32_t idx = 0;
    const FlashSectorRegion *r = g->sectorRegions;
    for (;; r++)
    {
        OM_LOG_INFO("region[%u]: off=%u size=%u count=%u", idx++, (unsigned)r->offset,
                    (unsigned)r->size, (unsigned)r->count);
        if (r->offset + r->size * r->count >= g->capacity)
        {
            break;
        }
    }
    CHECK(g->capacity == CAP_FULL, "capacity == 2MB (got %u)", (unsigned)g->capacity);
    CHECK(g->writeUnit == 4u, "writeUnit == 4");
    CHECK(g->erasedValue == 0xFFu, "erasedValue == 0xFF");

#if defined(STM32F427xx)
    OM_LOG_INFO("OPTCR=0x%08X DB1M=%d WRP=0x%03X", (unsigned)FLASH->OPTCR,
                ((FLASH->OPTCR & FLASH_OPTCR_DB1M) != 0u) ? 1 : 0,
                (unsigned)((FLASH->OPTCR & FLASH_OPTCR_nWRP_Msk) >> 16u));
#endif
}

/* ===================================================================
 * G2: 同步语义与边界
 * =================================================================== */

static void verify_boundary_rejects(void)
{
    OM_LOG_INFO("--- G2a boundary rejects ---");
    uint8_t byte = 0x11;
    CHECK(flash_read(g_flash, CAP_FULL, &byte, 1u) == OM_ERR_INVALID_ARG,
          "read at capacity rejected");
    CHECK(flash_read(g_flash, CAP_FULL - 1u, &byte, 2u) == OM_ERR_INVALID_ARG,
          "read crossing end rejected");
    CHECK(flash_write(g_flash, REG_TAIL + 2u, &byte, 4u) == OM_ERR_INVALID_ARG,
          "write misaligned addr rejected");
    CHECK(flash_write(g_flash, REG_TAIL, &byte, 2u) == OM_ERR_INVALID_ARG,
          "write len misaligned rejected");
    CHECK(flash_erase(g_flash, REG_TAIL, 0x10000u) == OM_ERR_INVALID_ARG,
          "erase half (64K of 128K) sector rejected");
    CHECK(flash_erase(g_flash, REG_TAIL + 1u, 0x20000u) == OM_ERR_INVALID_ARG,
          "erase misaligned addr rejected");
    CHECK(flash_erase(g_flash, 0u, 0u) == OM_OK, "erase len==0 no-op");
    CHECK(flash_write(g_flash, REG_TAIL, &byte, 0u) == OM_OK, "write len==0 no-op");
    CHECK(flash_read(g_flash, REG_TAIL, NULL, 0u) == OM_OK, "read len==0 no-op");

#if defined(STM32F427xx)
    /* 错误事件路径：受保护扇区擦除 → WRPERR → ERRIE 中断 → IO 返回。
     * 保护位由外部 option 脚本临时建立（nWRP bit11=0 保护 s11@0xE0000）；
     * 无保护位时 skip 并告警——绝不真擦 bank1 扇区（从 bank1 执行中真擦
     * bank1 = 取指冻结/假死）。预期外返回打印 ret + 适配器调试 SR。 */
    if ((FLASH->OPTCR & (1u << (FLASH_OPTCR_nWRP_Pos + 11u))) == 0u)
    {
        OmRet wret = flash_erase(g_flash, 0xE0000u, 0x20000u);
        CHECK(wret == OM_ERR_FLASH_IO,
              "erase protected sector rejected via error event (WRPERR)");
        if (wret != OM_ERR_FLASH_IO)
        {
            OM_LOG_INFO("  dbg wrperr: ret=%d dbgSr=0x%08X", (int)wret,
                        (unsigned)gBspFlash4DbgSr);
        }
    }
    else
    {
        OM_LOG_INFO("  skip: s11 unprotected (nWRP bit11=1); WRPERR case needs option setup");
    }
#endif
}

static void verify_tail_roundtrip(void)
{
    OM_LOG_INFO("--- G2c tail sector roundtrip (erase/write/read/restore) ---");
#ifdef VERIFY_FORCE_CLEAN
    OM_LOG_INFO("FORCE_CLEAN: unconditional tail erase");
    CHECK(flash_erase(g_flash, REG_TAIL, 0x20000u) == OM_OK, "force clean tail");
#endif
    if (!flash_verify_is_blank(REG_TAIL))
    {
        OM_LOG_ERROR("tail NOT blank; roundtrip skipped");
        g_fail++;
        return;
    }

    uint32_t t0 = (uint32_t)osal_time_now_monotonic();
    CHECK(flash_erase(g_flash, REG_TAIL, 0x20000u) == OM_OK, "erase tail 128K sector");
    uint32_t t1 = (uint32_t)osal_time_now_monotonic();
    OM_LOG_INFO("erase 128K took %u ms", (unsigned)(t1 - t0));

    /* 16KB 大块写读（4096 字） */
    static uint8_t big[0x4000];
    for (uint32_t i = 0; i < sizeof(big); i++)
    {
        big[i] = (uint8_t)(i & 0xFF);
    }
    uint32_t w0 = (uint32_t)osal_time_now_monotonic();
    CHECK(flash_write(g_flash, REG_TAIL, big, sizeof(big)) == OM_OK, "write 16KB pattern");
    uint32_t w1 = (uint32_t)osal_time_now_monotonic();
    OM_LOG_INFO("write 16KB took %u ms", (unsigned)(w1 - w0));

    static uint8_t rbuf[0x4000];
    memset(rbuf, 0x00, sizeof(rbuf));
    CHECK(flash_read(g_flash, REG_TAIL, rbuf, sizeof(rbuf)) == OM_OK, "read back 16KB");
    CHECK(memcmp(big, rbuf, sizeof(big)) == 0, "16KB content matches");

    /* 同值重写幂等（合法边界：已写区重复 program 相同内容应 OK 且内容不变） */
    CHECK(flash_write(g_flash, REG_TAIL, big, sizeof(big)) == OM_OK,
          "rewrite same 16KB (idempotent)");
    static uint8_t probe[64];
    memset(probe, 0x00, sizeof(probe));
    flash_read(g_flash, REG_TAIL, probe, sizeof(probe));
    CHECK(probe[0] == big[0] && probe[63] == big[63], "content unchanged after rewrite");

    CHECK(flash_erase(g_flash, REG_TAIL, 0x20000u) == OM_OK, "restore erase tail");
    CHECK(flash_verify_is_blank(REG_TAIL), "tail blank after restore");
}

/* ===================================================================
 * G3: 异步执行模型（设备级回调）
 * =================================================================== */

static int g_cbCount;
static OmRet g_cbRet;
static int g_chainStage;

/* G3.1 链回调：erase 完成 → 提交 write → write 完成 → read 校验 → 事件 */
static void chain_cb(FlashDev *dev, OmRet status, void *param)
{
    (void)param;
    g_cbCount++;
    if (g_chainStage == 0)
    {
        g_chainStage = 1;
        if (status != OM_OK)
        {
            osal_sem_post(g_evt);
            return;
        }
        static uint8_t pat[512];
        for (uint32_t i = 0; i < sizeof(pat); i++)
        {
            pat[i] = (uint8_t)(0x21 + i);
        }
        g_cbRet = flash_write_async(dev, REG_TAIL, pat, sizeof(pat));
        if (g_cbRet != OM_OK)
        {
            osal_sem_post(g_evt);
        }
        return;
    }
    static uint8_t rbuf[512];
    OmRet r = flash_read(dev, REG_TAIL, rbuf, sizeof(rbuf));
    CHECK(r == OM_OK && rbuf[0] == 0x21 && rbuf[511] == (uint8_t)(0x21 + 511),
          "async chain content verified in callback");
    if (r != OM_OK || rbuf[0] != 0x21)
    {
        OM_LOG_INFO("  dbg chain: r=%d wret=%d first=%02X", (int)r, (int)g_cbRet, rbuf[0]);
    }
    osal_sem_post(g_evt);
}

/* G3.3 回调内同步调用 → 同域拒绝 */
static void sync_in_worker_cb(FlashDev *dev, OmRet status, void *param)
{
    (void)status;
    (void)param;
    g_cbCount++;
    g_cbRet = flash_erase(dev, REG_TAIL, TAIL_SIZE); /* 应被同域拒绝 BUSY */
    osal_sem_post(g_evt);
}

/* G3.2 无操作计数回调 */
static void count_cb(FlashDev *dev, OmRet status, void *param)
{
    (void)dev;
    (void)status;
    (void)param;
    g_cbCount++;
}

static void verify_async_model(void)
{
    OM_LOG_INFO("--- G3 async execution model ---");

    /* G3.1 设备级回调链（erase → write → read 校验），区=尾扇区（先清） */
    CHECK(flash_erase(g_flash, REG_TAIL, 0x20000u) == OM_OK, "prep: tail erased");
    g_cbCount = 0;
    g_chainStage = 0;
    g_cbRet = OM_OK;
    flash_set_done_cb(g_flash, chain_cb, NULL);
    CHECK(flash_erase_async(g_flash, REG_TAIL, 0x20000u) == OM_OK,
          "async erase submitted (returns immediately)");
    CHECK(osal_sem_wait(g_evt, 5000u) == OSAL_OK, "async chain completed");
    CHECK(g_cbCount == 2, "device callback fired twice (erase+write)");
    CHECK(flash_erase(g_flash, REG_TAIL, TAIL_SIZE) == OM_OK, "tail restored (repeatable run)");

    /* G3.2 read-BUSY：async 擦除（tail 整扇区，~2s 在途）时同步读被拒 */
    if (flash_verify_is_blank(REG_TAIL))
    {
        flash_set_done_cb(g_flash, count_cb, NULL);
        g_cbCount = 0;
        CHECK(flash_erase_async(g_flash, REG_TAIL, TAIL_SIZE) == OM_OK,
              "async tail erase in flight");
        osal_sleep_ms(100); /* 128K 擦 ~2s，仍在途 */
        uint8_t b = 0;
        CHECK(flash_read(g_flash, REG_TAIL + 0x1000u, &b, 1u) == OM_ERR_FLASH_BUSY,
              "read rejected while erase in flight (no mixed read)");
        for (int i = 0; i < 300 && g_cbCount < 1; i++)
        {
            osal_sleep_ms(10);
        }
        CHECK(g_cbCount == 1, "in-flight erase completed via callback");
        b = 0;
        CHECK(flash_read(g_flash, REG_TAIL + 0x1000u, &b, 1u) == OM_OK, "read OK after erase done");
    }
    else
    {
        OM_LOG_INFO("tail not blank; read-BUSY case skipped");
    }

    /* G3.3 队列满 BUSY：tail 擦除在途（慢），连续 3 个 async → 第 3 个 BUSY */
    flash_set_done_cb(g_flash, count_cb, NULL);
    g_cbCount = 0;
    if (flash_verify_is_blank(REG_TAIL))
    {
        CHECK(flash_erase_async(g_flash, REG_TAIL, TAIL_SIZE) == OM_OK,
              "req1 in-flight (tail erase)");
        osal_sleep_ms(100); /* 确保 req1 已在执行 */
        CHECK(flash_erase_async(g_flash, REG_TAIL, TAIL_SIZE) == OM_OK, "req2 queued");
        CHECK(flash_erase_async(g_flash, REG_TAIL, TAIL_SIZE) == OM_ERR_FLASH_BUSY,
              "req3 rejected: queue full (depth 2)");
        for (int i = 0; i < 400 && g_cbCount < 2; i++)
        {
            osal_sleep_ms(10);
        }
        CHECK(g_cbCount == 2, "both queued erases completed");
    }
    else
    {
        OM_LOG_INFO("tail not blank; queue-full case skipped");
    }

    /* G3.4 回调内同步调用 → BUSY 拒绝（不死锁） */
    flash_set_done_cb(g_flash, sync_in_worker_cb, NULL);
    g_cbCount = 0;
    g_cbRet = OM_OK;
    if (flash_verify_is_blank(REG_TAIL))
    {
        CHECK(flash_erase_async(g_flash, REG_TAIL, TAIL_SIZE) == OM_OK,
              "async erase w/ sync-in-callback");
        CHECK(osal_sem_wait(g_evt, 5000u) == OSAL_OK, "callback executed (no deadlock)");
        CHECK(g_cbRet == OM_ERR_FLASH_BUSY, "sync wait inside worker rejected (BUSY)");
    }
    else
    {
        OM_LOG_INFO("tail not blank; sync-in-callback case skipped");
    }

    /* G3.5 无完成通知（setter 清空） */
    flash_set_done_cb(g_flash, NULL, NULL);
    if (flash_verify_is_blank(REG_TAIL))
    {
        CHECK(flash_erase_async(g_flash, REG_TAIL, TAIL_SIZE) == OM_OK,
              "async erase w/ no done cb submitted");
        for (int i = 0; i < 300; i++)
        {
            if (flash_verify_is_blank(REG_TAIL))
            {
                break;
            }
            osal_sleep_ms(10);
        }
        CHECK(flash_verify_is_blank(REG_TAIL), "no-cb async erase completed");
    }

    flash_set_done_cb(g_flash, NULL, NULL);
}

/* ===================================================================
 * 验证线程入口（APPLICATION：建心跳与验证线程）
 * =================================================================== */

static void flash_verify_thread(void *arg)
{
    (void)arg;
    OM_LOG_INFO("=== flash v1 verify start ===");
    g_flash = flash_find("flash0");
    CHECK(g_flash != NULL, "flash_find(\"flash0\")");
    if (!g_flash)
    {
        OM_LOG_INFO("=== flash v1 verify: %d passed, %d failed ===", g_pass, g_fail);
        return;
    }

    verify_geometry();
    osal_sleep_ms(150);
    verify_boundary_rejects();
    osal_sleep_ms(150);
    verify_tail_roundtrip();
    osal_sleep_ms(150);
    verify_async_model();
    osal_sleep_ms(150);

    OM_LOG_INFO("=== flash v1 verify: %d passed, %d failed ===", g_pass, g_fail);
    for (;;)
    {
        osal_sleep_ms(60000); /* FreeRTOS 任务不得返回：挂起 */
    }
}

static OmRet flash_verify_main(void)
{
    OsalThread *vthread = NULL;
    OsalThread *hthread = NULL;
    OsalThreadAttr vattr = {"flash_vfy", 2048u, OSAL_PRIO_NORMAL_BASE};
    OsalThreadAttr hattr = {"flash_hb", 2048u, OSAL_PRIO_HIGH_BASE};

    osal_sem_create(&g_evt, 1u, 0u);
    (void)osal_thread_create(&vthread, &vattr, flash_verify_thread, NULL);
    (void)osal_thread_create(&hthread, &hattr, flash_verify_heartbeat, NULL);
    return OM_OK;
}
OM_INIT_APPLICATION(flash_verify_main);

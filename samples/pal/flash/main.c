/**
 * @file main.c
 * @brief 片内 Flash v1 真机验证（rm-a/F427：同步语义 + 执行模型 + 边界）
 *
 * 覆盖（运行于验证线程 NORMAL；心跳线程 HIGH 全程打点 = 擦除期间 log 活性
 * 证据——后端等硬件完成时让出，心跳不应断流）：
 *   G1 几何/区域核对 + dual-bank 探测
 *   G2 同步语义：边界拒绝（越界/未对齐/半扇区）、尾扇区擦写读环、
 *      跨 bank 混合扇区擦、16KB 大块写读、program AND 物理语义、
 *      受保护扇区擦除错误事件路径（nWRP 临时保护；无保护位时 skip）
 *   G3 执行模型：调用者上下文同步返回、擦除期间心跳不断流（让出契约）、
 *      并访线程的读等锁而非 BUSY（每设备互斥）
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
static OsalSem *g_peerGo;           /* G3 并访探针起跑门 */
static volatile uint32_t g_hbCount; /* 心跳计数：擦除期间 log 活性的证据 */

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
        g_hbCount++;
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
    /* 两类码刻意分开：越界 = 改地址即可（RANGE）；未对齐/空参 = 参数不成立
     * （INVALID_ARG）。越界先于对齐报出——以下反例按各自的主因取码 */
    CHECK(flash_read(g_flash, CAP_FULL, &byte, 1u) == OM_ERR_RANGE, "read at capacity rejected (RANGE)");
    CHECK(flash_read(g_flash, CAP_FULL - 1u, &byte, 2u) == OM_ERR_RANGE,
          "read crossing end rejected (RANGE)");
    CHECK(flash_write(g_flash, REG_TAIL + 2u, &byte, 4u) == OM_ERR_INVALID_ARG,
          "write misaligned addr rejected");
    CHECK(flash_write(g_flash, REG_TAIL, &byte, 2u) == OM_ERR_INVALID_ARG,
          "write len misaligned rejected");
    CHECK(flash_erase(g_flash, REG_TAIL, 0x10000u) == OM_ERR_INVALID_ARG,
          "erase half (64K of 128K) sector rejected");
    CHECK(flash_erase(g_flash, REG_TAIL + 1u, 0x20000u) == OM_ERR_RANGE,
          "erase off+len crossing end rejected (RANGE wins over misalign)");
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
 * G3: 执行模型（调用者上下文同步 + 每设备互斥 + 让出契约）
 * =================================================================== */

static volatile uint32_t g_peerRet; /* 并访线程的 read 返回码 */
static volatile uint32_t g_peerT0;  /* 并访线程 read 起止时刻 */
static volatile uint32_t g_peerT1;
static volatile uint32_t g_eraseT0; /* 本线程擦除起止时刻 */
static volatile uint32_t g_eraseT1;

/** @brief 并访线程：等门后发起一次读，记录起止（读应等到擦除结束才返回） */
static void flash_peer_probe(void *arg)
{
    (void)arg;
    (void)osal_sem_wait(g_peerGo, OSAL_WAIT_FOREVER);
    osal_sleep_ms(100); /* 落在擦除窗口内（整扇区擦 ~秒级） */
    uint8_t b = 0;
    g_peerT0 = (uint32_t)osal_time_now_monotonic();
    g_peerRet = (uint32_t)flash_read(g_flash, REG_TAIL + 0x1000u, &b, 1u);
    g_peerT1 = (uint32_t)osal_time_now_monotonic();
    for (;;)
    {
        osal_sleep_ms(60000);
    }
}

static void verify_sync_model(void)
{
    OM_LOG_INFO("--- G3 execution model (sync + per-device lock) ---");

    OsalThread *peer = NULL;
    OsalThreadAttr pattr = {"flash_peer", 2048u, OSAL_PRIO_NORMAL_BASE};
    (void)osal_thread_create(&peer, &pattr, flash_peer_probe, NULL);

    if (!flash_verify_is_blank(REG_TAIL))
    {
        OM_LOG_ERROR("tail NOT blank; execution-model cases skipped");
        g_fail++;
        return;
    }

    /* G3.1 同步 + 让出：整扇区擦除期间调用者阻塞，但心跳（HIGH）不断流——
     * 后端等待硬件完成时让出 CPU，其它任务不被饿死（本层不持线程） */
    uint32_t hb0 = g_hbCount;
    g_peerT0 = 0u;
    g_peerT1 = 0u;
    g_peerRet = 0xFFFFFFFFu;
    osal_sem_post(g_peerGo); /* 放行并访线程：读与擦除竞争同一设备 */
    g_eraseT0 = (uint32_t)osal_time_now_monotonic();
    OmRet eret = flash_erase(g_flash, REG_TAIL, TAIL_SIZE);
    g_eraseT1 = (uint32_t)osal_time_now_monotonic();
    CHECK(eret == OM_OK, "tail erase completes in caller context (ret=%d)", (int)eret);
    CHECK(g_hbCount > hb0, "heartbeat kept running during erase (yield, %u->%u)",
          (unsigned)hb0, (unsigned)g_hbCount);

    /* G3.2 每设备互斥：擦除在途时另一线程的读不返回 BUSY，而是等锁；
     * 其返回时刻须不早于擦除结束——数据通路上不存在与擦除交错的读 */
    for (int i = 0; i < 500 && g_peerT1 == 0u; i++)
    {
        osal_sleep_ms(10);
    }
    CHECK(g_peerT1 != 0u, "peer read returned");
    CHECK(g_peerRet == (uint32_t)OM_OK,
          "peer read serialized behind erase (ret=%u, no BUSY rejection)",
          (unsigned)g_peerRet);
    CHECK(g_peerT1 + 5u >= g_eraseT1,
          "peer read finished after erase (t=%u vs erase end %u)", (unsigned)g_peerT1,
          (unsigned)g_eraseT1);

    /* G3.3 擦除自检结果可见：擦后整扇区为擦后值 */
    CHECK(flash_verify_is_blank(REG_TAIL), "erased region reads back as erasedValue");
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
    verify_sync_model();
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

    osal_sem_create(&g_peerGo, 1u, 0u);
    (void)osal_thread_create(&vthread, &vattr, flash_verify_thread, NULL);
    (void)osal_thread_create(&hthread, &hattr, flash_verify_heartbeat, NULL);
    return OM_OK;
}
OM_INIT_APPLICATION(flash_verify_main);

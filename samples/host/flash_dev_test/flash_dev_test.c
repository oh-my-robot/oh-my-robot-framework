/**
 * @file   flash_dev_test.c
 * @brief  FlashDev v1 框架 host 仿真测试（同步语义 + 异步执行模型）
 *
 * 夹具：flash_sim_u（均匀 256KB：扇区 4KB×64、writeUnit 4）
 *       flash_sim_f（F407 形 1MB：双 bank 非均一 16K×4+64K+128K×3）
 *
 * 测试面（v1）：
 *   T1 注册/查找/几何合法性    T2 读语义
 *   T3 擦除语义（均匀/非均一）  T4 program 语义
 *   T5 DevInterface            T6 多设备独立域并行
 *   T7 异步执行模型（async 链/队列满 BUSY/回调内同步拒绝/回调内再提交）
 *   T8 后端错误注入传播（同步/异步回调收错、失败不落位、队列不楔死）
 *
 * 返回码：g_fail == 0 退出 0，否则退出 1。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/model/device.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "osal/osal_sem.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h"

#include "flash_sim.h"

#ifdef _WIN32
#include <windows.h>
#define THREAD_FN(fn) static DWORD WINAPI fn(LPVOID arg)
typedef HANDLE ThreadHandle;
#else
#include <pthread.h>
#define THREAD_FN(fn) static void *fn(void *arg)
typedef pthread_t ThreadHandle;
#endif

/* ===================================================================
 * 断言与计数
 * =================================================================== */

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
 * 夹具
 * =================================================================== */

#define CAP_U (256u * 1024u)
#define SECT_U 4096u

static const FlashGeometry geom_uniform = {
    .capacity = CAP_U,
    .erasedValue = 0xFF,
    .writeUnit = 4u,
    .pageSize = 256u,
    .sectorSize = SECT_U,
    .sectorCount = CAP_U / SECT_U,
    .sectorRegions = NULL,
};

#define CAP_F (1024u * 1024u)
#define BANK2_OFF (512u * 1024u)

/* F407 形：bank1 {16K×4, 64K×1, 128K×3} + bank2 同构 */
static const FlashSectorRegion regs_f407[] = {
    {0u, 16384u, 4u},
    {65536u, 65536u, 1u},
    {131072u, 131072u, 3u},
    {BANK2_OFF, 16384u, 4u},
    {BANK2_OFF + 65536u, 65536u, 1u},
    {BANK2_OFF + 131072u, 131072u, 3u},
};

static const FlashGeometry geom_f407 = {
    .capacity = CAP_F,
    .erasedValue = 0xFF,
    .writeUnit = 4u,
    .pageSize = 0u,
    .sectorSize = 0u,
    .sectorCount = 0,
    .sectorRegions = regs_f407,
};

static const FlashOps sim_ops = {
    .read = flash_sim_read,
    .write = flash_sim_write,
    .erase = flash_sim_erase,
};

static FlashDev dev_u;
static FlashDev dev_f;
static FlashSim sim_u;
static FlashSim sim_f;

static uint8_t g_buf[8192]; /* 主线程读写缓冲 */

/* 异步测试事件（每用例一个轮次） */
static OsalSem *g_evt;
static volatile int g_cbCount;
static OmRet g_cbSyncRet;
static volatile OmRet g_cbErrStatus; /* T8：回调收到的后端错误码 */

static void test_evt_init(void)
{
    osal_sem_create(&g_evt, 1u, 0u);
}

static int test_evt_wait(uint32_t timeout_ms)
{
    return osal_sem_wait(g_evt, timeout_ms) == OSAL_OK;
}

/* ===================================================================
 * T1: 注册 / 查找 / 几何
 * =================================================================== */

static void test_register_find(void)
{
    printf("[T1] register / find / geometry\n");

    CHECK(flash_register(&dev_u, "flash_sim_u", &geom_uniform, &sim_ops, &sim_u, NULL) == OM_OK,
          "register uniform flash_sim_u (auto domain)");
    CHECK(flash_register(&dev_f, "flash_sim_f", &geom_f407, &sim_ops, &sim_f, NULL) == OM_OK,
          "register non-uniform flash_sim_f (auto domain)");
    CHECK(flash_find("flash_sim_u") == &dev_u, "flash_find by name");
    CHECK(flash_find("no_such_dev") == NULL, "flash_find miss -> NULL");
    CHECK(flash_find(NULL) == NULL, "flash_find(NULL) -> NULL");
    CHECK(flash_geometry(&dev_u) == &geom_uniform, "geometry returns registered static geom");

    /* 注册期几何校验 */
    FlashDev bad;
    memset(&bad, 0, sizeof(bad));
    FlashGeometry bad_geom = geom_uniform;
    bad_geom.capacity = 0;
    CHECK(flash_register(&bad, "flash_bad0", &bad_geom, &sim_ops, NULL, NULL) ==
              OM_ERR_INVALID_ARG,
          "reject capacity==0");
    bad_geom = geom_uniform;
    bad_geom.sectorSize = 0;
    bad_geom.sectorRegions = NULL;
    CHECK(flash_register(&bad, "flash_bad1", &bad_geom, &sim_ops, NULL, NULL) ==
              OM_ERR_INVALID_ARG,
          "reject non-uniform w/o region table");
    bad_geom = geom_uniform;
    bad_geom.sectorSize = 1000u;
    CHECK(flash_register(&bad, "flash_bad2", &bad_geom, &sim_ops, NULL, NULL) ==
              OM_ERR_INVALID_ARG,
          "reject capacity %% sectorSize != 0");
    bad_geom = geom_uniform;
    bad_geom.writeUnit = 0;
    CHECK(flash_register(&bad, "flash_bad3", &bad_geom, &sim_ops, NULL, NULL) ==
              OM_ERR_INVALID_ARG,
          "reject writeUnit==0");
    FlashOps no_ops = sim_ops;
    no_ops.erase = NULL;
    CHECK(flash_register(&bad, "flash_bad4", &geom_uniform, &no_ops, NULL, NULL) ==
              OM_ERR_INVALID_ARG,
          "reject ops missing erase");
    CHECK(flash_register(&bad, "flash_sim_u", &geom_uniform, &sim_ops, NULL, NULL) ==
              OM_ERR_CONFLICT,
          "duplicate name -> CONFLICT");
}

/* ===================================================================
 * T2: 读语义（同步直跑；空闲设备）
 * =================================================================== */

static void test_read(void)
{
    printf("[T2] read semantics\n");

    memset(g_buf, 0x00, sizeof(g_buf));
    CHECK(flash_read(&dev_u, 0u, g_buf, 64u) == OM_OK, "read erased region");
    for (int i = 0; i < 64; i++)
    {
        if (g_buf[i] != 0xFF)
        {
            CHECK(false, "erased value = 0xFF (i=%d got 0x%02X)", i, g_buf[i]);
            return;
        }
    }
    CHECK(true, "erased bytes read back as 0xFF");

    CHECK(flash_read(&dev_u, 0u, NULL, 0u) == OM_OK, "len==0 w/ NULL buf -> OK");
    CHECK(flash_read(&dev_u, CAP_U, g_buf, 1u) == OM_ERR_INVALID_ARG, "addr==capacity OOB");
    CHECK(flash_read(&dev_u, CAP_U - 1u, g_buf, 2u) == OM_ERR_INVALID_ARG, "len crosses end");
    CHECK(flash_read(&dev_u, 0u, NULL, 1u) == OM_ERR_INVALID_ARG, "buf NULL w/ len>0");
    CHECK(flash_read(NULL, 0u, g_buf, 1u) == OM_ERR_INVALID_ARG, "dev NULL");
}

/* ===================================================================
 * T3: 擦除语义（同步等待原语）
 * =================================================================== */

static void test_erase_uniform(void)
{
    printf("[T3a] erase semantics (uniform)\n");

    memset(g_buf, 0x5A, SECT_U);
    CHECK(flash_write(&dev_u, 0u, g_buf, SECT_U) == OM_OK, "dirty sector 0 (prep)");
    CHECK(flash_erase(&dev_u, 0u, SECT_U) == OM_OK, "erase whole sector 0");
    memset(g_buf, 0x00, 64u);
    CHECK(flash_read(&dev_u, 0u, g_buf, 64u) == OM_OK, "read back after erase");
    CHECK(g_buf[0] == 0xFF && g_buf[63] == 0xFF, "sector erased to 0xFF");

    CHECK(flash_erase(&dev_u, 2u, SECT_U) == OM_ERR_INVALID_ARG, "addr not on sector boundary");
    CHECK(flash_erase(&dev_u, 0u, SECT_U / 2u) == OM_ERR_INVALID_ARG, "len not whole sector");
    CHECK(flash_erase(&dev_u, SECT_U, SECT_U * 2u) == OM_OK, "erase 2 consecutive sectors");
    CHECK(flash_erase(&dev_u, 0u, 0u) == OM_OK, "len==0 erase no-op");
}

static void test_erase_nonuniform(void)
{
    printf("[T3b] erase semantics (non-uniform, F407 shape)\n");

    CHECK(flash_erase(&dev_f, 0u, 65536u) == OM_OK, "4x16K region whole");
    CHECK(flash_erase(&dev_f, 0u, 98304u) == OM_ERR_INVALID_ARG, "end inside 64K sector");
    CHECK(flash_erase(&dev_f, 0u, 131072u) == OM_OK, "cross 16K->64K regions");
    CHECK(flash_erase(&dev_f, BANK2_OFF, 65536u) == OM_OK, "erase start of bank2");
    CHECK(flash_erase(&dev_f, 65536u, BANK2_OFF - 65536u) == OM_OK, "whole mid region to bank2");
    CHECK(flash_erase(&dev_f, CAP_F - 131072u, 131072u) == OM_OK, "last 128K sector");
    CHECK(flash_erase(&dev_f, 0u, CAP_F) == OM_OK, "whole-chip erase");
    memset(g_buf, 0x00, 64u);
    CHECK(flash_read(&dev_f, BANK2_OFF + 100u, g_buf, 64u) == OM_OK, "read bank2 after chip erase");
    CHECK(g_buf[0] == 0xFF, "chip erased to 0xFF");
}

/* ===================================================================
 * T4: program 语义（同步等待原语）
 * =================================================================== */

static void test_program(void)
{
    printf("[T4] write/program semantics\n");

    const uint32_t base = 0x10000u;
    CHECK(flash_erase(&dev_u, base, SECT_U) == OM_OK, "prep: erase sector @0x10000");

    CHECK(flash_write(&dev_u, base + 2u, g_buf, 4u) == OM_ERR_INVALID_ARG, "addr misaligned");
    CHECK(flash_write(&dev_u, base, g_buf, 2u) == OM_ERR_INVALID_ARG, "len misaligned");
    CHECK(flash_write(&dev_u, base, NULL, 4u) == OM_ERR_INVALID_ARG, "data NULL");
    CHECK(flash_write(&dev_u, base, g_buf, 0u) == OM_OK, "len==0 no-op");

    memset(g_buf, 0xA5, 128u);
    CHECK(flash_write(&dev_u, base, g_buf, 128u) == OM_OK, "program erased region");
    memset(g_buf, 0x00, 128u);
    CHECK(flash_read(&dev_u, base, g_buf, 128u) == OM_OK, "read back");
    CHECK(g_buf[0] == 0xA5 && g_buf[127] == 0xA5, "content matches");

    memset(g_buf, 0xA5, 128u);
    CHECK(flash_write(&dev_u, base, g_buf, 128u) == OM_OK, "rewrite same value (idempotent)");

    memset(g_buf, 0xFF, 128u);
    CHECK(flash_write(&dev_u, base, g_buf, 128u) == OM_ERR_FLASH_IO,
          "program 0->1 flip rejected (strict NOR model)");
}

/* ===================================================================
 * T5: 标准 DevInterface
 * =================================================================== */

static void test_device_interface(void)
{
    printf("[T5] standard Device interface\n");

    const uint32_t base = 0x30000u;
    CHECK(flash_erase(&dev_u, base, SECT_U) == OM_OK, "prep: erase sector @0x30000");

    CHECK(device_open(&dev_u.parent, 0u) == OM_OK, "device_open (auto init)");

    uint8_t pattern[16];
    memset(pattern, 0x3C, sizeof(pattern));
    CHECK(device_write(&dev_u.parent, (void *)(uintptr_t)base, pattern, 16u) == 16u,
          "device_write thin forward via ctrl_info offset");
    memset(pattern, 0x00, sizeof(pattern));
    CHECK(device_read(&dev_u.parent, (void *)(uintptr_t)base, pattern, 16u) == 16u,
          "device_read thin forward");
    CHECK(pattern[0] == 0x3C && pattern[15] == 0x3C, "read back via device channel");

    const FlashGeometry *got = NULL;
    CHECK(device_ctrl(&dev_u.parent, FLASH_CMD_GET_GEOMETRY, &got) == OM_OK && got == &geom_uniform,
          "control GET_GEOMETRY");
    CHECK(device_ctrl(&dev_u.parent, 0x9999u, &got) == OM_ERR_NOT_SUPPORTED, "unknown cmd rejected");
    CHECK(device_close(&dev_u.parent) == OM_OK, "device_close");

    CHECK(flash_read(&dev_u, base, pattern, 16u) == OM_OK, "family API works w/o open");
}

/* ===================================================================
 * T6: 多设备独立域并行（慢设备不拖累快设备）
 * =================================================================== */

typedef struct
{
    FlashDev *dev;
    uint32_t base;
    uint32_t len;
    uint32_t rounds;
    uint32_t elapsedMs;
    uint32_t fails;
} ParaArg;

THREAD_FN(flash_para_worker)
{
    ParaArg *a = (ParaArg *)arg;
    uint8_t *buf = (uint8_t *)malloc(a->len);
    uint8_t *rbuf = (uint8_t *)malloc(a->len);
    if (!buf || !rbuf)
    {
        a->fails = 1;
        free(buf);
        free(rbuf);
        return 0;
    }
    memset(buf, 0xA0 | (uint8_t)(a->base >> 12), a->len); /* 每线程独立 pattern */

    uint32_t t0 = (uint32_t)osal_time_now_monotonic();
    for (uint32_t r = 0; r < a->rounds; r++)
    {
        if (flash_erase(a->dev, a->base, a->len) != OM_OK)
        {
            a->fails++;
            break;
        }
        if (flash_write(a->dev, a->base, buf, a->len) != OM_OK)
        {
            a->fails++;
            break;
        }
        memset(rbuf, 0x00, a->len);
        if (flash_read(a->dev, a->base, rbuf, a->len) != OM_OK)
        {
            a->fails++;
            break;
        }
        if (memcmp(buf, rbuf, a->len) != 0)
        {
            a->fails++;
        }
    }
    a->elapsedMs = (uint32_t)osal_time_now_monotonic() - t0;
    free(buf);
    free(rbuf);
    return 0;
}

static void test_parallel_domains(void)
{
    printf("[T6] multi-device parallel domains\n");

    /* u 慢（每操作 8ms，总 ~数百 ms）；f 瞬时。若共享执行者，f 会被 u 拖到
     * u 之后才完成；独立域下 f 应远早于 u 完成。 */
    flash_sim_set_delay(&sim_u, 8u);
    flash_sim_set_delay(&sim_f, 0u);

    ParaArg au = {&dev_u, 0x10000u, 0x8000u, 15u, 0u, 0u}; /* 32KB = 8 扇区，u 区 */
    ParaArg af = {&dev_f, 0x80000u, 0x4000u, 30u, 0u, 0u}; /* 16KB = bank2 首 16K 扇区 */
    ThreadHandle th_u;
    ThreadHandle th_f;

#ifdef _WIN32
    th_u = CreateThread(NULL, 0, flash_para_worker, &au, 0, NULL);
    th_f = CreateThread(NULL, 0, flash_para_worker, &af, 0, NULL);
    CHECK(th_u != NULL && th_f != NULL, "spawn parallel workers");
    WaitForMultipleObjects(2, (HANDLE[]){th_u, th_f}, TRUE, INFINITE);
    CloseHandle(th_u);
    CloseHandle(th_f);
#else
    CHECK(pthread_create(&th_u, NULL, flash_para_worker, &au) == 0, "spawn worker u");
    CHECK(pthread_create(&th_f, NULL, flash_para_worker, &af) == 0, "spawn worker f");
    pthread_join(th_u, NULL);
    pthread_join(th_f, NULL);
#endif

    CHECK(au.fails == 0 && af.fails == 0, "both workers clean (no cross-talk)");
    printf("  info: slow-u elapsed=%u ms, fast-f elapsed=%u ms\n", (unsigned)au.elapsedMs,
           (unsigned)af.elapsedMs);
    CHECK(au.elapsedMs >= 200u, "slow device actually took its own time (%u ms)",
          (unsigned)au.elapsedMs);
    /* 若共享执行者：f 请求排队在 u 之后，完成时刻 > u 总时长；独立域下 f 先于 u 完成 */
    CHECK(af.elapsedMs < au.elapsedMs,
          "fast device not dragged by slow device (independent domains)");

    /* 数据完整性：u 区 = u pattern */
    uint8_t probe = 0;
    flash_read(&dev_u, 0x10000u, &probe, 1u);
    CHECK(probe == (uint8_t)(0xA0 | (0x10000u >> 12)), "u region holds own pattern");
    flash_read(&dev_f, 0x80000u, &probe, 1u);
    CHECK(probe == (uint8_t)(0xA0 | (0x80000u >> 12)), "f region holds own pattern");

    flash_sim_set_delay(&sim_u, 0u);
}

/* ===================================================================
 * T7: 异步执行模型
 * =================================================================== */

/* 无操作回调（计数用） */
static void t7_noop_cb(FlashDev *dev, OmRet status, void *param)
{
    (void)dev;
    (void)status;
    (void)param;
    g_cbCount++;
}

/* T7.1 设备级链回调：阶段机推进 erase → write → read 校验 → 事件
 * （设备完成通知是"单一入口"：回调内按阶段自推进，典型链式用法） */
static int g_chainStage;

static void t7_chain_cb(FlashDev *dev, OmRet status, void *param)
{
    (void)param;
    g_cbCount++;
    if (g_chainStage == 0)
    {
        /* erase 完成 → 提交 write */
        g_chainStage = 1;
        if (status != OM_OK)
        {
            osal_sem_post(g_evt);
            return;
        }
        static uint8_t pat[128];
        for (uint32_t i = 0; i < sizeof(pat); i++)
        {
            pat[i] = (uint8_t)(0x11 + i);
        }
        g_cbSyncRet = flash_write_async(dev, 0x20000u, pat, sizeof(pat));
        if (g_cbSyncRet != OM_OK)
        {
            printf("  dbg: cb write_async ret=%d\n", (int)g_cbSyncRet);
            osal_sem_post(g_evt); /* 失败也放行，避免测试死等 */
        }
        return;
    }
    /* write 完成 → read 校验 */
    static uint8_t rbuf[128];
    OmRet r = flash_read(dev, 0x20000u, rbuf, sizeof(rbuf)); /* 回调内直跑（设备空闲） */
    if (r == OM_OK && rbuf[0] == 0x11 && rbuf[127] == (uint8_t)(0x11 + 127))
    {
        CHECK(true, "async chain content verified in callback");
    }
    else
    {
        CHECK(false, "async chain content mismatch (r=%d)", (int)r);
    }
    osal_sem_post(g_evt);
}

/* T7.3 回调内同步调用 → 同域拒绝 BUSY */
static void t7_cb_sync_in_worker(FlashDev *dev, OmRet status, void *param)
{
    (void)status;
    (void)param;
    g_cbCount++;
    g_cbSyncRet = flash_erase(dev, 0x30000u, SECT_U); /* 应被同域拒绝 */
    osal_sem_post(g_evt);
}

/* T7.5 无完成通知（setter 未注册）：入队即忘（轮询式等完成） */
static void test_async_null_done(void)
{
    flash_set_done_cb(&dev_u, NULL, NULL);
    CHECK(flash_erase_async(&dev_u, 0x3C000u, SECT_U) == OM_OK,
          "async erase w/ no done cb submitted");
    for (int i = 0; i < 200; i++)
    {
        uint8_t b = 0;
        flash_read(&dev_u, 0x3C000u, &b, 1u);
        if (b == 0xFF)
        {
            break; /* 完成（擦后空白） */
        }
        osal_sleep_ms(5);
    }
    uint8_t b = 0;
    flash_read(&dev_u, 0x3C000u, &b, 1u);
    CHECK(b == 0xFF, "NULL-done async completed (region erased)");
}

static void test_async(void)
{
    printf("[T7] async execution model\n");

    /* T7.1 设备级回调链（erase 完成 → 回调内提交 write → 校验） */
    g_cbCount = 0;
    g_chainStage = 0;
    g_cbSyncRet = OM_OK;
    flash_set_done_cb(&dev_u, t7_chain_cb, NULL);
    CHECK(flash_erase_async(&dev_u, 0x20000u, SECT_U) == OM_OK,
          "async erase submitted (returns immediately)");
    CHECK(test_evt_wait(2000u), "async chain completed via callbacks");
    CHECK(g_cbCount == 2, "two callbacks fired (erase+write)");

    /* T7.2 队列满 → BUSY（慢后端稳定在途：req1 执行 40ms 期间 req2 排队占满 2 槽） */
    flash_sim_set_delay(&sim_u, 40u);
    g_cbCount = 0;
    flash_set_done_cb(&dev_u, t7_noop_cb, NULL);
    CHECK(flash_erase_async(&dev_u, 0x30000u, SECT_U) == OM_OK,
          "req1 submitted (in-flight)");
    osal_sleep_ms(10); /* req1 仍在执行（40ms 未完） */
    CHECK(flash_erase_async(&dev_u, 0x31000u, SECT_U) == OM_OK,
          "req2 submitted (queued)");
    osal_sleep_ms(5);
    CHECK(flash_erase_async(&dev_u, 0x32000u, SECT_U) == OM_ERR_FLASH_BUSY,
          "req3 rejected: queue full");
    for (int i = 0; i < 100 && g_cbCount < 2; i++)
    {
        osal_sleep_ms(10);
    }
    CHECK(g_cbCount == 2, "both queued requests completed");
    flash_sim_set_delay(&sim_u, 0u);

    /* T7.3 回调内同步调用（同域 worker）→ BUSY 拒绝，不死锁 */
    g_cbCount = 0;
    g_cbSyncRet = OM_OK;
    flash_sim_set_delay(&sim_u, 10u);
    flash_set_done_cb(&dev_u, t7_cb_sync_in_worker, NULL);
    CHECK(flash_erase_async(&dev_u, 0x30000u, SECT_U) == OM_OK,
          "async erase w/ sync-in-callback submitted");
    CHECK(test_evt_wait(2000u), "callback executed (no deadlock)");
    CHECK(g_cbSyncRet == OM_ERR_FLASH_BUSY, "sync wait inside worker rejected (BUSY)");
    flash_sim_set_delay(&sim_u, 0u);

    /* T7.5 done == NULL */
    test_async_null_done();
}

/* ===================================================================
 * T8: 后端错误注入——错误传播语义（后端 IO 错误必须到达调用者：
 *     同步原语返回错误码、异步回调收到错误、失败不落位、队列不楔死）
 * =================================================================== */

static void t8_err_cb(FlashDev *dev, OmRet status, void *param)
{
    (void)dev;
    (void)param;
    g_cbErrStatus = status;
    osal_sem_post(g_evt);
}

static void test_backend_error(void)
{
    printf("[T8] backend error propagation (injected IO)\n");

    /* 准备：0x3F000（末扇区 63，未被前序用例占用）写 0x00 pattern——
     * 以"pattern 在/不在"区分擦除是否真实执行 */
    static uint8_t pat[64];
    memset(pat, 0x00, sizeof(pat));
    CHECK(flash_erase(&dev_u, 0x3F000u, SECT_U) == OM_OK, "prep: sector 64 blank");
    CHECK(flash_write(&dev_u, 0x3F000u, pat, sizeof(pat)) == OM_OK, "prep: pattern written");

    /* T8.1 同步擦除：注入下返回 IO、内容不被改动；解除后恢复可用 */
    flash_sim_set_fail(&sim_u, true);
    CHECK(flash_erase(&dev_u, 0x3F000u, SECT_U) == OM_ERR_FLASH_IO,
          "sync erase: backend IO error propagated");
    CHECK(flash_read(&dev_u, 0x3F000u, g_buf, sizeof(pat)) == OM_OK && g_buf[0] == 0x00 &&
              g_buf[63] == 0x00,
          "failed erase left content untouched");
    flash_sim_set_fail(&sim_u, false);
    CHECK(flash_erase(&dev_u, 0x3F000u, SECT_U) == OM_OK,
          "erase OK after fault cleared (state recovered)");
    CHECK(flash_read(&dev_u, 0x3F000u, g_buf, sizeof(pat)) == OM_OK && g_buf[0] == 0xFF,
          "recovered erase really erased");

    /* T8.2 同步写：注入下返回 IO 且不落位 */
    CHECK(flash_write(&dev_u, 0x3F000u, pat, sizeof(pat)) == OM_OK, "prep: pattern rewritten");
    flash_sim_set_fail(&sim_u, true);
    CHECK(flash_write(&dev_u, 0x3F000u + 0x100u, pat, sizeof(pat)) == OM_ERR_FLASH_IO,
          "sync write: backend IO error propagated");
    flash_sim_set_fail(&sim_u, false);
    CHECK(flash_read(&dev_u, 0x3F000u + 0x100u, g_buf, sizeof(pat)) == OM_OK &&
              g_buf[0] == 0xFF,
          "failed write left content untouched");

    /* T8.3 异步：注入下回调收到 IO 错误；槽释放——解除后新请求正常完成 */
    flash_sim_set_fail(&sim_u, true);
    g_cbErrStatus = OM_OK;
    flash_set_done_cb(&dev_u, t8_err_cb, NULL);
    CHECK(flash_erase_async(&dev_u, 0x3F000u, SECT_U) == OM_OK,
          "async erase submitted while backend failing");
    CHECK(test_evt_wait(2000u), "callback fired on backend error");
    CHECK(g_cbErrStatus == OM_ERR_FLASH_IO, "async callback received backend IO error");

    flash_sim_set_fail(&sim_u, false);
    g_cbCount = 0;
    flash_set_done_cb(&dev_u, t7_noop_cb, NULL);
    CHECK(flash_erase_async(&dev_u, 0x3F000u, SECT_U) == OM_OK,
          "async erase OK after fault cleared (queue not wedged)");
    for (int i = 0; i < 100 && g_cbCount < 1; i++)
    {
        osal_sleep_ms(10);
    }
    CHECK(g_cbCount == 1, "post-error async completed via callback");
    flash_set_done_cb(&dev_u, NULL, NULL);
}

/* ===================================================================
 * main
 * =================================================================== */

int main(void)
{
    printf("=== FlashDev v1 host simulation test ===\n");

    flash_sim_init(&sim_u, CAP_U, 0xFFu, 4u);
    flash_sim_init(&sim_f, CAP_F, 0xFFu, 4u);
    CHECK(sim_u.mem != NULL && sim_f.mem != NULL, "sim memory allocated");
    test_evt_init();

    test_register_find();
    test_read();
    test_erase_uniform();
    test_program();
    test_device_interface();
    test_erase_nonuniform();
    test_parallel_domains();
    test_async();
    test_backend_error();

    flash_sim_deinit(&sim_u);
    flash_sim_deinit(&sim_f);
    osal_sem_delete(g_evt);

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

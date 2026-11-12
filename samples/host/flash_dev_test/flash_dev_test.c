/**
 * @file   flash_dev_test.c
 * @brief  FlashDev v1 框架 host 仿真测试（同步语义 + 每设备互斥）
 *
 * 夹具：flash_sim_u（均匀 256KB：扇区 4KB×64、writeUnit 4）
 *       flash_sim_f（F407 形 1MB：双 bank 非均一 16K×4+64K+128K×3）
 *
 * 测试面（v1）：
 *   T1 注册/查找/几何合法性    T2 读语义
 *   T3 擦除语义（均匀/非均一）  T4 program 语义
 *   T5 DevInterface            T6 跨设备并行（锁按设备实例）
 *   T7 执行模型（调用者上下文同步 + 同设备互斥）
 *   T8 后端错误注入传播（同步返回错误、失败不落位、擦后校验、锁不泄漏）
 *
 * 返回码：g_fail == 0 退出 0，否则退出 1。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/model/device.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h"

#include "flash_sim.h"

#ifdef _WIN32
#include <windows.h>
#define THREAD_FN(fn) static DWORD WINAPI fn(LPVOID arg)
typedef HANDLE ThreadHandle;
typedef DWORD(WINAPI *ThreadFn)(LPVOID);
#else
#include <pthread.h>
#define THREAD_FN(fn) static void *fn(void *arg)
typedef pthread_t ThreadHandle;
typedef void *(*ThreadFn)(void *);
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

/* ===================================================================
 * 线程辅助（T6/T7 并发用例共用）
 * =================================================================== */

static unsigned long test_thread_id(void)
{
#ifdef _WIN32
    return (unsigned long)GetCurrentThreadId();
#else
    return (unsigned long)pthread_self();
#endif
}

static long test_atomic_inc(volatile long *p)
{
#ifdef _WIN32
    return InterlockedIncrement(p);
#else
    return __sync_add_and_fetch(p, 1);
#endif
}

static long test_atomic_dec(volatile long *p)
{
#ifdef _WIN32
    return InterlockedDecrement(p);
#else
    return __sync_sub_and_fetch(p, 1);
#endif
}

static ThreadHandle test_thread_spawn(ThreadFn fn, void *arg)
{
    ThreadHandle th = {0};
#ifdef _WIN32
    th = CreateThread(NULL, 0, fn, arg, 0, NULL);
#else
    if (pthread_create(&th, NULL, fn, arg) != 0)
    {
        th = 0;
    }
#endif
    return th;
}

static void test_thread_join(ThreadHandle th)
{
    if (!th)
    {
        return; /* 创建失败：无可等待对象 */
    }
#ifdef _WIN32
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
#else
    pthread_join(th, NULL);
#endif
}

/* ===================================================================
 * 后端探针：包裹 sim 后端，统计"同时处于后端内的调用数"与后端实际线程
 *
 * 互斥成立时该数恒为 1，且同设备并发线程的峰值也是 1；
 * 跨设备并行时峰值应达到 2。计数即锁有效性的直接证据，不依赖耗时。
 * =================================================================== */

static volatile long g_inBackend;
static volatile long g_maxInBackend;
static volatile unsigned long g_backendTid;

static void backend_enter(void)
{
    long n = test_atomic_inc(&g_inBackend);
    if (n > g_maxInBackend)
    {
        g_maxInBackend = n;
    }
    g_backendTid = test_thread_id();
}

static void backend_exit(void)
{
    (void)test_atomic_dec(&g_inBackend);
}

static OmRet wrap_read(FlashDev *dev, uint32_t addr, void *buf, size_t len)
{
    backend_enter();
    OmRet r = flash_sim_read(dev, addr, buf, len);
    backend_exit();
    return r;
}

static OmRet wrap_write(FlashDev *dev, uint32_t addr, const void *data, size_t len)
{
    backend_enter();
    OmRet r = flash_sim_write(dev, addr, data, len);
    backend_exit();
    return r;
}

static OmRet wrap_erase(FlashDev *dev, uint32_t addr, size_t len)
{
    backend_enter();
    OmRet r = flash_sim_erase(dev, addr, len);
    backend_exit();
    return r;
}

static const FlashOps wrap_ops = {
    .read = wrap_read,
    .write = wrap_write,
    .erase = wrap_erase,
};

/* ===================================================================
 * T1: 注册 / 查找 / 几何
 * =================================================================== */

static void test_register_find(void)
{
    printf("[T1] register / find / geometry\n");

    CHECK(flash_register(&dev_u, "flash_sim_u", &geom_uniform, &wrap_ops, &sim_u) == OM_OK,
          "register uniform flash_sim_u");
    CHECK(flash_register(&dev_f, "flash_sim_f", &geom_f407, &wrap_ops, &sim_f) == OM_OK,
          "register non-uniform flash_sim_f");
    CHECK(flash_find("flash_sim_u") == &dev_u, "flash_find by name");
    CHECK(flash_find("no_such_dev") == NULL, "flash_find miss -> NULL");
    CHECK(flash_find(NULL) == NULL, "flash_find(NULL) -> NULL");
    CHECK(flash_geometry(&dev_u) == &geom_uniform, "geometry returns registered static geom");

    /* 注册期几何校验 */
    FlashDev bad;
    memset(&bad, 0, sizeof(bad));
    FlashGeometry bad_geom = geom_uniform;
    bad_geom.capacity = 0;
    CHECK(flash_register(&bad, "flash_bad0", &bad_geom, &sim_ops, NULL) ==
              OM_ERR_INVALID_ARG,
          "reject capacity==0");
    bad_geom = geom_uniform;
    bad_geom.sectorSize = 0;
    bad_geom.sectorRegions = NULL;
    CHECK(flash_register(&bad, "flash_bad1", &bad_geom, &sim_ops, NULL) ==
              OM_ERR_INVALID_ARG,
          "reject non-uniform w/o region table");
    bad_geom = geom_uniform;
    bad_geom.sectorSize = 1000u;
    CHECK(flash_register(&bad, "flash_bad2", &bad_geom, &sim_ops, NULL) ==
              OM_ERR_INVALID_ARG,
          "reject capacity %% sectorSize != 0");
    bad_geom = geom_uniform;
    bad_geom.writeUnit = 0;
    CHECK(flash_register(&bad, "flash_bad3", &bad_geom, &sim_ops, NULL) ==
              OM_ERR_INVALID_ARG,
          "reject writeUnit==0");
    FlashOps no_ops = sim_ops;
    no_ops.erase = NULL;
    CHECK(flash_register(&bad, "flash_bad4", &geom_uniform, &no_ops, NULL) ==
              OM_ERR_INVALID_ARG,
          "reject ops missing erase");
    CHECK(flash_register(&bad, "flash_sim_u", &geom_uniform, &sim_ops, NULL) ==
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
    CHECK(flash_read(&dev_u, CAP_U, g_buf, 1u) == OM_ERR_RANGE, "addr==capacity OOB");
    CHECK(flash_read(&dev_u, CAP_U - 1u, g_buf, 2u) == OM_ERR_RANGE, "len crosses end");
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
 * T6: 跨设备并行（锁按设备实例：一个设备不让其它设备等待）
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

static void test_cross_device_parallel(void)
{
    printf("[T6] cross-device parallelism (per-device lock)\n");

    /* 两设备各设延迟，使两 worker 大部分时间停在后端内：跨设备并发达标时
     * 后端内在飞调用数会达到 2（同设备并发用例 T7.3 的峰值为 1） */
    g_inBackend = 0;
    g_maxInBackend = 0;
    flash_sim_set_delay(&sim_u, 2u);
    flash_sim_set_delay(&sim_f, 2u);

    ParaArg au = {&dev_u, 0x10000u, 0x1000u, 3u, 0u, 0u}; /* 1 扇区 */
    ParaArg af = {&dev_f, 0x80000u, 0x4000u, 3u, 0u, 0u}; /* bank2 首个 16K 扇区 */
    ThreadHandle th_u = test_thread_spawn(flash_para_worker, &au);
    ThreadHandle th_f = test_thread_spawn(flash_para_worker, &af);
    CHECK(th_u != 0 && th_f != 0, "spawn cross-device workers");
    test_thread_join(th_u);
    test_thread_join(th_f);

    CHECK(au.fails == 0 && af.fails == 0, "both workers clean (no cross-talk)");
    printf("  info: u elapsed=%u ms, f elapsed=%u ms\n", (unsigned)au.elapsedMs,
           (unsigned)af.elapsedMs);
    CHECK(g_maxInBackend >= 2, "two devices were in backend at the same time (peak=%ld)",
          g_maxInBackend);
    CHECK(g_inBackend == 0, "in-flight counter balanced after cross-device run");

    /* 数据完整性：各设备区 = 各自 pattern */
    uint8_t probe = 0;
    flash_read(&dev_u, 0x10000u, &probe, 1u);
    CHECK(probe == (uint8_t)(0xA0 | (0x10000u >> 12)), "u region holds own pattern");
    flash_read(&dev_f, 0x80000u, &probe, 1u);
    CHECK(probe == (uint8_t)(0xA0 | (0x80000u >> 12)), "f region holds own pattern");

    flash_sim_set_delay(&sim_u, 0u);
    flash_sim_set_delay(&sim_f, 0u);
}

/* ===================================================================
 * T7: 执行模型——调用者上下文同步 + 每设备互斥
 * =================================================================== */

static void test_sync_model(void)
{
    printf("[T7] execution model (caller context + per-device lock)\n");

    /* T7.1 后端在调用者线程上执行：本层不持有任何线程 */
    g_backendTid = 0u;
    flash_sim_set_delay(&sim_u, 0u);
    CHECK(flash_read(&dev_u, 0u, g_buf, 4u) == OM_OK, "read via instrumented backend");
    CHECK(g_backendTid == test_thread_id(), "backend ran on the calling thread");

    /* T7.2 同步返回：调用返回时操作已结束、结果已定，无通知机制参与 */
    flash_sim_set_delay(&sim_u, 5u);
    uint32_t t0 = (uint32_t)osal_time_now_monotonic();
    CHECK(flash_erase(&dev_u, 0x20000u, SECT_U) == OM_OK, "erase via instrumented backend");
    uint32_t dt = (uint32_t)osal_time_now_monotonic() - t0;
    CHECK(dt >= 5u, "call returned no earlier than backend completion (%u ms)", (unsigned)dt);

    /* T7.3 同设备互斥：两线程并发同设备，后端内在飞调用数恒为 1
     * （两线程各占独立扇区、各持自 pattern——互斥失效时计数与数据同时可见） */
    g_inBackend = 0;
    g_maxInBackend = 0;
    flash_sim_set_delay(&sim_u, 2u);
    ParaArg a1 = {&dev_u, 0x0000u, 0x2000u, 6u, 0u, 0u};
    ParaArg a2 = {&dev_u, 0x2000u, 0x2000u, 6u, 0u, 0u};
    ThreadHandle th1 = test_thread_spawn(flash_para_worker, &a1);
    ThreadHandle th2 = test_thread_spawn(flash_para_worker, &a2);
    CHECK(th1 != 0 && th2 != 0, "spawn same-device workers");
    test_thread_join(th1);
    test_thread_join(th2);
    CHECK(a1.fails == 0 && a2.fails == 0, "same-device workers clean (no torn operation)");
    CHECK(g_maxInBackend == 1, "backend never entered concurrently on one device (peak=%ld)",
          g_maxInBackend);
    CHECK(g_inBackend == 0, "in-flight counter balanced after same-device run");

    flash_sim_set_delay(&sim_u, 0u);
}

/* ===================================================================
 * T8: 后端错误注入——错误传播语义（后端 IO 错误必须到达调用者：
 *     同步返回错误码、失败不落位、擦后校验识别结构性不可用、锁不泄漏）
 * =================================================================== */

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

    /* T8.3 擦后校验：后端"报告成功但不落位"（擦除单元失效）→ UNUSABLE。
     * 与 IO 刻意分开：IO 是这一次操作失败（重试同一区），UNUSABLE 是这块区
     * 已验证不可用（跳过该区）——合用一个码则消费者无从选择策略 */
    flash_sim_set_silent_erase_fail(&sim_u, true);
    CHECK(flash_erase(&dev_u, 0x3F000u, SECT_U) == OM_ERR_FLASH_UNUSABLE,
          "silent erase failure caught by post-erase verify (UNUSABLE, not IO)");
    CHECK(flash_read(&dev_u, 0x3F000u, g_buf, 4u) == OM_OK && g_buf[0] == 0x00,
          "region really unerased (verdict came from the medium, not the report)");
    flash_sim_set_silent_erase_fail(&sim_u, false);
    CHECK(flash_erase(&dev_u, 0x3F000u, SECT_U) == OM_OK, "verify passes once medium recovers");

    /* T8.4 错误路径不泄漏设备锁：注入失败后锁已释放（直接探测锁对象，
     * 不用 flash_* 调用——锁若泄漏，同线程重入会死等） */
    flash_sim_set_fail(&sim_u, true);
    (void)flash_erase(&dev_u, 0x3F000u, SECT_U);
    (void)flash_write(&dev_u, 0x3F000u, pat, sizeof(pat));
    flash_sim_set_fail(&sim_u, false);
    CHECK(dev_u.lock != NULL && osal_mutex_lock(dev_u.lock, 200u) == OSAL_OK,
          "device lock free after backend errors (no leak on error path)");
    osal_mutex_unlock(dev_u.lock);
    CHECK(flash_erase(&dev_u, 0x3F000u, SECT_U) == OM_OK, "device usable after errors");
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

    test_register_find();
    test_read();
    test_erase_uniform();
    test_program();
    test_device_interface();
    test_erase_nonuniform();
    test_cross_device_parallel();
    test_sync_model();
    test_backend_error();

    flash_sim_deinit(&sim_u);
    flash_sim_deinit(&sim_f);

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

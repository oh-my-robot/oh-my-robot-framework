/**
 * @file   spi_nor_test.c
 * @brief  W25Q256JV SPI NOR 适配器 host 测试
 *
 * 与 flash_dev_test 的区别：那些用例把后端直接换成一个内存桩（绕过总线），
 * 本用例让适配器经真实的 SPI 框架（hal_spi.c）与总线对象跑，总线之下才是
 * 器件仿真。因此这里同时覆盖三层：
 *   器件适配器（命令编码、页拆包、等待让出）
 *   SPI 框架（总线注册、配置缓存、片选双路径中的硬件片选路径、完成信号）
 *   两者之间的契约（同步完成、片选跨 transfer 保持）
 *
 * 测试面：
 *   T1 身份与几何        T2 擦除与读回        T3 页内编程
 *   T4 跨页自动拆包      T5 多扇区擦除        T6 4 字节地址（高地址区）
 *   T7 等待路径确实让出  T8 未擦区域按位与    T9 越界语义
 *
 * 返回码：g_fail == 0 退出 0，否则退出 1。
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "devices/flash/spi_nor_w25q256jv.h"
#include "drivers/peripheral/flash/pal_flash_dev.h"
#include "drivers/peripheral/spi/pal_spi_dev.h"

#include "host_gpio_fake.h"
#include "spi_nor_sim.h"

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
 * 夹具：仿真器件 → SPI 总线 → 从设备 → flash 器件
 * =================================================================== */

static SpiNorSim g_sim;
static SpiBus g_bus;
static HalSpiDevice g_spiDev;
static W25q256jvDev g_nor;

#define NOR_NAME "nor0"

/** 片选走硬件路径：csSpec.controller 为 NULL 时框架回落到控制器的 setCs，
 *  仿真据此拿到片选边沿——host 侧没有 GPIO 控制器，这是唯一可行的路径。 */
static const SpiDeviceCfg g_cfg = {
    .csSpec = {.controller = NULL, .offset = 0u, .flags = 0u},
    .mode = SPI_MODE_0,
    .maxHz = 10000000u,
    .dataWidth = SPI_DATA_WIDTH_8,
    .bitOrder = SPI_MSB_FIRST,
    .transferOverheadMs = 5u,
};

static bool fixture_up(void)
{
    spi_nor_sim_init(&g_sim);
    if (!g_sim.mem)
    {
        return false;
    }
    if (spi_bus_register(&g_bus, &g_sim, &g_sim.ops) != OM_OK)
    {
        return false;
    }
    if (spi_device_attach(0u, &g_spiDev, "spi_nor", &g_cfg) != OM_OK)
    {
        return false;
    }
    if (w25q256jv_register(&g_nor, NOR_NAME, &g_spiDev) != OM_OK)
    {
        return false;
    }
    return true;
}

static FlashDev *nor_dev(void)
{
    return flash_find(NOR_NAME);
}

/* ---- 第二条路径：片选走 GPIO 控制器（与板上的接法一致） ---- */

static SpiNorSim g_sim_gpio;
static SpiBus g_bus_gpio;
static HalSpiDevice g_spiDev_gpio;
static W25q256jvDev g_nor_gpio;
static HostGpioFake g_gpio;

#define NOR_GPIO_NAME "nor_gpio_cs"
#define GPIO_CS_OFFSET 4u

static const SpiDeviceCfg g_cfg_gpio = {
    .csSpec = {.controller = "fakepio", .offset = GPIO_CS_OFFSET, .flags = 0u},
    .mode = SPI_MODE_0,
    .maxHz = 10000000u,
    .dataWidth = SPI_DATA_WIDTH_8,
    .bitOrder = SPI_MSB_FIRST,
    .transferOverheadMs = 5u,
};

static bool fixture_up_gpio(void)
{
    spi_nor_sim_init(&g_sim_gpio);
    if (!g_sim_gpio.mem)
    {
        return false;
    }
    if (host_gpio_fake_register(&g_gpio, "fakepio", &g_sim_gpio, GPIO_CS_OFFSET) != OM_OK)
    {
        return false;
    }
    if (spi_bus_register(&g_bus_gpio, &g_sim_gpio, &g_sim_gpio.ops) != OM_OK)
    {
        return false;
    }
    if (spi_device_attach(1u, &g_spiDev_gpio, "spi_nor_gpio", &g_cfg_gpio) != OM_OK)
    {
        return false;
    }
    return w25q256jv_register(&g_nor_gpio, NOR_GPIO_NAME, &g_spiDev_gpio) == OM_OK;
}

/* ===================================================================
 * 用例
 * =================================================================== */

static void test_identity_and_geometry(void)
{
    printf("[T1] identity & geometry\n");

    uint32_t id = 0u;
    CHECK(w25q256jv_read_jedec_id(&g_spiDev, &id) == OM_OK && id == 0xEF4019u,
          "jedec id read over spi (%06X)", (unsigned)id);

    FlashDev *dev = nor_dev();
    CHECK(dev != NULL, "device registered & found by name");

    const FlashGeometry *g = flash_geometry(dev);
    CHECK(g != NULL && g->capacity == 32u * 1024u * 1024u, "capacity = 32MB");
    CHECK(g->sectorSize == 4096u && g->sectorCount == 8192u, "uniform 4KB x 8192");
    CHECK(g->pageSize == 256u, "page = 256B");
    CHECK(g->writeUnit == 1u, "write unit = 1 byte (no alignment constraint)");
    CHECK(g->erasedValue == 0xFFu, "erased value = 0xFF");
    CHECK(g->sectorRegions == NULL, "uniform geometry carries no region table");
}

static void test_erase_and_readback(void)
{
    printf("[T2] erase & read back\n");

    FlashDev *dev = nor_dev();
    uint8_t buf[SPI_NOR_SIM_SECTOR_SIZE];

    uint32_t erase_before = g_sim.eraseOps;

    /* 先写脏，再擦，确认擦除真的经过了总线命令 */
    memset(buf, 0x5Au, sizeof(buf));
    CHECK(flash_write(dev, 0u, buf, sizeof(buf)) == OM_OK, "dirty write ok");
    memset(buf, 0u, sizeof(buf));
    CHECK(flash_read(dev, 0u, buf, sizeof(buf)) == OM_OK && buf[0] == 0x5Au,
          "dirty write read back");

    CHECK(flash_erase(dev, 0u, SPI_NOR_SIM_SECTOR_SIZE) == OM_OK, "sector erase ok");
    CHECK(g_sim.eraseOps == erase_before + 1u, "one sector erase command issued");

    memset(buf, 0u, sizeof(buf));
    CHECK(flash_read(dev, 0u, buf, sizeof(buf)) == OM_OK, "read back after erase");
    bool all_ff = true;
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        if (buf[i] != 0xFFu)
        {
            all_ff = false;
            break;
        }
    }
    CHECK(all_ff, "whole sector reads 0xFF after erase");
}

static void test_program_in_page(void)
{
    printf("[T3] program within one page\n");

    FlashDev *dev = nor_dev();
    uint8_t out[256];
    uint8_t in[256];

    for (size_t i = 0; i < sizeof(out); i++)
    {
        out[i] = (uint8_t)(i * 7u + 3u);
    }

    CHECK(flash_erase(dev, 0x10000u, SPI_NOR_SIM_SECTOR_SIZE) == OM_OK, "prepare sector");
    CHECK(flash_write(dev, 0x10000u, out, sizeof(out)) == OM_OK, "page write ok");

    memset(in, 0, sizeof(in));
    CHECK(flash_read(dev, 0x10000u, in, sizeof(in)) == OM_OK, "read back");
    CHECK(memcmp(out, in, sizeof(out)) == 0, "page content matches byte for byte");
}

static void test_cross_page_split(void)
{
    printf("[T4] cross-page write is split by the driver\n");

    FlashDev *dev = nor_dev();

    /* 起于页内偏移 240，长 300：跨 3 个页边界 */
    const uint32_t base = 0x20000u;
    const uint32_t off = 240u;
    const size_t len = 300u;
    uint8_t out[300];
    uint8_t in[300];

    for (size_t i = 0; i < len; i++)
    {
        out[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
    }

    CHECK(flash_erase(dev, base, SPI_NOR_SIM_SECTOR_SIZE) == OM_OK, "prepare sector");

    uint32_t crossings_before = g_sim.pageCrossings;
    uint32_t prog_before = g_sim.progOps;

    CHECK(flash_write(dev, base + off, out, len) == OM_OK, "cross-page write ok");

    CHECK(g_sim.pageCrossings == crossings_before,
          "no single program crossed a page boundary (%u split programs)",
          (unsigned)(g_sim.progOps - prog_before));
    CHECK(g_sim.progOps > prog_before, "program commands actually issued");

    memset(in, 0, sizeof(in));
    CHECK(flash_read(dev, base + off, in, len) == OM_OK, "read back");
    CHECK(memcmp(out, in, len) == 0, "cross-page content matches (no page wrap)");
}

static void test_multi_sector_erase(void)
{
    printf("[T5] multi-sector erase\n");

    FlashDev *dev = nor_dev();
    uint32_t before = g_sim.eraseOps;

    CHECK(flash_erase(dev, 0x30000u, SPI_NOR_SIM_SECTOR_SIZE * 3u) == OM_OK,
          "erase 3 sectors in one call");
    CHECK(g_sim.eraseOps == before + 3u, "three sector erase commands issued (%u)",
          (unsigned)(g_sim.eraseOps - before));

    uint8_t probe[16];
    memset(probe, 0, sizeof(probe));
    CHECK(flash_read(dev, 0x30000u + SPI_NOR_SIM_SECTOR_SIZE * 2u, probe, sizeof(probe)) ==
                  OM_OK &&
              probe[0] == 0xFFu,
          "last erased sector reads 0xFF");
}

static void test_four_byte_addressing(void)
{
    printf("[T6] 4-byte addressing (region above the 16MB line)\n");

    FlashDev *dev = nor_dev();

    /* 32MB 器件的高半区：3 字节地址无法表达该位置 */
    const uint32_t base = 0x1F00000u; /* 31MB */
    uint8_t out[64];
    uint8_t in[64];

    for (size_t i = 0; i < sizeof(out); i++)
    {
        out[i] = (uint8_t)(0x3Cu + i);
    }

    CHECK(flash_erase(dev, base, SPI_NOR_SIM_SECTOR_SIZE) == OM_OK,
          "erase a sector above the 16MB line");
    CHECK(flash_write(dev, base + 32u, out, sizeof(out)) == OM_OK,
          "program above the 16MB line");

    memset(in, 0, sizeof(in));
    CHECK(flash_read(dev, base + 32u, in, sizeof(in)) == OM_OK, "read back");
    CHECK(memcmp(out, in, sizeof(out)) == 0, "high-region content matches");

    /* 低半区同名位置必须不受影响：地址若被截断成 24 位就会落到这里 */
    uint8_t mirror[64];
    memset(mirror, 0, sizeof(mirror));
    CHECK(flash_read(dev, (base & 0xFFFFFFu) + 32u, mirror, sizeof(mirror)) == OM_OK,
          "read the would-be mirror address");
    CHECK(memcmp(out, mirror, sizeof(out)) != 0,
          "no truncation to 24-bit address (mirror region differs)");
}

static void test_wait_path_yields(void)
{
    printf("[T7] busy wait path\n");

    FlashDev *dev = nor_dev();
    uint32_t seen_before = g_sim.wipSeen;

    CHECK(flash_erase(dev, 0x40000u, SPI_NOR_SIM_SECTOR_SIZE) == OM_OK,
          "erase completes through the wait loop");

    CHECK(g_sim.wipSeen > seen_before,
          "device busy state was actually observed (%u polls saw WIP=1)",
          (unsigned)(g_sim.wipSeen - seen_before));
    CHECK((g_sim.sr1 & 0x01u) == 0u, "device reports ready once the operation ends");
}

static void test_program_bit_semantics(void)
{
    printf("[T8] program can only clear bits\n");

    FlashDev *dev = nor_dev();
    uint8_t data[4] = {0xF0u, 0xF0u, 0xF0u, 0xF0u};
    uint8_t in[4] = {0u, 0u, 0u, 0u};

    /* 目标区不擦，直接写：只能把 1 写成 0，位与结果仍为 0xF0 */
    CHECK(flash_write(dev, 0x50000u, data, sizeof(data)) == OM_OK, "write onto unerased area");
    CHECK(flash_read(dev, 0x50000u, in, sizeof(in)) == OM_OK, "read back");
    CHECK(in[0] == 0xF0u, "ones survived: medium applies bit-and, not overwrite");

    uint8_t zeros[4] = {0x0Fu, 0x0Fu, 0x0Fu, 0x0Fu};
    CHECK(flash_write(dev, 0x50000u, zeros, sizeof(zeros)) == OM_OK, "second write (clearing)");
    CHECK(flash_read(dev, 0x50000u, in, sizeof(in)) == OM_OK, "read back again");
    CHECK(in[0] == 0x00u, "second write cleared the remaining ones");
}

static void test_range_semantics(void)
{
    printf("[T9] out-of-range semantics\n");

    FlashDev *dev = nor_dev();
    uint8_t buf[16];

    CHECK(flash_read(dev, 32u * 1024u * 1024u, buf, sizeof(buf)) == OM_ERR_RANGE,
          "read at capacity -> RANGE");
    CHECK(flash_read(dev, 32u * 1024u * 1024u - 8u, buf, sizeof(buf)) == OM_ERR_RANGE,
          "read crossing capacity -> RANGE");
    CHECK(flash_erase(dev, 32u * 1024u * 1024u, SPI_NOR_SIM_SECTOR_SIZE) == OM_ERR_RANGE,
          "erase at capacity -> RANGE");
    CHECK(flash_read(dev, 0u, NULL, sizeof(buf)) == OM_ERR_INVALID_ARG,
          "null buffer -> INVALID_ARG");
}

static void test_gpio_cs_path(void)
{
    printf("[T10] cs via gpio controller\n");

    /* 挂载期由框架配置片选：输出、推挽、初始释放态 */
    CHECK(g_gpio.configureCalls >= 1u, "framework configured the cs pin at attach");
    CHECK(g_gpio.lastDirection == GPIO_DIR_OUTPUT, "cs pin driven as output");
    CHECK(g_gpio.lastPushPull, "cs pin driven push-pull");
    CHECK(g_gpio.lastInitHigh, "cs initial level is the deasserted one");
    CHECK(g_gpio.levels[GPIO_CS_OFFSET] == 1u, "cs idle high after attach (chip deselected)");

    /* 片选真的随事务翻转，且器件按命令边界解出了数据 */
    uint32_t asserts_before = g_sim_gpio.csAsserts;
    uint32_t id = 0u;
    CHECK(w25q256jv_read_jedec_id(&g_spiDev_gpio, &id) == OM_OK && id == 0xEF4019u,
          "jedec id over the gpio-cs device");
    CHECK(g_sim_gpio.csAsserts > asserts_before, "cs was asserted on the wire (%u times)",
          (unsigned)(g_sim_gpio.csAsserts - asserts_before));
    CHECK(g_gpio.writeCalls > 0u, "pin writes reached the gpio controller");

    /* 走一整轮：擦 → 写 → 读 */
    FlashDev *dev = flash_find(NOR_GPIO_NAME);
    CHECK(dev != NULL, "gpio-cs device registered & found by name");

    uint8_t out[128];
    uint8_t in[128];
    for (size_t i = 0; i < sizeof(out); i++)
    {
        out[i] = (uint8_t)(0x11u + i);
    }
    memset(in, 0, sizeof(in));

    CHECK(flash_erase(dev, 0x80000u, SPI_NOR_SIM_SECTOR_SIZE) == OM_OK,
          "erase over gpio-cs path");
    CHECK(flash_write(dev, 0x80000u, out, sizeof(out)) == OM_OK, "write over gpio-cs path");
    CHECK(flash_read(dev, 0x80000u, in, sizeof(in)) == OM_OK, "read over gpio-cs path");
    CHECK(memcmp(out, in, sizeof(out)) == 0, "content matches over gpio-cs path");
    CHECK(g_gpio.levels[GPIO_CS_OFFSET] == 1u, "cs released after the last transfer");
}

static void test_gpio_cs_rejections(void)
{
    printf("[T11] cs spec rejections at attach\n");

    /* 不存在（或未注册）的 GPIO 控制器 */
    static HalSpiDevice dev_bad_ctrl;
    SpiDeviceCfg cfg = g_cfg_gpio;
    cfg.csSpec.controller = "no_such_pio";
    CHECK(spi_device_attach(1u, &dev_bad_ctrl, "spi_bad_ctrl", &cfg) != OM_OK,
          "attach fails when the gpio controller is absent");

    /* 低有效标志与片选时序约定冲突：断言写 0 会被反转成物理高 */
    static HalSpiDevice dev_active_low;
    SpiDeviceCfg cfg_al = g_cfg_gpio;
    cfg_al.csSpec.flags = GPIO_FLAG_ACTIVE_LOW;
    CHECK(spi_device_attach(1u, &dev_active_low, "spi_active_low", &cfg_al) ==
              OM_ERR_INVALID_ARG,
          "attach rejects an active-low cs spec (would invert assert/release)");
}

/* ===================================================================
 * 入口
 * =================================================================== */

int main(void)
{
    printf("=== SPI NOR (W25Q256JV) adapter over real SPI framework ===\n");

    if (!fixture_up())
    {
        printf("FIXTURE FAILED: bus/device/registration setup did not come up\n");
        return 2;
    }
    if (!fixture_up_gpio())
    {
        printf("FIXTURE FAILED: gpio-cs fixture did not come up\n");
        return 2;
    }

    test_identity_and_geometry();
    test_erase_and_readback();
    test_program_in_page();
    test_cross_page_split();
    test_multi_sector_erase();
    test_four_byte_addressing();
    test_wait_path_yields();
    test_program_bit_semantics();
    test_range_semantics();
    test_gpio_cs_path();
    test_gpio_cs_rejections();

    spi_nor_sim_deinit(&g_sim);
    spi_nor_sim_deinit(&g_sim_gpio);

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}

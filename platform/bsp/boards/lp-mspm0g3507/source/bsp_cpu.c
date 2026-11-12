/*
 * LP_MSPM0G3507 — board-level CPU callbacks (Cortex-M0+, TI DriverLib)
 *
 * 本文件仅提供板级回调函数和 om_board_init()。
 * om_cpu_register() / om_core_init() 由框架 lib/source/core/om_cpu.c 提供。
 *
 * TODO: 替换为基于 TIMG12 的实际时间测量实现
 */

#include "bsp_gpio.h"
#include "bsp_pwm.h"
#include "bsp_tima_pwm.h"
#include "bsp_serial.h"
#include "bsp_spi.h"
#include "core/om_cpu.h"
#include "core/om_interrupt.h"
#include "ti_msp_dl_config.h"

static void mspm0g3507_errhandler(void)
{
    om_hw_disable_interrupt_force();
    while (1) {}
}

static void mspm0g3507_reset(void)
{
    *(volatile uint32_t *)0xE000ED0CU = 0x05FA0004U;
}

static Cputime mspm0g3507_get_cpu_time_s(void)
{
    return 0.0f;  /* TODO: TIMG12 */
}

static CputimeMs mspm0g3507_get_cpu_time_ms(void)
{
    return 0.0f;
}

static CputimeUs mspm0g3507_get_cpu_time_us(void)
{
    return 0ULL;
}

static Cputime mspm0g3507_get_delta_cpu_time_s(CputimeCnt *last_time_cnt)
{
    (void)last_time_cnt;
    return 0.0f;
}

static void mspm0g3507_delay_ms(float ms)
{
    (void)ms;  /* TODO: HW timer */
}

void om_board_init(void)
{
    /* 1. 硬件初始化（SysConfig 生成：时钟 + GPIO + UART + SPI pinmux）*/
    SYSCFG_DL_init();

    /* 2. ICM42688 CS 拉低，确保 SPI 模式（POR 协议检测窗口）*/
    DL_GPIO_clearPins(GPIOB, DL_GPIO_PIN_5);

    /* 3. CPU 注册 */
    static OmBoardInterface iface = {
        .errhandler      = mspm0g3507_errhandler,
        .reset           = mspm0g3507_reset,
        .getCpuTimeS     = mspm0g3507_get_cpu_time_s,
        .getCpuTimeMs    = mspm0g3507_get_cpu_time_ms,
        .getCpuTimeUs    = mspm0g3507_get_cpu_time_us,
        .getDeltaCpuTimeS = mspm0g3507_get_delta_cpu_time_s,
        .delayMs         = mspm0g3507_delay_ms,
    };
    om_cpu_register(32U, &iface);

    /* 4. 外设注册 */
    bsp_gpio_register();
    bsp_pwm_register();
    bsp_tima_pwm_register();
    bsp_serial_register();
    bsp_spi_register();
}

/**
 * @file    main.c
 * @brief   GPIO 硬件在环测试（rm-c-board / STM32F407）
 * @details
 * 测试覆盖：
 * - 引脚输出：方波翻转（示波器验证）
 * - 引脚输入：读取并输出到另一个引脚（跳线回环或 LED 观察）
 * - 中断：双边沿触发 + ISR 回调计数
 * - 端口级：批量读写
 *
 * 硬件准备（按需）：
 * - 测试输出翻转：示波器探头接 TEST_OUT_PIN
 * - 测试输入回环：跳线连接 TEST_OUT_PIN ↔ TEST_IN_PIN
 * - 测试中断：     跳线连接 TEST_IRQ_SRC ↔ TEST_IRQ_PIN
 */

#include "core/om_cpu.h"
#include "core/om_def.h"
#include "drivers/peripheral/gpio/pal_gpio_dev.h"
#include "osal/osal.h"
#include <stdint.h>

/* ===== 用户引脚配置（根据实际硬件修改） ===== */

/* 输出测试引脚 — 用示波器观察翻转波形 */
#define TEST_OUT_PORT  "gpioa"
#define TEST_OUT_OFFSET (5U)

/* 输入测试引脚 — 读电平并镜像输出 */
#define TEST_IN_PORT   "gpioa"
#define TEST_IN_OFFSET (6U)

/* 中断源引脚（输出翻转，模拟外部触发） */
#define TEST_IRQ_SRC_PORT  "gpioa"
#define TEST_IRQ_SRC_OFFSET (7U)

/* 中断目标引脚（接收中断） */
#define TEST_IRQ_PIN_PORT  "gpiob"
#define TEST_IRQ_PIN_OFFSET (0U)

/* 测试参数 */
#define TEST_LOOP_PERIOD_MS   (500U)
#define TEST_TOGGLE_PERIOD_MS (100U)
#define TEST_THREAD_PRIORITY  (4U)
#define TEST_THREAD_STACK     (512U)

/* ===== 静态引脚描述符 ===== */

static const GpioPinSpec out_spec = {TEST_OUT_PORT, TEST_OUT_OFFSET, 0};
static const GpioPinSpec in_spec  = {TEST_IN_PORT,  TEST_IN_OFFSET,  0};
static const GpioPinSpec irq_src_spec = {TEST_IRQ_SRC_PORT, TEST_IRQ_SRC_OFFSET, 0};
static const GpioPinSpec irq_pin_spec = {TEST_IRQ_PIN_PORT, TEST_IRQ_PIN_OFFSET, 0};

/* ===== 中断计数器（ISR 中累加，线程中读取） ===== */

static volatile uint32_t g_irq_count = 0;

static void gpio_irq_callback(void *arg)
{
    (void)arg;
    g_irq_count++;
}

/* ===== 测试线程 ===== */

static void gpio_test_task(void *arg)
{
    (void)arg;

    /* 1. 解析引脚句柄 */
    GpioPin out     = gpio_pin_get(&out_spec);
    GpioPin in      = gpio_pin_get(&in_spec);
    GpioPin irq_src = gpio_pin_get(&irq_src_spec);
    GpioPin irq_pin = gpio_pin_get(&irq_pin_spec);

    if (!gpio_pin_valid(out) || !gpio_pin_valid(in) ||
        !gpio_pin_valid(irq_src) || !gpio_pin_valid(irq_pin)) {
        /* 引脚解析失败 — 检查控制器名和偏移是否在范围内 */
        while (1) {}
    }

    /* 2. 配置输出引脚（推挽、无上下拉、初始低） */
    GpioPinConfig out_cfg = {
        .direction = GPIO_DIR_OUTPUT,
        .pull      = GPIO_PULL_NONE,
        .drive     = GPIO_DRIVE_PUSH_PULL,
        .speed     = GPIO_DRIVE_STRENGTH_LOW,
        .init_high = false,
    };
    gpio_pin_configure(out, &out_cfg);
    gpio_pin_configure(irq_src, &out_cfg);

    /* 3. 配置输入引脚（浮空输入） */
    GpioPinConfig in_cfg = {
        .direction = GPIO_DIR_INPUT,
        .pull      = GPIO_PULL_NONE,
        .drive     = GPIO_DRIVE_PUSH_PULL,
        .speed     = GPIO_DRIVE_STRENGTH_LOW,
        .init_high = false,
    };
    gpio_pin_configure(in, &in_cfg);

    /* 4. 配置中断引脚（输入 + 下拉，确保悬空时为确定电平） */
    GpioPinConfig irq_cfg = {
        .direction = GPIO_DIR_INPUT,
        .pull      = GPIO_PULL_DOWN,
        .drive     = GPIO_DRIVE_PUSH_PULL,
        .speed     = GPIO_DRIVE_STRENGTH_LOW,
        .init_high = false,
    };
    gpio_pin_configure(irq_pin, &irq_cfg);

    /* 5. 注册中断（双边沿触发） */
    OmRet ret = gpio_pin_attach_irq(irq_pin, GPIO_IRQ_EDGE_BOTH,
                                     gpio_irq_callback, NULL);
    if (ret != OM_OK) {
        /* 中断注册失败 — 检查 BSP caps 是否包含双边沿 */
        while (1) {}
    }
    gpio_pin_irq_enable(irq_pin, true);

    /* 6. 端口级测试 */
    GpioPort port_a = gpio_port_get("gpioa");
    if (gpio_port_valid(port_a)) {
        /* 批量读取整个端口初始状态 */
        uint32_t port_state = gpio_port_read(port_a);
        (void)port_state;
    }

    /* 7. 主循环 */
    uint32_t last_print = 0;
    uint8_t out_val = 0;

    while (1) {
        /* 输出引脚翻转 — 方波测试 */
        gpio_pin_toggle(out);
        out_val = gpio_pin_read(out);

        /* 输入引脚电平镜像到中断源引脚（跳线连接 irq_src → irq_pin） */
        uint8_t in_val = gpio_pin_read(in);
        gpio_pin_write(irq_src, in_val);

        /* 每 500ms 打印一次统计（通过调试器观察变量，无需串口） */
        if (g_irq_count != last_print) {
            last_print = g_irq_count;
            /*
             * TODO: 在此处设置调试断点观察 g_irq_count。
             * 由于 irq_src 对 irq_pin 不直接相连，
             * IRQ 计数靠外部跳线 irq_src → irq_pin 触发。
             */
        }

        osal_sleep_ms(TEST_LOOP_PERIOD_MS);
    }
}

/* ===== 入口 ===== */

int main(void)
{
    om_board_init();
    om_core_init();

    OsalThreadAttr attr = {
        .name      = "GpioTest",
        .priority  = TEST_THREAD_PRIORITY,
        .stackSize = TEST_THREAD_STACK * OSAL_STACK_WORD_BYTES,
    };
    OsalThread *task = NULL;
    if (osal_thread_create(&task, &attr, gpio_test_task, NULL) != OSAL_OK) {
        while (1) {}
    }

    return osal_kernel_start();
}

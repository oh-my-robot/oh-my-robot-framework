/**
 * @file    main.c
 * @brief   GPIO 硬件在环测试（rm-a-board / STM32F427）
 * @details
 * 引脚职能（受相邻引脚拓扑约束，仅两条跳线）：
 * - PC2：主循环 5Hz 翻转 →[跳线②]→ PB0（IRQ 触发链路）
 * - PB0：EXTI 双边沿，ISR 中翻转 PF1
 * - PF1：ISR 内翻转（5Hz 方波）→[跳线①]→ PE5（ISR 活性指示器）
 * - PE5：主循环读取（应跟随 PF1，验证 ISR 实际执行）
 *
 * 频率关系（核心判据）：
 * - PC2 主循环方波频率 = 5Hz（周期 200ms，每 100ms 翻转一次）
 * - PC2 每秒产生 10 条边沿（5 上升 + 5 下降）
 * - PB0 双边沿触发，ISR 速率 = 10 次/秒
 * - PF1 在 ISR 内翻转，每秒翻转 10 次 → 方波频率 5Hz（与 PC2 同频）
 * - PE5 通过跳线①跟随 PF1，每秒电平变化 10 次
 *
 * 接线（两条跳线都必须接）：
 * - 跳线①：PF1 ↔ PE5（验证 ISR 翻转效果）
 * - 跳线②：PC2 ↔ PB0（IRQ 触发链路）
 *
 * 示波器探头建议：
 * - CH1 接 PC2：5Hz 基准
 * - CH2 接 PF1：5Hz ISR 活性指示（应与 PC2 严格同频）
 *
 * 通过判据：PF1 与 PC2 同频（5Hz），且 g_irq_count 每 500ms 外层断点
 * 命中时增加约 5（10 次/秒 × 0.5 秒）。
 */

#include "core/om_cpu.h"
#include "core/om_def.h"
#include "drivers/peripheral/gpio/pal_gpio_dev.h"
#include "osal/osal.h"
#include <stdint.h>

/* ===== 用户引脚配置（根据实际硬件修改） ===== */

/* IRQ 源 — 主循环翻转，通过跳线②驱动 PB0 */
#define TEST_SRC_PORT  "gpioc"
#define TEST_SRC_OFFSET (2U)

/* IRQ 接收引脚 — 跳线②接 PC2，EXTI 双边沿 */
#define TEST_IRQ_PIN_PORT  "gpiob"
#define TEST_IRQ_PIN_OFFSET (0U)

/* ISR 频率指示器 — ISR 内翻转，通过跳线①驱动 PE5 */
#define TEST_INDICATOR_PORT  "gpiof"
#define TEST_INDICATOR_OFFSET (1U)

/* ISR 频率读取验证 — 跳线①接 PF1，主循环读取 */
#define TEST_IN_PORT   "gpioe"
#define TEST_IN_OFFSET (5U)

/* 测试参数 */
#define TEST_LOOP_PERIOD_MS   (500U)
#define TEST_TOGGLE_PERIOD_MS (100U)
#define TEST_THREAD_PRIORITY  (4U)
#define TEST_THREAD_STACK     (512U)

/* ===== 静态引脚描述符 ===== */

static const GpioPinSpec src_spec        = {TEST_SRC_PORT, TEST_SRC_OFFSET, 0};
static const GpioPinSpec irq_pin_spec    = {TEST_IRQ_PIN_PORT, TEST_IRQ_PIN_OFFSET, 0};
static const GpioPinSpec indicator_spec  = {TEST_INDICATOR_PORT, TEST_INDICATOR_OFFSET, 0};
static const GpioPinSpec in_spec         = {TEST_IN_PORT, TEST_IN_OFFSET, 0};

/* ===== ISR 共享状态 ===== */

static volatile uint32_t g_irq_count = 0;
static GpioPin g_indicator;   /* ISR 内翻转的引脚句柄，由 task 初始化 */

static void gpio_irq_callback(void *arg)
{
    (void)arg;
    g_irq_count++;
    gpio_pin_toggle(g_indicator);   /* PF1 翻转 — ISR 活性指示 */
}

/* ===== 测试线程 ===== */

static void gpio_test_task(void *arg)
{
    (void)arg;

    /* 1. 解析引脚句柄 */
    GpioPin src, irq_pin, indicator, in;
    if (gpio_pin_get(&src_spec, &src) != OM_OK ||
        gpio_pin_get(&irq_pin_spec, &irq_pin) != OM_OK ||
        gpio_pin_get(&indicator_spec, &indicator) != OM_OK ||
        gpio_pin_get(&in_spec, &in) != OM_OK) {
        /* 引脚解析失败 — 检查控制器名和偏移是否在范围内 */
        while (1) {}
    }

    /* 2. 配置两个输出引脚（PC2 主循环驱动 + PF1 ISR 驱动） */
    GpioPinConfig out_cfg = {
        .direction = GPIO_DIR_OUTPUT,
        .pull      = GPIO_PULL_NONE,
        .drive     = GPIO_DRIVE_PUSH_PULL,
        .speed     = GPIO_DRIVE_STRENGTH_LOW,
        .init_high = false,
    };
    gpio_pin_configure(src, &out_cfg);     /* PC2 */
    gpio_pin_configure(indicator, &out_cfg); /* PF1 */

    /* 3. 配置输入引脚 PE5（下拉，悬空时为确定低电平） */
    GpioPinConfig in_cfg = {
        .direction = GPIO_DIR_INPUT,
        .pull      = GPIO_PULL_DOWN,
        .drive     = GPIO_DRIVE_PUSH_PULL,
        .speed     = GPIO_DRIVE_STRENGTH_LOW,
        .init_high = false,
    };
    gpio_pin_configure(in, &in_cfg);

    /* 4. 配置 IRQ 接收引脚 PB0（输入 + 下拉） */
    GpioPinConfig irq_cfg = {
        .direction = GPIO_DIR_INPUT,
        .pull      = GPIO_PULL_DOWN,
        .drive     = GPIO_DRIVE_PUSH_PULL,
        .speed     = GPIO_DRIVE_STRENGTH_LOW,
        .init_high = false,
    };
    gpio_pin_configure(irq_pin, &irq_cfg);

    /* 5. 暴露 PF1 句柄给 ISR（必须在 attach_irq 之前完成） */
    g_indicator = indicator;

    /* 6. 注册中断（双边沿触发） */
    OmRet ret = gpio_pin_attach_irq(irq_pin, GPIO_IRQ_EDGE_BOTH,
                                     gpio_irq_callback, NULL);
    if (ret != OM_OK) {
        /* 中断注册失败 — 检查 BSP caps 是否包含双边沿 */
        while (1) {}
    }
    gpio_pin_irq_enable(irq_pin, true);

    /* 7. 端口级测试 — 读 GPIOC 初始状态（bit[SRC_OFFSET] 与 src_val 对照） */
    GpioPort port_src = gpio_port_get(TEST_SRC_PORT);
    if (gpio_port_valid(port_src)) {
        uint32_t port_state = gpio_port_read(port_src);
        (void)port_state;  /* 在此断点观察 bit[TEST_SRC_OFFSET] */
    }

    /* 8. 主循环 */
    uint32_t last_print = 0;
    uint8_t prev_in_val = 0;
    uint32_t in_change_count = 0;

    while (1) {
        /* 8.1 内层：100ms 翻转 PC2（5Hz 方波 → PB0 双边沿触发 ISR → PF1 翻转） */
        for (uint32_t i = 0; i < TEST_LOOP_PERIOD_MS / TEST_TOGGLE_PERIOD_MS; i++) {
            gpio_pin_toggle(src);   /* PC2 翻转 → 跳线② → PB0 边沿 → ISR */
            uint8_t src_val = gpio_pin_read(src);

            /* PE5 读 PF1（跳线①）— 应跟随 ISR 翻转节奏，每 100ms 大概率变化 */
            uint8_t in_val = gpio_pin_read(in);
            if (in_val != prev_in_val) {
                prev_in_val = in_val;
                in_change_count++;
            }
            (void)src_val;

            osal_sleep_ms(TEST_TOGGLE_PERIOD_MS);
        }

        /* 8.2 外层：每 500ms 观察 IRQ 计数 + PE5 变化次数 */
        if (g_irq_count != last_print) {
            last_print = g_irq_count;
            /* 在此断点观察：
             * - g_irq_count 每 500ms 增加约 5（ISR 速率 10/秒）
             * - in_change_count 每 500ms 增加约 5（跳线①连通时）
             */
        }
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

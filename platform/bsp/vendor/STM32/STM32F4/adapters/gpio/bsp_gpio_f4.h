/**
 * @file  bsp_gpio_f4.h
 * @brief STM32F4 家族 GPIO BSP 共享适配层：类型 + 家族常量 + 板数据契约
 * @details 板瘦身（见 ADR-0011）：F4 家族一份共享实现，各板只提供端口数据表。
 *
 *          ── 板数据契约（板 opt-in 本适配层后必须提供）────────────────────
 *          板侧 include/bsp_gpio.h（shim）：
 *            #define BSP_GPIO_PORT_COUNT   端口数（= gBspGpio 条目数）
 *            #include "gpio/bsp_gpio_f4.h"
 *          板侧 source/peripherals/gpio/bsp_gpio_data.c：
 *            BspGpio gBspGpio[BSP_GPIO_PORT_COUNT]   端口表（顺序即控制器索引）
 *          适配层文件由板 lua 显式引用（opt-in 铁律，永不进 vendor/chip sources）：
 *            selfreg_sources  += .../adapters/gpio/bsp_gpio_f4.c
 *            override_sources += .../adapters/gpio/bsp_gpio_f4_it.c
 */

#ifndef __BSP_GPIO_F4_H__
#define __BSP_GPIO_F4_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "drivers/peripheral/gpio/pal_gpio_dev.h"
#include "stm32f4xx_hal.h"

/*===========================================================================
 * 家族常量（板可在 include 前 #define 覆盖）
 *===========================================================================*/

#ifndef BSP_GPIO_PINS_PER_PORT
#define BSP_GPIO_PINS_PER_PORT (16U)
#endif

/** 默认 GPIO EXTI 中断优先级（0 最高，15 最低） */
#ifndef BSP_GPIO_IRQ_PRIORITY
#define BSP_GPIO_IRQ_PRIORITY (5U)
#endif

/** F4 EXTI 仅支持边沿触发 */
#define BSP_GPIO_IRQ_CAPS (GPIO_CAP_IRQ_EDGE_RISING | \
                           GPIO_CAP_IRQ_EDGE_FALLING | \
                           GPIO_CAP_IRQ_EDGE_BOTH)

/*===========================================================================
 * 类型
 *===========================================================================*/

typedef struct BspGpio {
    GPIO_TypeDef *port;
    GpioController parent;
    char *name;
    uint8_t irq_priority;
} BspGpio;

#define BSP_GPIO_STATIC_INIT(PORT, NAME) \
    (BspGpio){ (PORT), {0}, (NAME), BSP_GPIO_IRQ_PRIORITY }

/*===========================================================================
 * 板数据契约声明（不完整维度 extern）
 *===========================================================================*/

extern BspGpio gBspGpio[BSP_GPIO_PORT_COUNT]; /* BSP_GPIO_PORT_COUNT 由板 shim 定义 */

void bsp_gpio_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_GPIO_F4_H__ */

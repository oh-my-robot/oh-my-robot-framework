/**
 * @file    bsp_gpio.h
 * @brief   rm-c-board GPIO BSP 配置
 */

#ifndef __BSP_GPIO_H__
#define __BSP_GPIO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "drivers/peripheral/gpio/pal_gpio_dev.h"
#include "stm32f4xx_hal.h"

#define BSP_GPIO_PORT_COUNT    (9U)
#define BSP_GPIO_PINS_PER_PORT (16U)

/** 默认 GPIO EXTI 中断优先级（0 最高，15 最低），可在板级配置中覆盖 */
#ifndef BSP_GPIO_IRQ_PRIORITY
#define BSP_GPIO_IRQ_PRIORITY  (5U)
#endif

/** STM32F4 EXTI 仅支持边沿触发 */
#define BSP_GPIO_IRQ_CAPS      (GPIO_CAP_IRQ_EDGE_RISING  | \
                                GPIO_CAP_IRQ_EDGE_FALLING | \
                                GPIO_CAP_IRQ_EDGE_BOTH)

typedef struct BspGpio {
    GPIO_TypeDef *port;
    GpioController parent;
    char *name;
    uint8_t irq_priority;
} BspGpio;

extern BspGpio gBspGpio[BSP_GPIO_PORT_COUNT];

void bsp_gpio_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_GPIO_H__ */

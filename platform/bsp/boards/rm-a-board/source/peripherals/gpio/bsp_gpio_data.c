/**
 * @file  bsp_gpio_data.c
 * @brief rm-a-board GPIO 板数据（板瘦身：板=数据，供共享适配层 bsp_gpio_f4.c 消费）
 * @details 契约见 gpio/bsp_gpio_f4.h；BSP_GPIO_PORT_COUNT 在板 shim bsp_gpio.h 定义。
 */
#include "bsp_gpio.h"

/* 端口表（顺序即控制器索引，与 EXTI 端口路由一致） */
BspGpio gBspGpio[BSP_GPIO_PORT_COUNT] = {
    BSP_GPIO_STATIC_INIT(GPIOA, "gpioa"),
    BSP_GPIO_STATIC_INIT(GPIOB, "gpiob"),
    BSP_GPIO_STATIC_INIT(GPIOC, "gpioc"),
    BSP_GPIO_STATIC_INIT(GPIOD, "gpiod"),
    BSP_GPIO_STATIC_INIT(GPIOE, "gpioe"),
    BSP_GPIO_STATIC_INIT(GPIOF, "gpiof"),
    BSP_GPIO_STATIC_INIT(GPIOG, "gpiog"),
    BSP_GPIO_STATIC_INIT(GPIOH, "gpioh"),
    BSP_GPIO_STATIC_INIT(GPIOI, "gpioi"),
    BSP_GPIO_STATIC_INIT(GPIOJ, "gpioj"),
};

/* 一致性兜底：BSP_GPIO_PORT_COUNT 必须等于 gBspGpio 条目数（C99 技巧，编译期校验） */
typedef char bsp_gpio_count_ok[(BSP_GPIO_PORT_COUNT == sizeof(gBspGpio) / sizeof(gBspGpio[0])) ? 1 : -1];

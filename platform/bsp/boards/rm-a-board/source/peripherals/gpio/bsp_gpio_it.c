/**
 * @file    bsp_gpio_it.c
 * @brief   rm-a-board GPIO EXTI ISR 覆盖（override_sources）
 */

#include "bsp_gpio.h"

/* ===== EXTI 端口路由（通过 SYSCFG EXTICR 确定） ===== */

static int bsp_gpio_exti_port_index(uint8_t exti_line)
{
    volatile uint32_t *exticr = &SYSCFG->EXTICR[exti_line / 4];
    uint32_t shift = (exti_line % 4) * 4;
    return (int)((*exticr >> shift) & 0xF);
}

static void bsp_gpio_dispatch_exti(uint8_t pin_num)
{
    int port_idx = bsp_gpio_exti_port_index(pin_num);
    if (port_idx >= 0 && port_idx < BSP_GPIO_PORT_COUNT)
        hal_gpio_isr(&gBspGpio[port_idx].parent, pin_num);
}

/* ===== EXTI0 ~ EXTI4（单引脚 ISR） ===== */

void EXTI0_IRQHandler(void)
{
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0);
    bsp_gpio_dispatch_exti(0);
}

void EXTI1_IRQHandler(void)
{
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_1);
    bsp_gpio_dispatch_exti(1);
}

void EXTI2_IRQHandler(void)
{
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_2);
    bsp_gpio_dispatch_exti(2);
}

void EXTI3_IRQHandler(void)
{
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_3);
    bsp_gpio_dispatch_exti(3);
}

void EXTI4_IRQHandler(void)
{
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_4);
    bsp_gpio_dispatch_exti(4);
}

/* ===== EXTI9_5 / EXTI15_10（共享 ISR） ===== */

void EXTI9_5_IRQHandler(void)
{
    for (uint8_t pin = 5; pin <= 9; pin++) {
        uint32_t bitmask = 1U << pin;
        if (__HAL_GPIO_EXTI_GET_IT(bitmask)) {
            __HAL_GPIO_EXTI_CLEAR_IT(bitmask);
            bsp_gpio_dispatch_exti(pin);
        }
    }
}

void EXTI15_10_IRQHandler(void)
{
    for (uint8_t pin = 10; pin <= 15; pin++) {
        uint32_t bitmask = 1U << pin;
        if (__HAL_GPIO_EXTI_GET_IT(bitmask)) {
            __HAL_GPIO_EXTI_CLEAR_IT(bitmask);
            bsp_gpio_dispatch_exti(pin);
        }
    }
}

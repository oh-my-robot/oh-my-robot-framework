/**
 * @file  f4_clk.h
 * @brief STM32F4 家族时钟使能公共件（共享适配层通用）
 * @details 时钟使能宏无法数据化（是宏不是对象），以家族查表 helper 收敛，
 *          供各外设适配层（bsp_*_f4.c）复用——Zephyr clock-control 查表的等价物。
 *          GPIOJ/K 仅存在于大容量 F4（如 F427），以 #if defined() 按芯片守卫。
 */

#ifndef __BSP_F4_CLK_H__
#define __BSP_F4_CLK_H__

#include "stm32f4xx_hal.h"

/** @brief 按端口指针使能 GPIO 时钟（先查后开，避免重复使能） */
static inline void bsp_f4_enable_gpio_clk(GPIO_TypeDef *port)
{
    switch ((uint32_t)port)
    {
    case (uint32_t)GPIOA:
        if (__HAL_RCC_GPIOA_IS_CLK_DISABLED()) __HAL_RCC_GPIOA_CLK_ENABLE();
        break;
    case (uint32_t)GPIOB:
        if (__HAL_RCC_GPIOB_IS_CLK_DISABLED()) __HAL_RCC_GPIOB_CLK_ENABLE();
        break;
    case (uint32_t)GPIOC:
        if (__HAL_RCC_GPIOC_IS_CLK_DISABLED()) __HAL_RCC_GPIOC_CLK_ENABLE();
        break;
    case (uint32_t)GPIOD:
        if (__HAL_RCC_GPIOD_IS_CLK_DISABLED()) __HAL_RCC_GPIOD_CLK_ENABLE();
        break;
    case (uint32_t)GPIOE:
        if (__HAL_RCC_GPIOE_IS_CLK_DISABLED()) __HAL_RCC_GPIOE_CLK_ENABLE();
        break;
    case (uint32_t)GPIOF:
        if (__HAL_RCC_GPIOF_IS_CLK_DISABLED()) __HAL_RCC_GPIOF_CLK_ENABLE();
        break;
    case (uint32_t)GPIOG:
        if (__HAL_RCC_GPIOG_IS_CLK_DISABLED()) __HAL_RCC_GPIOG_CLK_ENABLE();
        break;
    case (uint32_t)GPIOH:
        if (__HAL_RCC_GPIOH_IS_CLK_DISABLED()) __HAL_RCC_GPIOH_CLK_ENABLE();
        break;
    case (uint32_t)GPIOI:
        if (__HAL_RCC_GPIOI_IS_CLK_DISABLED()) __HAL_RCC_GPIOI_CLK_ENABLE();
        break;
#if defined(GPIOJ) /* GPIOJ/K 仅大容量 F4（如 F427），F407 无 */
    case (uint32_t)GPIOJ:
        if (__HAL_RCC_GPIOJ_IS_CLK_DISABLED()) __HAL_RCC_GPIOJ_CLK_ENABLE();
        break;
#endif
#if defined(GPIOK)
    case (uint32_t)GPIOK:
        if (__HAL_RCC_GPIOK_IS_CLK_DISABLED()) __HAL_RCC_GPIOK_CLK_ENABLE();
        break;
#endif
    default:
        break;
    }
}

#endif /* __BSP_F4_CLK_H__ */

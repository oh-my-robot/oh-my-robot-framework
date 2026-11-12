/**
 * @file bsp_gpio.h
 * @brief MSPM0G3507 GPIO BSP — 框架 GpioOps 实现 (DL_GPIO)
 *
 * 端口：GPIOA(PA0-31)、GPIOB(PB0-27)
 * 注册为 "gpioa" / "gpiob"，各 16 引脚
 */

#ifndef __OM_BSP_GPIO_H__
#define __OM_BSP_GPIO_H__

#include "drivers/peripheral/gpio/pal_gpio_dev.h"
#include "ti/devices/msp/msp.h"

#define BSP_GPIO_PORT_COUNT  2

typedef struct BspGpio {
    GPIO_Regs      *port;
    GpioController  parent;
    const char     *name;
    uint8_t         pin_count;     /* 该端口实际引脚数 */
} BspGpio;

void bsp_gpio_register(void);

#endif

/**
 * @file   host_gpio_fake.h
 * @brief  可控 GPIO 控制器（host 测试用）
 *
 * 存在的意义：板上的 SPI 片选走 GPIO 路径，而 host 侧没有 GPIO 硬件。本控制器
 * 把引脚状态收在内存里，并把片选引脚的电平变化转给器件仿真——器件据此划分命令
 * 边界，与真板上"引脚 → 器件"的关系一致。
 *
 * 由此，片选的两条路径都能被覆盖：控制器路径（本文件）与控制器直控路径
 * （csSpec.controller == NULL 时框架回落到 ops->setCs）。
 */

#ifndef HOST_GPIO_FAKE_H
#define HOST_GPIO_FAKE_H

#include <stdbool.h>
#include <stdint.h>

#include "drivers/peripheral/gpio/pal_gpio_dev.h"

#include "spi_nor_sim.h"

#define HOST_GPIO_FAKE_PINS 16u

typedef struct {
    GpioController ctrl;
    GpioOps ops;

    SpiNorSim *csSim; /* 片选电平变化的接收方 */
    uint8_t csOffset; /* 作为片选的引脚偏移 */

    /* 观测（测试断言用） */
    uint32_t configureCalls;
    GpioDirection lastDirection;
    bool lastInitHigh;
    bool lastPushPull;
    uint32_t writeCalls;
    uint8_t levels[HOST_GPIO_FAKE_PINS];
} HostGpioFake;

/** @brief 注册为 GPIO 控制器，并把 cs_offset 的电平变化接到 cs_sim */
OmRet host_gpio_fake_register(HostGpioFake *fake, const char *name, SpiNorSim *cs_sim,
                              uint8_t cs_offset);

#endif /* HOST_GPIO_FAKE_H */

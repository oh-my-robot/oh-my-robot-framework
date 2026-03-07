/*
 * rm-c-board IRQ port implementation (Cortex-M4, GNU toolchain)
 * 注意：该文件仅用于板级/芯片层实现，不进入核心层头文件依赖。
 */

#include "port/om_port_hw.h"
#include "stm32f4xx.h"

uint32_t port_get_primask(void)
{
    return __get_PRIMASK();
}

void port_set_primask(uint32_t primask)
{
    __set_PRIMASK(primask);
}

void port_enable_int(void)
{
    __enable_irq();
}

void port_disable_int(void)
{
    __disable_irq();
}

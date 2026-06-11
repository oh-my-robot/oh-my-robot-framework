#include "port/om_port_hw.h"
#include "stm32h7xx.h"

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

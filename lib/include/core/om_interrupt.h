#ifndef __OM_API_H__
#define __OM_API_H__

#include "port/om_port_compiler.h" /* 编译器相关API */
#include "port/om_port_hw.h"       /* 硬件相关API */
#include <stdint.h>

/* 硬件中断 */
#define om_hw_enable_interrupt_force() port_enable_int()
#define om_hw_disable_interrupt_force() port_disable_int()

#define om_hw_get_primask() port_get_primask()
#define om_hw_set_primask(x) port_set_primask(x)

/* 硬件中断控制 */
static inline uint32_t om_hw_disable_interrupt(void)
{
    uint32_t primask = om_hw_get_primask();
    om_hw_set_primask(PORT_PRIMASK_DISABLED);
    return primask;
}

static inline void om_hw_restore_interrupt(uint32_t primask)
{
    if (primask == PORT_PRIMASK_ENABLED)
        om_hw_set_primask(PORT_PRIMASK_ENABLED);
}

#endif /* __OM_API_H__ */

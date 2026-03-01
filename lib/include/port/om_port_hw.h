#ifndef __OM_PORT__HW_H__
#define __OM_PORT__HW_H__

#include <stdint.h>



#ifdef __cplusplus
extern "C"
{
#endif

#define PORT_PRIMASK_ENABLED (0) // 当PRIMASK寄存器为x时，开启中断
#define PORT_PRIMASK_DISABLED (!PORT_PRIMASK_ENABLED)

#if (PORT_PRIMASK_ENABLED == PORT_PRIMASK_DISABLED)
#error "PORT_PRIMASK_ENABLED should not be equal to PORT_PRIMASK_DISABLED"
#endif

#if defined(OM_PORT_STUB_IRQ)
static inline void port_enable_int(void) { }
static inline void port_disable_int(void) { }
static inline uint32_t port_get_primask(void) { return PORT_PRIMASK_ENABLED; }
static inline void port_set_primask(uint32_t primask) { (void)primask; }
#else
/* 平台需提供以下接口的具体实现（如 Cortex-M 可由 CMSIS 实现） */
void port_enable_int(void);
void port_disable_int(void);
uint32_t port_get_primask(void);
void port_set_primask(uint32_t primask);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __OM_PORT__HW_H__ */

#ifndef __OM_API_H__
#define __OM_API_H__

#include "core/port/om_port_compiler.h"
#include "core/port/om_port_hw.h"
#include <stdint.h>

/** @brief 无条件关全局中断（错误处理用） */
#define om_hw_disable_interrupt_force() port_int_disable()

/** @brief 无条件开全局中断 */
#define om_hw_enable_interrupt_force() port_int_enable()

/** @brief 进入临界区：保存中断状态并关闭，返回恢复密钥 */
static inline port_critical_key_t om_hw_disable_interrupt(void)
{
    return port_critical_enter();
}

/** @brief 退出临界区：恢复到 key 对应的中断状态 */
static inline void om_hw_restore_interrupt(port_critical_key_t key)
{
    port_critical_exit(key);
}

#endif /* __OM_API_H__ */

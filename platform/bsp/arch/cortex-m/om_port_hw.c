/*
 * Cortex-M IRQ port 实现（arch/cortex-m 内核架构级共享）
 * PRIMASK 临界区是 ARMv6-M/ARMv7-M 共同能力（M0+/M3/M4 一致）。
 * include 收敛为 CMSIS 通用 intrinsics 头 cmsis_compiler.h（零 vendor 依赖，
 * gnu-rm/armclang/TI-Clang 均适用）。
 * 接入：板 lua sources 引用本文件（板瘦身 opt-in 铁律）。
 */
#include "core/port/om_port_hw.h"
#include "cmsis_compiler.h"

port_critical_key_t port_critical_enter(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return (port_critical_key_t)primask;
}

void port_critical_exit(port_critical_key_t key)
{
    __set_PRIMASK((uint32_t)key);
}

void port_int_disable(void)
{
    __disable_irq();
}

void port_int_enable(void)
{
    __enable_irq();
}

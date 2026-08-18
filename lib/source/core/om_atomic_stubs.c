/**
 * @file      om_atomic_stubs.c
 * @brief     __atomic_* 编译器内置函数的软件兼容实现
 *
 * 当目标平台缺乏硬件原子指令时，编译器会将 __atomic_* 调用降级为
 * 库函数调用。本文件提供这些库函数的实现：通过临界区保护（关中断 →
 * 读-改-写 → 恢复中断）保证单核场景下的原子性。
 *
 * 编译守卫：OM_PORT_SOFTWARE_ATOMICS
 *   该宏由 port 层（om_port_compiler.h）根据目标架构声明。
 *   仅当目标架构缺乏硬件原子指令时置 1，本文件才产出符号。
 *
 *   有硬件原子指令的平台（ARMv7-M、RISC-V A 扩展等）由编译器
 *   自带运行时提供 __atomic_* 实现，本文件必须静默跳过，
 *   否则链接期符号冲突。
 *
 *   新增架构时：在 om_port_compiler.h 的「架构能力声明」段
 *   追加对应的 OM_PORT_SOFTWARE_ATOMICS 条件。
 *
 * 临界区：om_hw_disable_interrupt() / om_hw_restore_interrupt()
 * 由平台 port 层提供具体中断屏蔽策略。
 */

#include <stdint.h>
#include "core/om_interrupt.h"

/*===========================================================================
 * 白名单守卫：仅缺乏硬件原子指令的架构参与编译
 *===========================================================================*/

#if OM_PORT_SOFTWARE_ATOMICS

#define CRITICAL_ENTER(key) uint32_t key = om_hw_disable_interrupt()
#define CRITICAL_EXIT(key)  om_hw_restore_interrupt(key)

/* --- 1-byte atomics --- */

uint8_t __atomic_load_1(const volatile void *ptr, int memorder)
{
    (void)memorder;
    CRITICAL_ENTER(k);
    uint8_t val = *(const volatile uint8_t *)ptr;
    CRITICAL_EXIT(k);
    return val;
}

void __atomic_store_1(volatile void *ptr, uint8_t val, int memorder)
{
    (void)memorder;
    CRITICAL_ENTER(k);
    *(volatile uint8_t *)ptr = val;
    CRITICAL_EXIT(k);
}

uint8_t __atomic_compare_exchange_1(volatile void *ptr, void *expected,
                                    uint8_t desired, int weak,
                                    int success, int failure)
{
    (void)weak;
    (void)success;
    (void)failure;
    CRITICAL_ENTER(k);
    uint8_t *exp = (uint8_t *)expected;
    uint8_t cur  = *(volatile uint8_t *)ptr;
    uint8_t ok;
    if (cur == *exp) {
        *(volatile uint8_t *)ptr = desired;
        ok                       = 1;
    } else {
        *exp = cur;
        ok   = 0;
    }
    CRITICAL_EXIT(k);
    return ok;
}

/* --- 4-byte atomics --- */

uint32_t __atomic_load_4(const volatile void *ptr, int memorder)
{
    (void)memorder;
    CRITICAL_ENTER(k);
    uint32_t val = *(const volatile uint32_t *)ptr;
    CRITICAL_EXIT(k);
    return val;
}

void __atomic_store_4(volatile void *ptr, uint32_t val, int memorder)
{
    (void)memorder;
    CRITICAL_ENTER(k);
    *(volatile uint32_t *)ptr = val;
    CRITICAL_EXIT(k);
}

uint32_t __atomic_fetch_or_4(volatile void *ptr, uint32_t val, int memorder)
{
    (void)memorder;
    CRITICAL_ENTER(k);
    uint32_t old              = *(volatile uint32_t *)ptr;
    *(volatile uint32_t *)ptr = old | val;
    CRITICAL_EXIT(k);
    return old;
}

uint32_t __atomic_fetch_and_4(volatile void *ptr, uint32_t val, int memorder)
{
    (void)memorder;
    CRITICAL_ENTER(k);
    uint32_t old              = *(volatile uint32_t *)ptr;
    *(volatile uint32_t *)ptr = old & val;
    CRITICAL_EXIT(k);
    return old;
}

uint32_t __atomic_fetch_add_4(volatile void *ptr, uint32_t val, int memorder)
{
    (void)memorder;
    CRITICAL_ENTER(k);
    uint32_t old              = *(volatile uint32_t *)ptr;
    *(volatile uint32_t *)ptr = old + val;
    CRITICAL_EXIT(k);
    return old;
}

uint32_t __atomic_fetch_sub_4(volatile void *ptr, uint32_t val, int memorder)
{
    (void)memorder;
    CRITICAL_ENTER(k);
    uint32_t old              = *(volatile uint32_t *)ptr;
    *(volatile uint32_t *)ptr = old - val;
    CRITICAL_EXIT(k);
    return old;
}

#endif /* OM_PORT_SOFTWARE_ATOMICS */

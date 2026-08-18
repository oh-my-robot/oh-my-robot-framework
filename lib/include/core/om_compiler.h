/**
 * @file  om_compiler.h
 * @brief 编译器特性抽象统一入口
 * @details 包含 port/om_port_compiler.h 的实现，向上层暴露 OM_ 前缀宏。
 *          框架上层代码只用 OM_SECTION / OM_USED / OM_WEAK，不直接碰
 *          __attribute__ 或 #ifdef __ICCARM__。
 */

#ifndef __OM_COMPILER_H__
#define __OM_COMPILER_H__

#include "port/om_port_compiler.h"

/* 编译器特性 —— 统一命名，屏蔽 GCC/Clang/armclang/IAR/MSVC 差异 */
#define OM_SECTION(x) __port_section(x)
#define OM_USED       __port_used
#define OM_WEAK       __port_weak
#define OM_ALIGN(n)   __port_align(n)
#define OM_NORETURN   __port_noreturn
#define OM_PACKED     __port_packed

#endif /* __OM_COMPILER_H__ */

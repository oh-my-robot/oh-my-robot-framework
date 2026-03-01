#ifndef __OM_PORT_COMPILER_H__
#define __OM_PORT_COMPILER_H__

#ifdef __cplusplus
extern "C"
{
#endif

// 编译器类型检测
#if defined(_MSC_VER)
#define AW_COMPILER_MSVC
#elif defined(__ARMCC_VERSION)
// ARM Compiler 6 (基于 Clang)
#if __ARMCC_VERSION >= 6000000
#define AW_COMPILER_AC6
#define AW_COMPILER_GCC_LIKE
#define AW_COMPILER AW_COMPILER_AC6
#else
// ARM Compiler 5
#define AW_COMPILER_AC5
#define AW_COMPILER AW_COMPILER_AC5
#endif
#elif defined(__CC_ARM)
// ARM Compiler 5 (标准宏)
#define AW_COMPILER_AC5
#define AW_COMPILER AW_COMPILER_AC5
#elif defined(__clang__) || defined(__GNUC__)
#define AW_COMPILER_GCC_LIKE
#define AW_COMPILER AW_COMPILER_GCC_LIKE
#else
#error "port/compiler: [Err]: Unsupported compiler"
#endif

#define AW_STR_HELPER(x) #x
#define AW_STR(x) AW_STR_HELPER(x)

// MSVC 实现
#if defined(_MSC_VER)
// MSVC 的 message 输出不自动包含行号，我们可以手动拼装
#define AW_NOTE(msg) __pragma(message(__FILE__ "(" AW_STR(__LINE__) "): [Note] " msg))
// GCC / Clang / ARM6 (AC6) 实现
#elif defined(__GNUC__) || defined(__clang__)
// _Pragma 是 C99 标准操作符
#define AW_NOTE(msg) _Pragma(AW_STR(message("[Note] " msg)))
// Fallback to nothing
#else
#define AW_NOTE(msg)
#endif

#if defined(AW_COMPILER_GCC_LIKE) || defined(__ARMCC_VERSION)
#define __port_attribute(x) __attribute__(x)
#define __port_used __port_attribute((used))
#define __port_weak __port_attribute((weak))
#define __port_section(x) __port_attribute((section(x)))
#define __port_align(n) __port_attribute((aligned(n)))
#define __port_noreturn __port_attribute((noreturn))
#define __port_packed __port_attribute((packed))
#elif defined(_MSC_VER)
#define __port_attribute(x)
#define __port_used
#define __port_weak
#define __port_section(x)
#define __port_align(n) __declspec(align(n))
#define __port_noreturn __declspec(noreturn)
#define __port_packed
#else
#define __port_attribute(x)
#define __port_used
#define __port_weak
#define __port_section(x)
#define __port_align(n)
#define __port_noreturn
#define __port_packed
#endif

/* 兼容别名，统一映射到 __port_* 主宏。 */
#define PORT_ATTRIBUTE(x) __port_attribute(x)
#define PORT_USED __port_used
#define PORT_WEAK __port_weak
#define PORT_SECTION(x) __port_section(x)
#define PORT_ALIGN(n) __port_align(n)
#define PORT_NORETURN __port_noreturn
#define PORT_PACKED __port_packed

#ifdef __cplusplus
}
#endif

#endif // __OM_PORT_COMPILER_H__

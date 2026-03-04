#ifndef AW_ATOMIC_BASE_H
#define AW_ATOMIC_BASE_H

#include "port/om_port_compiler.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// 1. C 标准与编译器检测
// ============================================================================

// 检测 C11 及以上版本的原子操作支持
// 如果定义了 __STDC_NO_ATOMICS__, 后续将直接使用标准库接口
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#define AW_USE_STDATOMIC
#endif

/**
 * AC6 (基于 Clang) 完美兼容 GCC 原子 Builtin
 * 因此代码是和gcc一样的
 * MSVC则引入特殊依赖
 */
#if defined(AW_COMPILER_MSVC)
#include <intrin.h>
#include <windows.h>
#elif defined(AW_COMPILER_GCC_LIKE) || defined(AW_COMPILER_AC6) || defined(AW_COMPILER_AC5)
// AW_NOTE("It's a test")
#else
// 如果既不是 C11 也不支持已知编译器，则无法编译
#ifndef AW_USE_STDATOMIC
#error "om_atomics: Unsupported compiler and C standard version < C11"
#endif
#endif

// ============================================================================
// 2. 内存模型定义 (Memory Order)
// ============================================================================

#ifdef AW_USE_STDATOMIC
// --- C11 标准库模式 ---
#include <stdatomic.h>

// 直接映射标准类型，确保类型兼容性
typedef memory_order OmMemoryOrder;

// 映射宏常量
#define AW_MO_RELAXED memory_order_relaxed
#define AW_MO_CONSUME memory_order_consume
#define AW_MO_ACQUIRE memory_order_acquire
#define AW_MO_RELEASE memory_order_release
#define AW_MO_ACQ_REL memory_order_acq_rel
#define AW_MO_SEQ_CST memory_order_seq_cst

#else
// --- 传统/回退模式 ---
// 定义与 C11 类似的枚举，供特定编译器的实现使用
typedef enum
{
    AW_MO_RELAXED = 0,
    AW_MO_CONSUME = 1,
    AW_MO_ACQUIRE = 2,
    AW_MO_RELEASE = 3,
    AW_MO_ACQ_REL = 4,
    AW_MO_SEQ_CST = 5
} OmMemoryOrder;

#endif

// ============================================================================
// 3. 原子类型定义 (统一声明方式)
// ============================================================================

/*
 * 用于声明原子变量的宏。
 * * 示例:
 * OmAtomicInt counter = AW_ATOMIC_VAR_INIT(0);
 * OmAtomicLong flag;
 * * 原理:
 * - C11: 使用 _Atomic 关键字修饰 (例如 _Atomic int)。
 * - MSVC/Legacy: 使用 volatile 关键字修饰 (例如 volatile int)，
 * 因为这些编译器的原子操作通常接受 volatile 指针。
 */

#ifdef AW_USE_STDATOMIC
// C11 标准模式
#define OM_ATOMIC_T(type) _Atomic(type)
#define AW_ATOMIC_VAR_INIT(val) (val) // ATOMIC_VAR_INIT(val)在C11中是标准，但在C17后就被放弃了，因此采用直接返回值
#else
// MSVC / GCC Legacy / AC5 模式
// 在这些编译器中，原子操作函数通常期望传入 volatile 指针
#define OM_ATOMIC_T(type) volatile type
#define AW_ATOMIC_VAR_INIT(val) (val)
#endif

// 常用原子类型别名
typedef OM_ATOMIC_T(int) OmAtomicInt;
typedef OM_ATOMIC_T(unsigned int) OmAtomicUint;
typedef OM_ATOMIC_T(long) OmAtomicLong;
typedef OM_ATOMIC_T(unsigned long) OmAtomicUlong;
typedef OM_ATOMIC_T(long long) OmAtomicLlong;
typedef OM_ATOMIC_T(unsigned long long) OmAtomicUllong;
typedef OM_ATOMIC_T(void*) OmAtomicPtr;
typedef OM_ATOMIC_T(size_t) OmAtomicSize;

// ============================================================================
// 4. 辅助宏
// ============================================================================
#define AW_INLINE static inline

#endif // AW_ATOMIC_BASE_H

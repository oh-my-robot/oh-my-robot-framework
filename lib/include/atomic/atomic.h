#ifndef AW_ATOMICS_H
#define AW_ATOMICS_H

#include "atomic_base.h"

// ============================================================================
// 编译器后端选择逻辑
// ============================================================================

// 如果环境支持 C11 stdatomic，则不引入特定编译器的后端头文件
#ifndef AW_USE_STDATOMIC
#include "atomic_ac5.h"
#include "atomic_gcc.h"
#include "atomic_msvc.h"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    // ============================================================================
    // C Interface
    // ============================================================================

#ifdef AW_USE_STDATOMIC

// ========================================================================
// 分支 A: C11 标准库实现 (优先使用)
// ========================================================================

// stdatomic.h 已在 atomic_base.h 中包含 (如果 AW_USE_STDATOMIC 被定义)

// 1. Load
#define OM_LOAD(ptr, order) atomic_load_explicit(ptr, order)

// 2. Store
#define OM_STORE(ptr, val, order) atomic_store_explicit(ptr, val, order)

// 3. Exchange
#define OM_EXCHANGE(ptr, val, order) atomic_exchange_explicit(ptr, val, order)

// 4. CAS
// atomic_compare_exchange_strong 返回 bool，与 om_cas 语义一致
// 注意：stdatomic 要求 expected 参数为指针
#define OM_CAS(ptr, expected_ptr, desired, success_order, fail_order)                                                                      \
    atomic_compare_exchange_strong_explicit(ptr, expected_ptr, desired, success_order, fail_order)

// 5. Arithmetic
#define OM_FETCH_ADD(ptr, val, order) atomic_fetch_add_explicit(ptr, val, order)

#define OM_FETCH_SUB(ptr, val, order) atomic_fetch_sub_explicit(ptr, val, order)

// 6. Bitwise
#define OM_FETCH_AND(ptr, val, order) atomic_fetch_and_explicit(ptr, val, order)

#define OM_FETCH_OR(ptr, val, order) atomic_fetch_or_explicit(ptr, val, order)

#define OM_FETCH_XOR(ptr, val, order) atomic_fetch_xor_explicit(ptr, val, order)

// 7. Fences
#define OM_THREAD_FENCE(order) atomic_thread_fence(order)

#define OM_SIGNAL_FENCE(order) atomic_signal_fence(order)

/* Keep lowercase backend names available for atomic_simple.h and existing users. */
#define om_load(ptr, order) OM_LOAD(ptr, order)
#define om_store(ptr, val, order) OM_STORE(ptr, val, order)
#define om_exchange(ptr, val, order) OM_EXCHANGE(ptr, val, order)
#define om_cas(ptr, expected_ptr, desired, success_order, fail_order) \
    OM_CAS(ptr, expected_ptr, desired, success_order, fail_order)
#define om_fetch_add(ptr, val, order) OM_FETCH_ADD(ptr, val, order)
#define om_fetch_sub(ptr, val, order) OM_FETCH_SUB(ptr, val, order)
#define om_fetch_and(ptr, val, order) OM_FETCH_AND(ptr, val, order)
#define om_fetch_or(ptr, val, order) OM_FETCH_OR(ptr, val, order)
#define om_fetch_xor(ptr, val, order) OM_FETCH_XOR(ptr, val, order)
#define om_thread_fence(order) OM_THREAD_FENCE(order)
#define om_signal_fence(order) OM_SIGNAL_FENCE(order)

#else

// ========================================================================
// 分支 B: 特定编译器回退实现 (Legacy / Fallback)
// ========================================================================
/**
 *  _Generic不会识别volatile type，因此使用 逗号表达式。结果是右操作数的值，
 *  并且执行了左值到右值转换，这个转换会自动剥离 volatile 和 atomic 限定符
 *  (volatile type -> type)，当然这一切只发生在编译器宏展开阶段，
 *  只是为了查找到要展开的代码，不会改变原本的数据类型(即实际上参与运算的数据类型还是volatile type)，
 *  因此不会影响原子性
 */
// 补充定义：防止 MSVC 宏展开时找不到 CAST 宏
#ifndef _AW_CAST_8
#define _AW_CAST_8(ptr) ((volatile char*)(ptr))
#define _AW_CAST_16(ptr) ((volatile short*)(ptr))
#define _AW_CAST_32(ptr) ((volatile long*)(ptr))
#define _AW_CAST_64(ptr) ((volatile long long*)(ptr))
#endif

// --- 1. Load ---
#if defined(AW_COMPILER_GCC_LIKE) || defined(AW_COMPILER_AC5)
#define om_load(ptr, order) _om_impl_load(ptr, order)
#elif defined(AW_COMPILER_MSVC)
#define om_load(ptr, order)                                                                                                                \
    _Generic((0, *(ptr)),                                                                                                                  \
        char: _om_msvc_load_8(_AW_CAST_8(ptr), order),                                                                                     \
        signed char: _om_msvc_load_8(_AW_CAST_8(ptr), order),                                                                              \
        unsigned char: _om_msvc_load_8(_AW_CAST_8(ptr), order),                                                                            \
        short: _om_msvc_load_16(_AW_CAST_16(ptr), order),                                                                                  \
        unsigned short: _om_msvc_load_16(_AW_CAST_16(ptr), order),                                                                         \
        int: _om_msvc_load_32(_AW_CAST_32(ptr), order),                                                                                    \
        unsigned int: _om_msvc_load_32(_AW_CAST_32(ptr), order),                                                                           \
        long: _om_msvc_load_32(_AW_CAST_32(ptr), order),                                                                                   \
        unsigned long: _om_msvc_load_32(_AW_CAST_32(ptr), order),                                                                          \
        long long: _om_msvc_load_64(_AW_CAST_64(ptr), order),                                                                              \
        unsigned long long: _om_msvc_load_64(_AW_CAST_64(ptr), order),                                                                     \
        void*: _om_msvc_load_64(_AW_CAST_64(ptr), order),                                                                                  \
        default: _om_msvc_load_64(_AW_CAST_64(ptr), order))
#endif

// --- 2. Store ---
#if defined(AW_COMPILER_GCC_LIKE) || defined(AW_COMPILER_AC5)
#define om_store(ptr, val, order) _om_impl_store(ptr, val, order)
#elif defined(AW_COMPILER_MSVC)
#define om_store(ptr, val, order)                                                                                                          \
    _Generic((0, *(ptr)),                                                                                                                  \
        char: _om_msvc_store_8(_AW_CAST_8(ptr), (char)(val), order),                                                                       \
        signed char: _om_msvc_store_8(_AW_CAST_8(ptr), (char)(val), order),                                                                \
        unsigned char: _om_msvc_store_8(_AW_CAST_8(ptr), (char)(val), order),                                                              \
        short: _om_msvc_store_16(_AW_CAST_16(ptr), (short)(val), order),                                                                   \
        unsigned short: _om_msvc_store_16(_AW_CAST_16(ptr), (short)(val), order),                                                          \
        int: _om_msvc_store_32(_AW_CAST_32(ptr), (long)(val), order),                                                                      \
        unsigned int: _om_msvc_store_32(_AW_CAST_32(ptr), (long)(val), order),                                                             \
        long: _om_msvc_store_32(_AW_CAST_32(ptr), (long)(val), order),                                                                     \
        unsigned long: _om_msvc_store_32(_AW_CAST_32(ptr), (long)(val), order),                                                            \
        long long: _om_msvc_store_64(_AW_CAST_64(ptr), (long long)(val), order),                                                           \
        unsigned long long: _om_msvc_store_64(_AW_CAST_64(ptr), (long long)(val), order),                                                  \
        void*: _om_msvc_store_64(_AW_CAST_64(ptr), (long long)(val), order),                                                               \
        default: _om_msvc_store_64(_AW_CAST_64(ptr), (long long)(val), order))
#endif

// --- 3. Exchange ---
#if defined(AW_COMPILER_GCC_LIKE) || defined(AW_COMPILER_AC5)
#define om_exchange(ptr, val, order) _om_impl_exchange(ptr, val, order)
#elif defined(AW_COMPILER_MSVC)
#define om_exchange(ptr, val, order)                                                                                                       \
    _Generic((0, *(ptr)),                                                                                                                  \
        char: _om_msvc_exchange_8(_AW_CAST_8(ptr), (char)(val), order),                                                                    \
        signed char: _om_msvc_exchange_8(_AW_CAST_8(ptr), (char)(val), order),                                                             \
        unsigned char: _om_msvc_exchange_8(_AW_CAST_8(ptr), (char)(val), order),                                                           \
        short: _om_msvc_exchange_16(_AW_CAST_16(ptr), (short)(val), order),                                                                \
        unsigned short: _om_msvc_exchange_16(_AW_CAST_16(ptr), (short)(val), order),                                                       \
        int: _om_msvc_exchange_32(_AW_CAST_32(ptr), (long)(val), order),                                                                   \
        unsigned int: _om_msvc_exchange_32(_AW_CAST_32(ptr), (long)(val), order),                                                          \
        long: _om_msvc_exchange_32(_AW_CAST_32(ptr), (long)(val), order),                                                                  \
        unsigned long: _om_msvc_exchange_32(_AW_CAST_32(ptr), (long)(val), order),                                                         \
        long long: _om_msvc_exchange_64(_AW_CAST_64(ptr), (long long)(val), order),                                                        \
        unsigned long long: _om_msvc_exchange_64(_AW_CAST_64(ptr), (long long)(val), order),                                               \
        void*: _om_msvc_exchange_64(_AW_CAST_64(ptr), (long long)(val), order),                                                            \
        default: _om_msvc_exchange_64(_AW_CAST_64(ptr), (long long)(val), order))
#endif

// --- 4. CAS ---
#if defined(AW_COMPILER_GCC_LIKE) || defined(AW_COMPILER_AC5)
#define om_cas(ptr, expected_ptr, desired, success_order, fail_order) _om_impl_cas(ptr, expected_ptr, desired, success_order, fail_order)
#elif defined(AW_COMPILER_MSVC)
#define om_cas(ptr, expected_ptr, desired, success_order, fail_order)                                                                      \
    _Generic((0, *(ptr)),                                                                                                                  \
        char: _om_msvc_cas_8(_AW_CAST_8(ptr), (char*)(expected_ptr), (char)(desired), success_order, fail_order),                          \
        signed char: _om_msvc_cas_8(_AW_CAST_8(ptr), (char*)(expected_ptr), (char)(desired), success_order, fail_order),                   \
        unsigned char: _om_msvc_cas_8(_AW_CAST_8(ptr), (char*)(expected_ptr), (char)(desired), success_order, fail_order),                 \
        short: _om_msvc_cas_16(_AW_CAST_16(ptr), (short*)(expected_ptr), (short)(desired), success_order, fail_order),                     \
        unsigned short: _om_msvc_cas_16(_AW_CAST_16(ptr), (short*)(expected_ptr), (short)(desired), success_order, fail_order),            \
        int: _om_msvc_cas_32(_AW_CAST_32(ptr), (long*)(expected_ptr), (long)(desired), success_order, fail_order),                         \
        unsigned int: _om_msvc_cas_32(_AW_CAST_32(ptr), (long*)(expected_ptr), (long)(desired), success_order, fail_order),                \
        long: _om_msvc_cas_32(_AW_CAST_32(ptr), (long*)(expected_ptr), (long)(desired), success_order, fail_order),                        \
        unsigned long: _om_msvc_cas_32(_AW_CAST_32(ptr), (long*)(expected_ptr), (long)(desired), success_order, fail_order),               \
        long long: _om_msvc_cas_64(_AW_CAST_64(ptr), (long long*)(expected_ptr), (long long)(desired), success_order, fail_order),         \
        unsigned long long: _om_msvc_cas_64(_AW_CAST_64(ptr), (long long*)(expected_ptr), (long long)(desired), success_order,             \
                                            fail_order),                                                                                   \
        void*: _om_msvc_cas_64(_AW_CAST_64(ptr), (long long*)(expected_ptr), (long long)(desired), success_order, fail_order),             \
        default: _om_msvc_cas_64(_AW_CAST_64(ptr), (long long*)(expected_ptr), (long long)(desired), success_order, fail_order))
#endif

// --- 5. Arithmetic ---
#if defined(AW_COMPILER_GCC_LIKE) || defined(AW_COMPILER_AC5)
#define om_fetch_add(ptr, val, order) _om_impl_fetch_add(ptr, val, order)
#define om_fetch_sub(ptr, val, order) _om_impl_fetch_sub(ptr, val, order)
#elif defined(AW_COMPILER_MSVC)
#define om_fetch_add(ptr, val, order)                                                                                                      \
    _Generic((0, *(ptr)),                                                                                                                  \
        char: _om_msvc_fetch_add_8(_AW_CAST_8(ptr), (char)(val), order),                                                                   \
        signed char: _om_msvc_fetch_add_8(_AW_CAST_8(ptr), (char)(val), order),                                                            \
        unsigned char: _om_msvc_fetch_add_8(_AW_CAST_8(ptr), (char)(val), order),                                                          \
        short: _om_msvc_fetch_add_16(_AW_CAST_16(ptr), (short)(val), order),                                                               \
        unsigned short: _om_msvc_fetch_add_16(_AW_CAST_16(ptr), (short)(val), order),                                                      \
        int: _om_msvc_fetch_add_32(_AW_CAST_32(ptr), (long)(val), order),                                                                  \
        unsigned int: _om_msvc_fetch_add_32(_AW_CAST_32(ptr), (long)(val), order),                                                         \
        long: _om_msvc_fetch_add_32(_AW_CAST_32(ptr), (long)(val), order),                                                                 \
        unsigned long: _om_msvc_fetch_add_32(_AW_CAST_32(ptr), (long)(val), order),                                                        \
        long long: _om_msvc_fetch_add_64(_AW_CAST_64(ptr), (long long)(val), order),                                                       \
        unsigned long long: _om_msvc_fetch_add_64(_AW_CAST_64(ptr), (long long)(val), order))
#define om_fetch_sub(ptr, val, order) om_fetch_add(ptr, -(val), order)
#endif

// --- 6. Bitwise ---
#if defined(AW_COMPILER_GCC_LIKE) || defined(AW_COMPILER_AC5)
#define om_fetch_and(ptr, val, order) _om_impl_fetch_and(ptr, val, order)
#define om_fetch_or(ptr, val, order) _om_impl_fetch_or(ptr, val, order)
#define om_fetch_xor(ptr, val, order) _om_impl_fetch_xor(ptr, val, order)
#elif defined(AW_COMPILER_MSVC)
#define om_fetch_and(ptr, val, order)                                                                                                      \
    _Generic((0, *(ptr)),                                                                                                                  \
        char: _om_msvc_fetch_and_8(_AW_CAST_8(ptr), (char)(val), order),                                                                   \
        signed char: _om_msvc_fetch_and_8(_AW_CAST_8(ptr), (char)(val), order),                                                            \
        unsigned char: _om_msvc_fetch_and_8(_AW_CAST_8(ptr), (char)(val), order),                                                          \
        short: _om_msvc_fetch_and_16(_AW_CAST_16(ptr), (short)(val), order),                                                               \
        unsigned short: _om_msvc_fetch_and_16(_AW_CAST_16(ptr), (short)(val), order),                                                      \
        int: _om_msvc_fetch_and_32(_AW_CAST_32(ptr), (long)(val), order),                                                                  \
        unsigned int: _om_msvc_fetch_and_32(_AW_CAST_32(ptr), (long)(val), order),                                                         \
        long: _om_msvc_fetch_and_32(_AW_CAST_32(ptr), (long)(val), order),                                                                 \
        unsigned long: _om_msvc_fetch_and_32(_AW_CAST_32(ptr), (long)(val), order),                                                        \
        long long: _om_msvc_fetch_and_64(_AW_CAST_64(ptr), (long long)(val), order),                                                       \
        unsigned long long: _om_msvc_fetch_and_64(_AW_CAST_64(ptr), (long long)(val), order))
#define om_fetch_or(ptr, val, order)                                                                                                       \
    _Generic((0, *(ptr)),                                                                                                                  \
        char: _om_msvc_fetch_or_8(_AW_CAST_8(ptr), (char)(val), order),                                                                    \
        signed char: _om_msvc_fetch_or_8(_AW_CAST_8(ptr), (char)(val), order),                                                             \
        unsigned char: _om_msvc_fetch_or_8(_AW_CAST_8(ptr), (char)(val), order),                                                           \
        short: _om_msvc_fetch_or_16(_AW_CAST_16(ptr), (short)(val), order),                                                                \
        unsigned short: _om_msvc_fetch_or_16(_AW_CAST_16(ptr), (short)(val), order),                                                       \
        int: _om_msvc_fetch_or_32(_AW_CAST_32(ptr), (long)(val), order),                                                                   \
        unsigned int: _om_msvc_fetch_or_32(_AW_CAST_32(ptr), (long)(val), order),                                                          \
        long: _om_msvc_fetch_or_32(_AW_CAST_32(ptr), (long)(val), order),                                                                  \
        unsigned long: _om_msvc_fetch_or_32(_AW_CAST_32(ptr), (long)(val), order),                                                         \
        long long: _om_msvc_fetch_or_64(_AW_CAST_64(ptr), (long long)(val), order),                                                        \
        unsigned long long: _om_msvc_fetch_or_64(_AW_CAST_64(ptr), (long long)(val), order))
#define om_fetch_xor(ptr, val, order)                                                                                                      \
    _Generic((0, *(ptr)),                                                                                                                  \
        char: _om_msvc_fetch_xor_8(_AW_CAST_8(ptr), (char)(val), order),                                                                   \
        signed char: _om_msvc_fetch_xor_8(_AW_CAST_8(ptr), (char)(val), order),                                                            \
        unsigned char: _om_msvc_fetch_xor_8(_AW_CAST_8(ptr), (char)(val), order),                                                          \
        short: _om_msvc_fetch_xor_16(_AW_CAST_16(ptr), (short)(val), order),                                                               \
        unsigned short: _om_msvc_fetch_xor_16(_AW_CAST_16(ptr), (short)(val), order),                                                      \
        int: _om_msvc_fetch_xor_32(_AW_CAST_32(ptr), (long)(val), order),                                                                  \
        unsigned int: _om_msvc_fetch_xor_32(_AW_CAST_32(ptr), (long)(val), order),                                                         \
        long: _om_msvc_fetch_xor_32(_AW_CAST_32(ptr), (long)(val), order),                                                                 \
        unsigned long: _om_msvc_fetch_xor_32(_AW_CAST_32(ptr), (long)(val), order),                                                        \
        long long: _om_msvc_fetch_xor_64(_AW_CAST_64(ptr), (long long)(val), order),                                                       \
        unsigned long long: _om_msvc_fetch_xor_64(_AW_CAST_64(ptr), (long long)(val), order))
#endif

// --- 7. Fences ---
#if defined(AW_COMPILER_GCC_LIKE) || defined(AW_COMPILER_AC5)
#define om_thread_fence(order) _om_impl_thread_fence(order)
#define om_signal_fence(order) _om_impl_signal_fence(order)
#elif defined(AW_COMPILER_MSVC)
#define om_thread_fence(order)                                                                                                             \
    do                                                                                                                                     \
    {                                                                                                                                      \
        if ((order) == AW_MO_SEQ_CST)                                                                                                      \
            MemoryBarrier();                                                                                                               \
        else if ((order) != AW_MO_RELAXED)                                                                                                 \
            _ReadWriteBarrier();                                                                                                           \
    } while (0)

#define om_signal_fence(order) _ReadWriteBarrier()
#endif

#endif // AW_USE_STDATOMIC

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // AW_ATOMICS_H

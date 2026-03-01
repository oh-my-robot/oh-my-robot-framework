#ifndef AW_BACKEND_GCC_H
#define AW_BACKEND_GCC_H

#include "atomic_base.h"

#ifdef AW_COMPILER_GCC_LIKE

// GCC 内置函数在类型方面具有通用性。
// 我们定义了一些宏来直接传递参数。

#define _om_impl_load(ptr, order) __atomic_load_n(ptr, order)

#define _om_impl_store(ptr, val, order) __atomic_store_n(ptr, val, order)

#define _om_impl_exchange(ptr, val, order) __atomic_exchange_n(ptr, val, order)

#define _om_impl_cas(ptr, exp, des, succ, fail) __atomic_compare_exchange_n(ptr, exp, des, 0, succ, fail)

#define _om_impl_fetch_add(ptr, val, order) __atomic_fetch_add(ptr, val, order)

#define _om_impl_fetch_sub(ptr, val, order) __atomic_fetch_sub(ptr, val, order)

#define _om_impl_fetch_and(ptr, val, order) __atomic_fetch_and(ptr, val, order)

#define _om_impl_fetch_or(ptr, val, order) __atomic_fetch_or(ptr, val, order)

#define _om_impl_fetch_xor(ptr, val, order) __atomic_fetch_xor(ptr, val, order)

#define _om_impl_thread_fence(order) __atomic_thread_fence(order)

#define _om_impl_signal_fence(order) __atomic_signal_fence(order)

#endif // AW_COMPILER_GCC_LIKE

#endif // AW_BACKEND_GCC_H
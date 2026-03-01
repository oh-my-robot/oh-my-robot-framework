#ifndef AW_BACKEND_AC5_H
#define AW_BACKEND_AC5_H

#include "atomic_base.h"

#ifdef AW_COMPILER_AC5

// AC5 doesn't support C11 _Generic, but its __sync_* intrinsics are overloaded.
// For Load/Store, we use volatile access + barriers.

// AC5 Barrier Helpers
AW_INLINE void _om_ac5_barrier(om_memory_order order)
{
    if (order != AW_MO_RELAXED)
    {
        __sync_synchronize(); // AC5 Full Barrier
    }
}

// 1. Load
// AC5 treats volatile access as atomic for aligned native types.
// We wrap it to ignore 'order' partially (always safe/barrier).
#define _om_impl_load(ptr, order)                                                                                                          \
    ({                                                                                                                                     \
        _om_ac5_barrier(order);                                                                                                            \
        __typeof__(*(ptr)) _v = *(volatile __typeof__(*(ptr))*)(ptr);                                                                      \
        _om_ac5_barrier(order);                                                                                                            \
        _v;                                                                                                                                \
    })

// 2. Store
#define _om_impl_store(ptr, val, order)                                                                                                    \
    do                                                                                                                                     \
    {                                                                                                                                      \
        _om_ac5_barrier(order);                                                                                                            \
        *(volatile __typeof__(*(ptr))*)(ptr) = (val);                                                                                      \
        if (order == AW_MO_SEQ_CST)                                                                                                        \
            __sync_synchronize();                                                                                                          \
    } while (0)

// 3. Exchange
// __sync_lock_test_and_set is actually an acquire barrier exchange usually,
// but __sync_val_compare_and_swap loop is safer for full exchange semantics in legacy mode.
#define _om_impl_exchange(ptr, val, order)                                                                                                 \
    ({                                                                                                                                     \
        __typeof__(*(ptr)) _old;                                                                                                           \
        do                                                                                                                                 \
        {                                                                                                                                  \
            _old = *(volatile __typeof__(*(ptr))*)(ptr);                                                                                   \
        } while (__sync_val_compare_and_swap(ptr, _old, val) != _old);                                                                     \
        _old;                                                                                                                              \
    })

// 4. CAS
// __sync_bool_compare_and_swap returns bool, matches our need.
// AC5 sync builtins are full barriers (SeqCst).
#define _om_impl_cas(ptr, exp, des, succ, fail)                                                                                            \
    ({                                                                                                                                     \
        bool _ret = false;                                                                                                                 \
        __typeof__(*(ptr)) _old_val = *(exp);                                                                                              \
        __typeof__(*(ptr)) _prev_val = __sync_val_compare_and_swap(ptr, _old_val, des);                                                    \
        if (_prev_val == _old_val)                                                                                                         \
        {                                                                                                                                  \
            _ret = true;                                                                                                                   \
        }                                                                                                                                  \
        else                                                                                                                               \
        {                                                                                                                                  \
            *(exp) = _prev_val;                                                                                                            \
            _ret = false;                                                                                                                  \
        }                                                                                                                                  \
        _ret;                                                                                                                              \
    })

// 5. Fetch Ops
// AC5 __sync intrinsics are overloaded for 1, 2, 4, 8 byte integers.
#define _om_impl_fetch_add(ptr, val, order) __sync_fetch_and_add(ptr, val)
#define _om_impl_fetch_sub(ptr, val, order) __sync_fetch_and_sub(ptr, val)
#define _om_impl_fetch_and(ptr, val, order) __sync_fetch_and_and(ptr, val)
#define _om_impl_fetch_or(ptr, val, order) __sync_fetch_and_or(ptr, val)
#define _om_impl_fetch_xor(ptr, val, order) __sync_fetch_and_xor(ptr, val)

// 6. Fences
#define _om_impl_thread_fence(order)                                                                                                       \
    if (order != AW_MO_RELAXED)                                                                                                            \
    __sync_synchronize()

#define _om_impl_signal_fence(order) __schedule_barrier()

#endif // AW_COMPILER_AC5

#endif // AW_BACKEND_AC5_H
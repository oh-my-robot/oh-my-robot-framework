#ifndef AW_AWTOMIC_MSVC_H
#define AW_AWTOMIC_MSVC_H

#include "atomic_base.h"

#ifdef AW_COMPILER_MSVC

// ----------------------------------------------------------------------------
// MSVC 显式屏障辅助
// ----------------------------------------------------------------------------
AW_INLINE void _om_msvc_barrier_pre(OmMemoryOrder order)
{
    if (order != AW_MO_RELAXED)
        _ReadWriteBarrier(); // Compiler barrier
    if (order == AW_MO_SEQ_CST)
        MemoryBarrier(); // Full hardware barrier
}
AW_INLINE void _om_msvc_barrier_post(OmMemoryOrder order)
{
    if (order != AW_MO_RELAXED)
        _ReadWriteBarrier();
    if (order == AW_MO_SEQ_CST)
        MemoryBarrier();
}

// ----------------------------------------------------------------------------
// 8-bit Implementation
// ----------------------------------------------------------------------------
AW_INLINE char _om_msvc_load_8(volatile char* ptr, OmMemoryOrder order)
{
    char v = *ptr;
    _om_msvc_barrier_post(order);
    return v;
}
AW_INLINE void _om_msvc_store_8(volatile char* ptr, char val, OmMemoryOrder order)
{
    _om_msvc_barrier_pre(order);
    *ptr = val;
    if (order == AW_MO_SEQ_CST)
        MemoryBarrier();
}
AW_INLINE char _om_msvc_exchange_8(volatile char* ptr, char val, OmMemoryOrder order)
{
    return _InterlockedExchange8(ptr, val);
}
AW_INLINE bool _om_msvc_cas_8(volatile char* ptr, char* exp, char des, OmMemoryOrder succ, OmMemoryOrder fail)
{
    char old = *exp;
    char prev = _InterlockedCompareExchange8(ptr, des, old);
    if (prev == old)
        return true;
    *exp = prev;
    return false;
}
AW_INLINE char _om_msvc_fetch_add_8(volatile char* ptr, char val, OmMemoryOrder order)
{
    return _InterlockedExchangeAdd8(ptr, val);
}
AW_INLINE char _om_msvc_fetch_or_8(volatile char* ptr, char val, OmMemoryOrder order)
{
    return _InterlockedOr8(ptr, val);
}
AW_INLINE char _om_msvc_fetch_and_8(volatile char* ptr, char val, OmMemoryOrder order)
{
    return _InterlockedAnd8(ptr, val);
}
AW_INLINE char _om_msvc_fetch_xor_8(volatile char* ptr, char val, OmMemoryOrder order)
{
    return _InterlockedXor8(ptr, val);
}

// ----------------------------------------------------------------------------
// 16-bit Implementation
// ----------------------------------------------------------------------------
AW_INLINE short _om_msvc_load_16(volatile short* ptr, OmMemoryOrder order)
{
    short v = *ptr;
    _om_msvc_barrier_post(order);
    return v;
}
AW_INLINE void _om_msvc_store_16(volatile short* ptr, short val, OmMemoryOrder order)
{
    _om_msvc_barrier_pre(order);
    *ptr = val;
    if (order == AW_MO_SEQ_CST)
        MemoryBarrier();
}
AW_INLINE short _om_msvc_exchange_16(volatile short* ptr, short val, OmMemoryOrder order)
{
    return _InterlockedExchange16(ptr, val);
}
AW_INLINE bool _om_msvc_cas_16(volatile short* ptr, short* exp, short des, OmMemoryOrder succ, OmMemoryOrder fail)
{
    short old = *exp;
    short prev = _InterlockedCompareExchange16(ptr, des, old);
    if (prev == old)
        return true;
    *exp = prev;
    return false;
}
AW_INLINE short _om_msvc_fetch_add_16(volatile short* ptr, short val, OmMemoryOrder order)
{
    return _InterlockedExchangeAdd16(ptr, val);
}
AW_INLINE short _om_msvc_fetch_or_16(volatile short* ptr, short val, OmMemoryOrder order)
{
    return _InterlockedOr16(ptr, val);
}
AW_INLINE short _om_msvc_fetch_and_16(volatile short* ptr, short val, OmMemoryOrder order)
{
    return _InterlockedAnd16(ptr, val);
}
AW_INLINE short _om_msvc_fetch_xor_16(volatile short* ptr, short val, OmMemoryOrder order)
{
    return _InterlockedXor16(ptr, val);
}

// ----------------------------------------------------------------------------
// 32-bit Implementation (long 在 MSVC 中是 32 位整数)
// ----------------------------------------------------------------------------
AW_INLINE long _om_msvc_load_32(volatile long* ptr, OmMemoryOrder order)
{
    long v = *ptr;
    _om_msvc_barrier_post(order);
    return v;
}
AW_INLINE void _om_msvc_store_32(volatile long* ptr, long val, OmMemoryOrder order)
{
    _om_msvc_barrier_pre(order);
    *ptr = val;
    if (order == AW_MO_SEQ_CST)
        MemoryBarrier();
}
AW_INLINE long _om_msvc_exchange_32(volatile long* ptr, long val, OmMemoryOrder order)
{
    return _InterlockedExchange(ptr, val);
}
AW_INLINE bool _om_msvc_cas_32(volatile long* ptr, long* exp, long des, OmMemoryOrder succ, OmMemoryOrder fail)
{
    long old = *exp;
    long prev = _InterlockedCompareExchange(ptr, des, old);
    if (prev == old)
        return true;
    *exp = prev;
    return false;
}
AW_INLINE long _om_msvc_fetch_add_32(volatile long* ptr, long val, OmMemoryOrder order)
{
    return _InterlockedExchangeAdd(ptr, val);
}
AW_INLINE long _om_msvc_fetch_or_32(volatile long* ptr, long val, OmMemoryOrder order)
{
    return _InterlockedOr(ptr, val);
}
AW_INLINE long _om_msvc_fetch_and_32(volatile long* ptr, long val, OmMemoryOrder order)
{
    return _InterlockedAnd(ptr, val);
}
AW_INLINE long _om_msvc_fetch_xor_32(volatile long* ptr, long val, OmMemoryOrder order)
{
    return _InterlockedXor(ptr, val);
}

// ----------------------------------------------------------------------------
// 64-bit Implementation
// ----------------------------------------------------------------------------
AW_INLINE long long _om_msvc_load_64(volatile long long* ptr, OmMemoryOrder order)
{
    long long v = *ptr;
    _om_msvc_barrier_post(order);
    return v;
}
AW_INLINE void _om_msvc_store_64(volatile long long* ptr, long long val, OmMemoryOrder order)
{
    _om_msvc_barrier_pre(order);
    *ptr = val;
    if (order == AW_MO_SEQ_CST)
        MemoryBarrier();
}
AW_INLINE long long _om_msvc_exchange_64(volatile long long* ptr, long long val, OmMemoryOrder order)
{
    return _InterlockedExchange64(ptr, val);
}
AW_INLINE bool _om_msvc_cas_64(volatile long long* ptr, long long* exp, long long des, OmMemoryOrder succ, OmMemoryOrder fail)
{
    long long old = *exp;
    long long prev = _InterlockedCompareExchange64(ptr, des, old);
    if (prev == old)
        return true;
    *exp = prev;
    return false;
}
AW_INLINE long long _om_msvc_fetch_add_64(volatile long long* ptr, long long val, OmMemoryOrder order)
{
    return _InterlockedExchangeAdd64(ptr, val);
}
AW_INLINE long long _om_msvc_fetch_or_64(volatile long long* ptr, long long val, OmMemoryOrder order)
{
    return _InterlockedOr64(ptr, val);
}
AW_INLINE long long _om_msvc_fetch_and_64(volatile long long* ptr, long long val, OmMemoryOrder order)
{
    return _InterlockedAnd64(ptr, val);
}
AW_INLINE long long _om_msvc_fetch_xor_64(volatile long long* ptr, long long val, OmMemoryOrder order)
{
    return _InterlockedXor64(ptr, val);
}

#endif // AW_COMPILER_MSVC

#endif // AW_AWTOMIC_MSVC_H
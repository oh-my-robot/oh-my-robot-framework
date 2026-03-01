#ifndef AW_ATOMIC_SIMPLE_H
#define AW_ATOMIC_SIMPLE_H

#include "atomic.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * ============================================================================
 * AW Atomic Simplified Library (Rel/Acq Model)
 * ============================================================================
 * * 命名约定:
 * - _rlx : Relaxed (松散模型，仅保证原子性，无同步语义)
 * - _acq : Acquire (获取语义，用于 Load 或 RMW)
 * - _rel : Release (释放语义，用于 Store 或 RMW)
 * - _ar  : AcqRel  (获取+释放，用于 RMW)
 * * 所有的指针参数 (ptr) 均通过底层 atomic.h 处理，
 * 因此本库不关心具体数据类型 (int, long, void* 等)。
 */

// ============================================================================
// 1. Load (读取)
// ============================================================================

// 最常用的同步读取，保证后续读写不越过此点
#define OM_LOAD_ACQ(ptr) om_load(ptr, AW_MO_ACQUIRE)

// 仅保证原子读取，无顺序保证 (常用于统计计数读取)
#define OM_LOAD_RLX(ptr) om_load(ptr, AW_MO_RELAXED)

// 消费语义 (在某些架构上比 Acquire 轻量，但通常等同于 Acquire)
#define OM_LOAD_CONSUME(ptr) om_load(ptr, AW_MO_CONSUME)

// ============================================================================
// 2. Store (写入)
// ============================================================================

// 最常用的同步写入，保证之前的读写已完成 (常用于发布数据)
#define OM_STORE_REL(ptr, val) om_store(ptr, val, AW_MO_RELEASE)

// 仅保证原子写入 (常用于重置计数器)
#define OM_STORE_RLX(ptr, val) om_store(ptr, val, AW_MO_RELAXED)

// ============================================================================
// 3. Exchange (交换 / Swap)
// ============================================================================

// 强同步交换 (常用于锁的获取与释放)
#define OM_SWAP_AR(ptr, val) om_exchange(ptr, val, AW_MO_ACQ_REL)

// 获取语义交换 (常用于获取锁)
#define OM_SWAP_ACQ(ptr, val) om_exchange(ptr, val, AW_MO_ACQUIRE)

// 释放语义交换 (常用于释放锁并写入新值)
#define OM_SWAP_REL(ptr, val) om_exchange(ptr, val, AW_MO_RELEASE)

// 松散交换
#define OM_SWAP_RLX(ptr, val) om_exchange(ptr, val, AW_MO_RELAXED)

// ============================================================================
// 4. CAS (Compare And Swap)
// ============================================================================
// 注意: om_cas 原型为 (ptr, expected_ptr, desired, succ_order, fail_order)
// 本库中 CAS 失败时的内存序默认设置为 Acquire (如果成功序包含Acquire) 或 Relaxed

// [最常用] 强 CAS，成功时全屏障，失败时获取最新值 (适用于大多数无锁结构)
#define OM_CAS_AR(ptr, exp_ptr, des) om_cas(ptr, exp_ptr, des, AW_MO_ACQ_REL, AW_MO_ACQUIRE)

// 获取型 CAS (成功和失败都具备 Acquire 语义)
#define OM_CAS_ACQ(ptr, exp_ptr, des) om_cas(ptr, exp_ptr, des, AW_MO_ACQUIRE, AW_MO_ACQUIRE)

// 释放型 CAS (成功时 Release，失败时 Relaxed)
#define OM_CAS_REL(ptr, exp_ptr, des) om_cas(ptr, exp_ptr, des, AW_MO_RELEASE, AW_MO_RELAXED)

// 松散 CAS (无同步语义)
#define OM_CAS_RLX(ptr, exp_ptr, des) om_cas(ptr, exp_ptr, des, AW_MO_RELAXED, AW_MO_RELAXED)

// ============================================================================
// 5. Arithmetic (Fetch Add / Sub)
// ============================================================================

// --- Fetch Add (返回旧值) ---
#define OM_FAA_RLX(ptr, val) om_fetch_add(ptr, val, AW_MO_RELAXED)
#define OM_FAA_ACQ(ptr, val) om_fetch_add(ptr, val, AW_MO_ACQUIRE)
#define OM_FAA_REL(ptr, val) om_fetch_add(ptr, val, AW_MO_RELEASE)
#define OM_FAA_AR(ptr, val) om_fetch_add(ptr, val, AW_MO_ACQ_REL)

// --- Fetch Sub (返回旧值) ---
#define OM_FAS_RLX(ptr, val) om_fetch_sub(ptr, val, AW_MO_RELAXED)
#define OM_FAS_ACQ(ptr, val) om_fetch_sub(ptr, val, AW_MO_ACQUIRE)
#define OM_FAS_REL(ptr, val) om_fetch_sub(ptr, val, AW_MO_RELEASE)
#define OM_FAS_AR(ptr, val) om_fetch_sub(ptr, val, AW_MO_ACQ_REL)

// --- Helper: Increment / Decrement (常用操作) ---
#define OM_INC_RLX(ptr) OM_FAA_RLX(ptr, 1)
#define OM_INC_AR(ptr) OM_FAA_AR(ptr, 1)
#define OM_DEC_RLX(ptr) OM_FAS_RLX(ptr, 1)
#define OM_DEC_AR(ptr) OM_FAS_AR(ptr, 1)

// ============================================================================
// 6. Bitwise Logic (Fetch And / Or / Xor)
// ============================================================================

// --- Fetch And (按位与) ---
#define OM_FAND_RLX(ptr, val) om_fetch_and(ptr, val, AW_MO_RELAXED)
#define OM_FAND_ACQ(ptr, val) om_fetch_and(ptr, val, AW_MO_ACQUIRE)
#define OM_FAND_REL(ptr, val) om_fetch_and(ptr, val, AW_MO_RELEASE)
#define OM_FAND_AR(ptr, val) om_fetch_and(ptr, val, AW_MO_ACQ_REL)

// --- Fetch Or (按位或 - 常用于设置标志位) ---
#define OM_FOR_RLX(ptr, val) om_fetch_or(ptr, val, AW_MO_RELAXED)
#define OM_FOR_ACQ(ptr, val) om_fetch_or(ptr, val, AW_MO_ACQUIRE)
#define OM_FOR_REL(ptr, val) om_fetch_or(ptr, val, AW_MO_RELEASE)
#define OM_FOR_AR(ptr, val) om_fetch_or(ptr, val, AW_MO_ACQ_REL)

// --- Fetch Xor (按位异或 - 常用于翻转位) ---
#define OM_FXOR_RLX(ptr, val) om_fetch_xor(ptr, val, AW_MO_RELAXED)
#define OM_FXOR_ACQ(ptr, val) om_fetch_xor(ptr, val, AW_MO_ACQUIRE)
#define OM_FXOR_REL(ptr, val) om_fetch_xor(ptr, val, AW_MO_RELEASE)
#define OM_FXOR_AR(ptr, val) om_fetch_xor(ptr, val, AW_MO_ACQ_REL)

// ============================================================================
// 7. Fences (内存屏障)
// ============================================================================

// 线程屏障 (全局可见性同步)
#define OM_FENCE_ACQ() om_thread_fence(AW_MO_ACQUIRE)
#define OM_FENCE_REL() om_thread_fence(AW_MO_RELEASE)
#define OM_FENCE_AR() om_thread_fence(AW_MO_ACQ_REL)
#define OM_FENCE_SEQ() om_thread_fence(AW_MO_SEQ_CST) // 最强屏障

// 编译器/信号屏障 (防止编译器重排，不生成 CPU 指令)
#define OM_COMPILER_BARRIER() om_signal_fence(AW_MO_ACQ_REL)

#ifdef __cplusplus
}
#endif

#endif // AW_ATOMIC_SIMPLE_H

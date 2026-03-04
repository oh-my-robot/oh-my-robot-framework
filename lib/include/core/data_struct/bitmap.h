#ifndef AW_BITMAP_H
#define AW_BITMAP_H

#include "atomic/atomic.h"
#include "core/om_def.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t bitCount;
    size_t wordCount;
} AwBitmapMeta;

typedef struct {
    AwBitmapMeta meta;
    unsigned long *words;
} AwBitmapRom;

typedef struct {
    AwBitmapMeta meta;
    OmAtomicUlong *words;
} AwBitmapAtomic;

/**
 * @brief 计算位图需要的机器字数量
 */
size_t om_bitmap_word_count(size_t bit_count);

/**
 * @brief 初始化 raw 位图（非线程安全）
 * @retval OM_OK 成功
 * @retval OM_ERROR_PARAM 参数非法（bitmap/buffer 为空或 bit_count 为 0）
 */
OmRet om_bitmap_rom_init(AwBitmapRom *bitmap, unsigned long *buffer, size_t bit_count);

/**
 * @brief 读取 raw 位
 */
bool om_bitmap_rom_test(const AwBitmapRom *bitmap, size_t bit_index);

/**
 * @brief 置位 raw 位
 */
void om_bitmap_rom_set(AwBitmapRom *bitmap, size_t bit_index);

/**
 * @brief 清位 raw 位
 */
void om_bitmap_rom_clear(AwBitmapRom *bitmap, size_t bit_index);

/**
 * @brief 初始化 atomic 位图（线程安全）
 * @retval OM_OK 成功
 * @retval OM_ERROR_PARAM 参数非法（bitmap/buffer 为空或 bit_count 为 0）
 */
OmRet om_bitmap_atomic_init(AwBitmapAtomic *bitmap, OmAtomicUlong *buffer, size_t bit_count);

/**
 * @brief 按 bit_count 计算并动态分配 raw 位图 buffer
 * @param bit_count 位数量
 * @param pmalloc 分配函数，可传 NULL（内部回退到 malloc）
 * @return 成功返回 buffer 指针，失败返回 NULL
 * @note 该接口仅负责 buffer 分配；需配合 om_bitmap_rom_init 使用。
 */
unsigned long *om_bitmap_rom_buffer_alloc(size_t bit_count, void *(*pmalloc)(size_t));

/**
 * @brief 按 bit_count 计算并动态分配 atomic 位图 buffer
 * @param bit_count 位数量
 * @param pmalloc 分配函数，可传 NULL（内部回退到 malloc）
 * @return 成功返回 buffer 指针，失败返回 NULL
 * @note 该接口仅负责 buffer 分配；需配合 om_bitmap_atomic_init 使用。
 */
OmAtomicUlong *om_bitmap_atomic_buffer_alloc(size_t bit_count, void *(*pmalloc)(size_t));

/**
 * @brief 释放由 om_bitmap_*_buffer_alloc 分配的 buffer
 * @param buffer buffer 指针
 * @param pfree 释放函数，可传 NULL（内部回退到 free）
 * @note 仅用于 alloc 路径分配的内存；外部静态/栈 buffer 禁止传入。
 */
void om_bitmap_buffer_free(void *buffer, void (*pfree)(void *));

/**
 * @brief 读取 atomic 位（线程安全）
 */
bool om_bitmap_atomic_test(const AwBitmapAtomic *bitmap, size_t bit_index);

/**
 * @brief 原子尝试置位
 * @retval true  从 0 -> 1 成功
 * @retval false 该位原本已为 1 或参数非法
 */
bool om_bitmap_atomic_try_set(AwBitmapAtomic *bitmap, size_t bit_index);

/**
 * @brief 原子清位
 */
void om_bitmap_atomic_clear(AwBitmapAtomic *bitmap, size_t bit_index);

/**
 * @brief 原子分配第一个空闲 bit（从 start_hint 开始循环扫描）
 * @retval OM_OK         分配成功
 * @retval OM_ERROR_BUSY 无可用 bit
 * @retval OM_ERROR_PARAM 参数非法
 */
OmRet om_bitmap_atomic_alloc_first_zero(AwBitmapAtomic *bitmap, size_t start_hint, size_t *bit_out);

#ifdef __cplusplus
}
#endif

#endif // AW_BITMAP_H

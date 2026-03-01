#include "core/data_struct/bitmap.h"
#include <stdlib.h>
#include <string.h>

#define AW_BITMAP_BITS_PER_WORD (sizeof(unsigned long) * 8U)

static inline size_t om_bitmap_word_index(size_t bit_index)
{
    return bit_index / AW_BITMAP_BITS_PER_WORD;
}

static inline unsigned long om_bitmap_bit_mask(size_t bit_index)
{
    return (unsigned long)1U << (bit_index % AW_BITMAP_BITS_PER_WORD);
}

size_t om_bitmap_word_count(size_t bit_count)
{
    return (bit_count + AW_BITMAP_BITS_PER_WORD - 1U) / AW_BITMAP_BITS_PER_WORD;
}

OmRet_e om_bitmap_rom_init(AwBitmapRom_s *bitmap, unsigned long *buffer, size_t bit_count)
{
    if (bitmap == NULL || buffer == NULL || bit_count == 0U)
        return OM_ERROR_PARAM;

    bitmap->meta.bitCount = bit_count;
    bitmap->meta.wordCount = om_bitmap_word_count(bit_count);
    bitmap->words = buffer;

    memset(bitmap->words, 0, bitmap->meta.wordCount * sizeof(unsigned long));
    return OM_OK;
}

bool om_bitmap_rom_test(const AwBitmapRom_s *bitmap, size_t bit_index)
{
    if (bitmap == NULL || bitmap->words == NULL || bit_index >= bitmap->meta.bitCount)
        return false;

    size_t word_index = om_bitmap_word_index(bit_index);
    unsigned long bit_mask = om_bitmap_bit_mask(bit_index);
    return (bitmap->words[word_index] & bit_mask) != 0U;
}

void om_bitmap_rom_set(AwBitmapRom_s *bitmap, size_t bit_index)
{
    if (bitmap == NULL || bitmap->words == NULL || bit_index >= bitmap->meta.bitCount)
        return;

    size_t word_index = om_bitmap_word_index(bit_index);
    unsigned long bit_mask = om_bitmap_bit_mask(bit_index);
    bitmap->words[word_index] |= bit_mask;
}

void om_bitmap_rom_clear(AwBitmapRom_s *bitmap, size_t bit_index)
{
    if (bitmap == NULL || bitmap->words == NULL || bit_index >= bitmap->meta.bitCount)
        return;

    size_t word_index = om_bitmap_word_index(bit_index);
    unsigned long bit_mask = om_bitmap_bit_mask(bit_index);
    bitmap->words[word_index] &= ~bit_mask;
}

OmRet_e om_bitmap_atomic_init(AwBitmapAtomic_s *bitmap, om_atomic_ulong_t *buffer, size_t bit_count)
{
    if (bitmap == NULL || buffer == NULL || bit_count == 0U)
        return OM_ERROR_PARAM;

    bitmap->meta.bitCount = bit_count;
    bitmap->meta.wordCount = om_bitmap_word_count(bit_count);
    bitmap->words = buffer;

    for (size_t index = 0; index < bitmap->meta.wordCount; index++)
        OM_STORE(&bitmap->words[index], 0UL, AW_MO_RELAXED);
    return OM_OK;
}

unsigned long *om_bitmap_rom_buffer_alloc(size_t bit_count, void *(*pmalloc)(size_t))
{
    if (bit_count == 0U)
        return NULL;

    size_t word_count = om_bitmap_word_count(bit_count);
    void *(*alloc_func)(size_t) = (pmalloc != NULL) ? pmalloc : malloc;
    unsigned long *buffer = (unsigned long *)alloc_func(word_count * sizeof(unsigned long));
    if (buffer != NULL)
        memset(buffer, 0, word_count * sizeof(unsigned long));
    return buffer;
}

om_atomic_ulong_t *om_bitmap_atomic_buffer_alloc(size_t bit_count, void *(*pmalloc)(size_t))
{
    if (bit_count == 0U)
        return NULL;

    size_t word_count = om_bitmap_word_count(bit_count);
    void *(*alloc_func)(size_t) = (pmalloc != NULL) ? pmalloc : malloc;
    om_atomic_ulong_t *buffer = (om_atomic_ulong_t *)alloc_func(word_count * sizeof(om_atomic_ulong_t));
    if (buffer != NULL)
        memset(buffer, 0, word_count * sizeof(om_atomic_ulong_t));
    return buffer;
}

void om_bitmap_buffer_free(void *buffer, void (*pfree)(void *))
{
    if (buffer != NULL)
    {
        void (*free_func)(void *) = (pfree != NULL) ? pfree : free;
        free_func(buffer);
    }
}

bool om_bitmap_atomic_test(const AwBitmapAtomic_s *bitmap, size_t bit_index)
{
    if (bitmap == NULL || bitmap->words == NULL || bit_index >= bitmap->meta.bitCount)
        return false;

    size_t word_index = om_bitmap_word_index(bit_index);
    unsigned long bit_mask = om_bitmap_bit_mask(bit_index);
    unsigned long word_value = OM_LOAD(&bitmap->words[word_index], AW_MO_RELAXED);
    return (word_value & bit_mask) != 0U;
}

bool om_bitmap_atomic_try_set(AwBitmapAtomic_s *bitmap, size_t bit_index)
{
    if (bitmap == NULL || bitmap->words == NULL || bit_index >= bitmap->meta.bitCount)
        return false;

    size_t word_index = om_bitmap_word_index(bit_index);
    unsigned long bit_mask = om_bitmap_bit_mask(bit_index);
    unsigned long old_value = OM_FETCH_OR(&bitmap->words[word_index], bit_mask, AW_MO_ACQ_REL);
    return (old_value & bit_mask) == 0U;
}

void om_bitmap_atomic_clear(AwBitmapAtomic_s *bitmap, size_t bit_index)
{
    if (bitmap == NULL || bitmap->words == NULL || bit_index >= bitmap->meta.bitCount)
        return;

    size_t word_index = om_bitmap_word_index(bit_index);
    unsigned long bit_mask = om_bitmap_bit_mask(bit_index);
    OM_FETCH_AND(&bitmap->words[word_index], ~bit_mask, AW_MO_ACQ_REL);
}

OmRet_e om_bitmap_atomic_alloc_first_zero(AwBitmapAtomic_s *bitmap, size_t start_hint, size_t *bit_out)
{
    if (bitmap == NULL || bitmap->words == NULL || bit_out == NULL || bitmap->meta.bitCount == 0U)
        return OM_ERROR_PARAM;

    size_t bit_count = bitmap->meta.bitCount;
    size_t start_index = start_hint % bit_count;

    for (size_t offset = 0; offset < bit_count; offset++)
    {
        size_t bit_index = (start_index + offset) % bit_count;
        if (om_bitmap_atomic_try_set(bitmap, bit_index))
        {
            *bit_out = bit_index;
            return OM_OK;
        }
    }
    return OM_ERROR_BUSY;
}

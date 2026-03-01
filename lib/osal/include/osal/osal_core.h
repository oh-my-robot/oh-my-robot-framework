#ifndef OM_OSAL_CORE_H
#define OM_OSAL_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "osal_config.h"

/* OSAL 缁熶竴鐘舵€佺爜锛圥hase 1 鍚堝悓锟?*/
typedef int32_t OsalStatus_t;

#define OSAL_OK (0)               /* 鎴愬姛 */
#define OSAL_TIMEOUT (-1)         /* 瓒呮椂锛坱imeout > 0 锟?WAIT_FOREVER 璇箟瓒呮椂锟?*/
#define OSAL_WOULD_BLOCK (-2)     /* 闈為樆濉炶皟鐢紙timeout=0锛夋潯浠朵笉婊¤冻 */
#define OSAL_NO_RESOURCE (-3)     /* 璧勬簮涓嶈冻锛堝唴锟?鍙ユ焺/闃熷垪妲戒綅锟?*/
#define OSAL_INVALID (-4)         /* 鍙傛暟/鍙ユ焺/涓婁笅鏂囬潪锟?*/
#define OSAL_NOT_SUPPORTED (-5)   /* 绔彛涓嶆敮锟?*/
#define OSAL_INTERNAL (-6)        /* 鍐呴儴閿欒 */

/**
 * @brief 鍒ゆ柇褰撳墠鏄惁澶勪簬涓柇涓婁笅锟?
 * @return 1 琛ㄧず鍦ㄤ腑鏂腑锟? 琛ㄧず鍦ㄧ嚎绋嬩笂涓嬫枃
 */
int osal_is_in_isr(void);

/**
 * @brief ISR 涓寸晫鍖虹姸鎬佺被锟?
 * @note 鐢辩鍙ｅ疄鐜板畾涔夊叾鍏蜂綋鍚箟锛涘綋锟?FreeRTOS 绔槧灏勪负涓柇灞忚斀鏃у€硷拷?
 */
typedef uint32_t OsalIrqIsrState_t;

/**
 * @brief 鍦ㄧ嚎绋嬩笂涓嬫枃杩涘叆涓寸晫锟?
 * @note 绂佹锟?ISR 涓皟鐢拷?
 */
void osal_irq_lock_task(void);

/**
 * @brief 鍦ㄧ嚎绋嬩笂涓嬫枃閫€鍑轰复鐣屽尯
 * @note 绂佹锟?ISR 涓皟鐢拷?
 */
void osal_irq_unlock_task(void);

/**
 * @brief 锟?ISR 涓婁笅鏂囪繘鍏ヤ复鐣屽尯骞朵繚瀛樹腑鏂姸锟?
 * @return ISR 涓寸晫鍖烘仮澶嶇姸锟?
 * @note 绂佹鍦ㄧ嚎绋嬩笂涓嬫枃璋冪敤锟?
 */
OsalIrqIsrState_t osal_irq_lock_from_isr(void);

/**
 * @brief 锟?ISR 涓婁笅鏂囨仮澶嶄腑鏂姸锟?
 * @param state `osal_irq_lock_from_isr` 杩斿洖鐨勭姸锟?
 * @note 绂佹鍦ㄧ嚎绋嬩笂涓嬫枃璋冪敤锟?
 */
void osal_irq_unlock_from_isr(OsalIrqIsrState_t state);

/**
 * @brief OSAL鍐呭瓨鐢宠
 * @param size 鐢宠瀛楄妭锟?
 * @return 鎴愬姛杩斿洖鎸囬拡锛屽け璐ヨ繑鍥濶ULL
 * @note 涓ユ牸妯″紡涓嬬姝㈠湪 ISR 涓皟鐢紱鑻ヨ鐢ㄥ垯瑙﹀彂鏂█锛岃繍琛屾椂杩斿洖 NULL锟?
 */
void* osal_malloc(size_t size);

/**
 * @brief OSAL鍐呭瓨閲婃斁
 * @param ptr 鎸囬拡
 * @note 涓ユ牸妯″紡涓嬬姝㈠湪 ISR 涓皟鐢紱鑻ヨ鐢ㄥ垯瑙﹀彂鏂█锛岃繍琛屾椂鐩存帴杩斿洖锟?
 */
void osal_free(void* ptr);

#ifdef __OM_USE_ASSERT
/* 鏂█澶辫触鍚庤繘鍏ユ寰幆锛屼究浜庤皟璇曞畾锟?*/
#define OSAL_ASSERT(expr)                                                                                                                  \
    do                                                                                                                                     \
    {                                                                                                                                      \
        if (!(expr))                                                                                                                       \
        {                                                                                                                                  \
            for (;;)                                                                                                                       \
            {                                                                                                                              \
            }                                                                                                                              \
        }                                                                                                                                  \
    } while (0)
#else
#define OSAL_ASSERT(expr) ((void)0)
#endif

/* 鏂█褰撳墠澶勪簬绾跨▼涓婁笅锟?*/
#define OSAL_ASSERT_IN_TASK() OSAL_ASSERT(!osal_is_in_isr())

#endif


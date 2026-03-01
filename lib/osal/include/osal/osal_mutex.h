#ifndef OM_OSAL_MUTEX_H
#define OM_OSAL_MUTEX_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalMutexHandle_s* OsalMutex_t;

/**
 * @brief 鍒涘缓浜掓枼閿侊紙绾跨▼涓婁笅鏂囷級
 * @param mutex 杈撳嚭浜掓枼閿佸彞鏌?
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
OsalStatus_t osal_mutex_create(OsalMutex_t* mutex);

/**
 * @brief 鍒犻櫎浜掓枼閿侊紙绾跨▼涓婁笅鏂囷級
 * @param mutex 浜掓枼閿佸彞鏌?
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 * @note 涓ユ牸鍓嶇疆鏉′欢锛氳皟鐢ㄦ柟闇€纭繚鏃犲苟鍙戣闂拰鏃犵瓑寰呰€呫€?
 */
OsalStatus_t osal_mutex_delete(OsalMutex_t mutex);

/**
 * @brief 鍔犻攣锛堢嚎绋嬩笂涓嬫枃锛岄潪閫掑綊璇箟锛?
 * @param mutex 浜掓枼閿佸彞鏌?
 * @param timeout_ms 瓒呮椂鏃堕棿锛坢s锛夛紝鍙敤 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID/OSAL_INTERNAL`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 * @note v1.0 涓嶆敮鎸侀€掑綊 mutex锛涘悓绾跨▼閲嶅鍔犻攣鎸夎秴鏃惰鍒欒繑鍥?`OSAL_WOULD_BLOCK/OSAL_TIMEOUT`锛堟垨鏃犻檺绛夊緟锛夈€?
 */
OsalStatus_t osal_mutex_lock(OsalMutex_t mutex, uint32_t timeout_ms);

/**
 * @brief 瑙ｉ攣锛堢嚎绋嬩笂涓嬫枃锛?
 * @param mutex 浜掓枼閿佸彞鏌?
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 * @note 闈?owner 瑙ｉ攣杩斿洖 `OSAL_INVALID`銆?
 * @note 浜掓枼閿佸簲閬垮厤璺ㄧ嚎绋嬮噴鏀撅紱浠呮寔鏈夐攣鐨勭嚎绋嬪彲鎵ц瑙ｉ攣銆?
 */
OsalStatus_t osal_mutex_unlock(OsalMutex_t mutex);

#endif


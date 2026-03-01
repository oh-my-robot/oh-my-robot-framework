#ifndef OM_OSAL_SEM_H
#define OM_OSAL_SEM_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalSemHandle_s* OsalSem_t;

/**
 * @brief 鍒涘缓淇″彿閲忥紙绾跨▼涓婁笅鏂囷級
 * @param sem 杈撳嚭淇″彿閲忓彞鏌?
 * @param max_count 鏈€澶ц鏁?
 * @param init_count 鍒濆璁℃暟
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
OsalStatus_t osal_sem_create(OsalSem_t* sem, uint32_t max_count, uint32_t init_count);

/**
 * @brief 鍒犻櫎淇″彿閲忥紙绾跨▼涓婁笅鏂囷級
 * @param sem 淇″彿閲忓彞鏌?
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 * @note 涓ユ牸鍓嶇疆鏉′欢锛氳皟鐢ㄦ柟闇€纭繚鏃犲苟鍙戣闂拰鏃犵瓑寰呰€呫€?
 */
OsalStatus_t osal_sem_delete(OsalSem_t sem);

/**
 * @brief 绛夊緟淇″彿閲忥紙绾跨▼涓婁笅鏂囷級
 * @param sem 淇″彿閲忓彞鏌?
 * @param timeout_ms 瓒呮椂鏃堕棿锛坢s锛夛紝鍙敤 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID/OSAL_INTERNAL`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
OsalStatus_t osal_sem_wait(OsalSem_t sem, uint32_t timeout_ms);

/**
 * @brief 閲婃斁淇″彿閲忥紙绾跨▼涓婁笅鏂囷級
 * @param sem 淇″彿閲忓彞鏌?
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 * @note 褰撹鏁板凡婊℃椂杩斿洖 `OSAL_NO_RESOURCE`锛堥潪闃诲澶辫触锛夈€?
 */
OsalStatus_t osal_sem_post(OsalSem_t sem);

/**
 * @brief 閲婃斁淇″彿閲忥紙ISR 涓婁笅鏂囷級
 * @param sem 淇″彿閲忓彞鏌?
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 绂佹鍦ㄧ嚎绋嬩笂涓嬫枃璋冪敤銆?
 * @note 褰撹鏁板凡婊℃椂杩斿洖 `OSAL_NO_RESOURCE`锛堥潪闃诲澶辫触锛夈€?
 */
OsalStatus_t osal_sem_post_from_isr(OsalSem_t sem);

/**
 * @brief 鑾峰彇淇″彿閲忓綋鍓嶈鏁帮紙绾跨▼涓婁笅鏂囷級
 * @param sem 淇″彿閲忓彞鏌?
 * @param out_count 杈撳嚭璁℃暟
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 * @note 璇ユ帴鍙ｄ粎鐢ㄤ簬瑙傛祴锛屼笉浣滀负鍒犻櫎瀹夊叏鎬х殑鍏呭垎鏉′欢銆?
 */
OsalStatus_t osal_sem_get_count(OsalSem_t sem, uint32_t* out_count);

/**
 * @brief 鑾峰彇淇″彿閲忓綋鍓嶈鏁帮紙ISR 涓婁笅鏂囷級
 * @param sem 淇″彿閲忓彞鏌?
 * @param out_count 杈撳嚭璁℃暟
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID`
 * @note 绂佹鍦ㄧ嚎绋嬩笂涓嬫枃璋冪敤銆?
 * @note 璇ユ帴鍙ｄ粎鐢ㄤ簬瑙傛祴锛屼笉浣滀负鍒犻櫎瀹夊叏鎬х殑鍏呭垎鏉′欢銆?
 */
OsalStatus_t osal_sem_get_count_from_isr(OsalSem_t sem, uint32_t* out_count);

#endif


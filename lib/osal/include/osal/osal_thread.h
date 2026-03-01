#ifndef OM_OSAL_THREAD_H
#define OM_OSAL_THREAD_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalThreadHandle_s* OsalThread_t;
typedef void (*OsalThreadEntry_t)(void* arg);

typedef struct
{
    const char* name;    /* 绾跨▼鍚嶇О锛岀敤浜庤皟璇?*/
    uint32_t stackSize; /* 鏍堝ぇ灏忥紙瀛楄妭锛?*/
    uint32_t priority;   /* 绾跨▼浼樺厛绾?*/
} OsalThreadAttr_s;

/**
 * @brief 鍒涘缓绾跨▼锛堢嚎绋嬩笂涓嬫枃锛?
 * @param thread 杈撳嚭绾跨▼鍙ユ焺锛堝繀椤婚潪 NULL锛?
 * @param attr 绾跨▼灞炴€э紱鍙负 NULL銆俙attr->stackSize==0` 鏃朵娇鐢ㄧ鍙ｉ粯璁ゆ爤澶у皬锛堝瓧鑺傦級
 * @param entry 绾跨▼鍏ュ彛鍑芥暟
 * @param arg 鍏ュ彛鍙傛暟
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID/OSAL_NO_RESOURCE/OSAL_INTERNAL`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
OsalStatus_t osal_thread_create(OsalThread_t* thread, const OsalThreadAttr_s* attr, OsalThreadEntry_t entry,
                                 void* arg);

/**
 * @brief 鑾峰彇褰撳墠绾跨▼鍙ユ焺锛堢嚎绋嬩笂涓嬫枃锛?
 * @return 褰撳墠绾跨▼鍙ユ焺锛涘湪 ISR 涓鐢ㄦ椂杩斿洖 NULL
 */
OsalThread_t osal_thread_self(void);

/**
 * @brief 绛夊緟绾跨▼缁撴潫锛堝彲閫夎兘鍔涳紝绾跨▼涓婁笅鏂囷級
 * @param thread 鐩爣绾跨▼
 * @param timeout_ms 瓒呮椂锛堟敮鎸?`OSAL_WAIT_FOREVER`锛?
 * @return `OSAL_OK` 鎴愬姛锛沗OSAL_NOT_SUPPORTED` 绔彛涓嶆敮鎸侊紱鍏朵粬涓洪敊璇爜
 * @note 褰撳墠 FreeRTOS 绔彛淇濇寔 `OSAL_NOT_SUPPORTED`銆?
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
OsalStatus_t osal_thread_join(OsalThread_t thread, uint32_t timeout_ms);

/**
 * @brief 涓诲姩璁╁嚭 CPU锛堢嚎绋嬩笂涓嬫枃锛?
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
void osal_thread_yield(void);

/**
 * @brief 閫€鍑哄綋鍓嶇嚎绋嬶紙绾跨▼涓婁笅鏂囷級
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
void osal_thread_exit(void);

/**
 * @brief 缁堟鎸囧畾绾跨▼锛堢嚎绋嬩笂涓嬫枃锛?
 * @param thread 绾跨▼鍙ユ焺
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID/OSAL_NOT_SUPPORTED/OSAL_INTERNAL`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 * @warning 鍗遍櫓璇箟锛氬厑璁歌法绾跨▼缁堟锛屼絾涓嶆壙璇鸿嚜鍔ㄦ竻鐞嗙洰鏍囩嚎绋嬫寔鏈夌殑涓氬姟璧勬簮锛堥攣/澶栬/鍐呭瓨绛夛級銆?
 * @warning `terminate(self)` 瑙嗕负闈炴硶锛屽繀椤讳娇鐢?`osal_thread_exit` 瀹屾垚鑷€€鍑恒€?
 * @note 鎺ㄨ崘浼樺厛浣跨敤鈥滃崗浣滈€€鍑衡€濇ā鍨嬶紱璺ㄧ嚎绋嬬粓姝粎鐢ㄤ簬鍙楁帶鍦烘櫙銆?
 */
OsalStatus_t osal_thread_terminate(OsalThread_t thread);

/**
 * @brief 鍚姩璋冨害鍣紙RTOS 鍦烘櫙锛岀嚎绋嬩笂涓嬫枃锛?
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID/OSAL_INTERNAL`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
OsalStatus_t osal_kernel_start(void);

#endif


#ifndef OM_OSAL_QUEUE_H
#define OM_OSAL_QUEUE_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalQueueHandle_s* OsalQueue_t;

/**
 * @brief 鍒涘缓娑堟伅闃熷垪锛堢嚎绋嬩笂涓嬫枃锛?
 * @param queue 杈撳嚭闃熷垪鍙ユ焺
 * @param length 闃熷垪闀垮害锛堝厓绱犱釜鏁帮級
 * @param item_size 鍗曚釜鍏冪礌澶у皬锛堝瓧鑺傦級
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
OsalStatus_t osal_queue_create(OsalQueue_t* queue, uint32_t length, uint32_t item_size);

/**
 * @brief 鍒犻櫎娑堟伅闃熷垪锛堢嚎绋嬩笂涓嬫枃锛?
 * @param queue 闃熷垪鍙ユ焺
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 * @note 涓ユ牸鍓嶇疆鏉′欢锛氳皟鐢ㄦ柟闇€纭繚鏃犲苟鍙戣闂€佹棤绛夊緟鑰呫€佹棤骞跺彂鐢熶骇娑堣垂銆?
 */
OsalStatus_t osal_queue_delete(OsalQueue_t queue);

/**
 * @brief 鍙戦€佹秷鎭紙绾跨▼涓婁笅鏂囷級
 * @param queue 闃熷垪鍙ユ焺
 * @param item 鏁版嵁鎸囬拡
 * @param timeout_ms 瓒呮椂鏃堕棿锛坢s锛夛紝鍙敤 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID/OSAL_INTERNAL`
 */
OsalStatus_t osal_queue_send(OsalQueue_t queue, const void* item, uint32_t timeout_ms);

/**
 * @brief 鍙戦€佹秷鎭紙涓柇涓婁笅鏂囷級
 * @param queue 闃熷垪鍙ユ焺
 * @param item 鏁版嵁鎸囬拡
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_WOULD_BLOCK/OSAL_INVALID/OSAL_INTERNAL`
 * @note 浠呭厑璁稿湪 ISR 涓皟鐢紱鍦ㄧ嚎绋嬩笂涓嬫枃璋冪敤杩斿洖 `OSAL_INVALID`銆?
 */
OsalStatus_t osal_queue_send_from_isr(OsalQueue_t queue, const void* item);

/**
 * @brief 鎺ユ敹娑堟伅锛堢嚎绋嬩笂涓嬫枃锛?
 * @param queue 闃熷垪鍙ユ焺
 * @param item 杈撳嚭鏁版嵁鎸囬拡
 * @param timeout_ms 瓒呮椂鏃堕棿锛坢s锛夛紝鍙敤 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID/OSAL_INTERNAL`
 */
OsalStatus_t osal_queue_recv(OsalQueue_t queue, void* item, uint32_t timeout_ms);

/**
 * @brief 鎺ユ敹娑堟伅锛堜腑鏂笂涓嬫枃锛?
 * @param queue 闃熷垪鍙ユ焺
 * @param item 杈撳嚭鏁版嵁鎸囬拡
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_WOULD_BLOCK/OSAL_INVALID/OSAL_INTERNAL`
 * @note 浠呭厑璁稿湪 ISR 涓皟鐢紱鍦ㄧ嚎绋嬩笂涓嬫枃璋冪敤杩斿洖 `OSAL_INVALID`銆?
 */
OsalStatus_t osal_queue_recv_from_isr(OsalQueue_t queue, void* item);

/**
 * @brief 鏌ョ湅娑堟伅锛堢嚎绋嬩笂涓嬫枃锛屼笉鍑洪槦锛?
 * @param queue 闃熷垪鍙ユ焺
 * @param item 杈撳嚭鏁版嵁鎸囬拡
 * @param timeout_ms 瓒呮椂鏃堕棿锛坢s锛夛紝鍙敤 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID/OSAL_INTERNAL`
 */
OsalStatus_t osal_queue_peek(OsalQueue_t queue, void* item, uint32_t timeout_ms);

/**
 * @brief 鏌ョ湅娑堟伅锛堜腑鏂笂涓嬫枃锛屼笉鍑洪槦锛?
 * @param queue 闃熷垪鍙ユ焺
 * @param item 杈撳嚭鏁版嵁鎸囬拡
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_WOULD_BLOCK/OSAL_INVALID/OSAL_INTERNAL`
 * @note 浠呭厑璁稿湪 ISR 涓皟鐢紱鍦ㄧ嚎绋嬩笂涓嬫枃璋冪敤杩斿洖 `OSAL_INVALID`銆?
 */
OsalStatus_t osal_queue_peek_from_isr(OsalQueue_t queue, void* item);

/**
 * @brief 澶嶄綅闃熷垪锛堟竻绌猴級
 * @param queue 闃熷垪鍙ユ焺
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID/OSAL_INTERNAL`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
OsalStatus_t osal_queue_reset(OsalQueue_t queue);

/**
 * @brief 鑾峰彇闃熷垪涓秷鎭暟閲?
 * @param queue 闃熷垪鍙ユ焺
 * @param out_count 杈撳嚭褰撳墠娑堟伅鏁伴噺
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID/OSAL_INTERNAL`
 * @note 绾跨▼涓?ISR 涓婁笅鏂囧潎鍙皟鐢ㄣ€?
 */
OsalStatus_t osal_queue_messages_waiting(OsalQueue_t queue, uint32_t* out_count);

/**
 * @brief 鑾峰彇闃熷垪鍓╀綑绌洪棿鏁伴噺
 * @param queue 闃熷垪鍙ユ焺
 * @param out_count 杈撳嚭鍓╀綑鍙敤妲戒綅鏁?
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID/OSAL_INTERNAL`
 * @note 绾跨▼涓?ISR 涓婁笅鏂囧潎鍙皟鐢ㄣ€?
 */
OsalStatus_t osal_queue_spaces_available(OsalQueue_t queue, uint32_t* out_count);

#endif


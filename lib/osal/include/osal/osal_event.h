#ifndef OM_OSAL_EVENT_H
#define OM_OSAL_EVENT_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalEventFlagsHandle_s* OsalEventFlags_t;

/*
 * event_flags 鍙敤涓氬姟浣嶆帺鐮侊紙绋冲畾鍚堝悓锛夛細
 * - 瀵瑰鎺ュ彛缁熶竴浣跨敤 uint32_t锟?
 * - 鐢辩鍙ｅ眰閫氳繃鏋勫缓绯荤粺娉ㄥ叆 OM_OSAL_EVENT_FLAGS_USABLE_MASK锟?
 * - OSAL 鍏叡澶翠笉鍋氱鍙ｅ垎鏀笌榛樿鍏滃簳锛岄伩鍏嶅钩鍙拌涔夋硠婕忥拷?
 */
#ifndef OM_OSAL_EVENT_FLAGS_USABLE_MASK
#error "OM_OSAL_EVENT_FLAGS_USABLE_MASK is not defined. It must be injected by the active OSAL port."
#endif

#define OSAL_EVENT_FLAGS_USABLE_MASK OM_OSAL_EVENT_FLAGS_USABLE_MASK

/* osal_event_flags_wait options */
#define OSAL_EVENT_FLAGS_WAIT_ALL (1u << 0) /* wait_all锛屽惁锟?wait_any */
#define OSAL_EVENT_FLAGS_NO_CLEAR (1u << 1) /* 涓嶆竻闄や綅锛屽惁锟?wait 鎴愬姛鍚庢竻锟?wait_mask */

/**
 * @brief 鍒涘缓浜嬩欢鏍囧織缁勶紙绾跨▼涓婁笅鏂囷級
 * @param event_flags 杈撳嚭浜嬩欢鏍囧織缁勫彞锟?
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑锟?`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 绂佹锟?ISR 涓皟鐢拷?
 */
OsalStatus_t osal_event_flags_create(OsalEventFlags_t* event_flags);

/**
 * @brief 鍒犻櫎浜嬩欢鏍囧織缁勶紙绾跨▼涓婁笅鏂囷級
 * @param event_flags 浜嬩欢鏍囧織缁勫彞锟?
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑锟?`OSAL_INVALID`
 * @note 绂佹锟?ISR 涓皟鐢拷?
 * @note 涓ユ牸鍓嶇疆鏉′欢锛氳皟鐢ㄦ柟闇€纭繚鏃犲苟鍙戣闂€佹棤绛夊緟鑰咃拷?
 */
OsalStatus_t osal_event_flags_delete(OsalEventFlags_t event_flags);

/**
 * @brief 璁剧疆浜嬩欢浣嶏紙绾跨▼涓婁笅鏂囷級
 * @param event_flags 浜嬩欢鏍囧織缁勫彞锟?
 * @param flags 寰呰缃簨浠朵綅鎺╃爜锛屽繀椤绘弧锟?`(flags & ~OSAL_EVENT_FLAGS_USABLE_MASK) == 0`
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑锟?`OSAL_INVALID`
 * @note 绂佹锟?ISR 涓皟鐢拷?
 */
OsalStatus_t osal_event_flags_set(OsalEventFlags_t event_flags, uint32_t flags);

/**
 * @brief 璁剧疆浜嬩欢浣嶏紙涓柇涓婁笅鏂囷級
 * @param event_flags 浜嬩欢鏍囧織缁勫彞锟?
 * @param flags 寰呰缃簨浠朵綅鎺╃爜锛屽繀椤绘弧锟?`(flags & ~OSAL_EVENT_FLAGS_USABLE_MASK) == 0`
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑锟?`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 浠呭厑璁稿湪 ISR 涓皟鐢紱鍦ㄧ嚎绋嬩笂涓嬫枃璋冪敤杩斿洖 `OSAL_INVALID`锟?
 */
OsalStatus_t osal_event_flags_set_from_isr(OsalEventFlags_t event_flags, uint32_t flags);

/**
 * @brief 娓呴櫎浜嬩欢浣嶏紙绾跨▼涓婁笅鏂囷級
 * @param event_flags 浜嬩欢鏍囧織缁勫彞锟?
 * @param flags 寰呮竻闄や簨浠朵綅鎺╃爜锛屽繀椤绘弧锟?`(flags & ~OSAL_EVENT_FLAGS_USABLE_MASK) == 0`
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑锟?`OSAL_INVALID`
 * @note 绂佹锟?ISR 涓皟鐢拷?
 */
OsalStatus_t osal_event_flags_clear(OsalEventFlags_t event_flags, uint32_t flags);

/**
 * @brief 绛夊緟浜嬩欢鏍囧織锛堢嚎绋嬩笂涓嬫枃锟?
 * @param event_flags 浜嬩欢鏍囧織鍙ユ焺
 * @param wait_mask 绛夊緟鐨勪綅鎺╃爜锛屽繀椤婚潪 0 涓旀弧锟?`(wait_mask & ~OSAL_EVENT_FLAGS_USABLE_MASK) == 0`
 * @param out_value 杈撳嚭鍊硷紙鍙负 NULL锛夛紱鑻ラ潪 NULL锛屼粎杩斿洖 `OSAL_EVENT_FLAGS_USABLE_MASK` 鑼冨洿鍐呯殑锟?
 * @param timeout_ms 瓒呮椂鏃堕棿锛坢s锛夛紝鍙敤 `OSAL_WAIT_FOREVER`
 * @param options 绛夊緟閫夐」锛坄OSAL_EVENT_FLAGS_*`锟?
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑锟?`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID`
 * @note 绂佹锟?ISR 涓皟鐢拷?
 */
OsalStatus_t osal_event_flags_wait(OsalEventFlags_t event_flags, uint32_t wait_mask, uint32_t* out_value,
                                    uint32_t timeout_ms, uint32_t options);

#endif


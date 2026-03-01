#ifndef OM_OSAL_TIMER_H
#define OM_OSAL_TIMER_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalTimerHandle_s* OsalTimer_t;
typedef void (*OsalTimerCallback_t)(OsalTimer_t timer);
typedef enum
{
    OSAL_TIMER_ONE_SHOT = 0u, /* 鍗曟瀹氭椂鍣?*/
    OSAL_TIMER_PERIODIC = 1u, /* 鍛ㄦ湡瀹氭椂鍣?*/
} OsalTimerMode_e;

/**
 * @brief 鍒涘缓杞欢瀹氭椂鍣紙绾跨▼涓婁笅鏂囷級
 * @param out_timer 杈撳嚭瀹氭椂鍣ㄥ彞鏌勶紙蹇呴』闈?NULL锛?
 * @param name 瀹氭椂鍣ㄥ悕绉帮紝鍙负 NULL锛堢敱绔彛鍐冲畾榛樿鍛藉悕锛?
 * @param period_ms 鍛ㄦ湡锛坢s锛?
 * @param mode 瀹氭椂鍣ㄦā寮忥紙鍗曟/鍛ㄦ湡锛?
 * @param user_id 鐢ㄦ埛鑷畾涔夋寚閽?
 * @param cb 鍥炶皟鍑芥暟
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_INVALID/OSAL_NO_RESOURCE`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 * @note 鍥炶皟鍑芥暟蹇呴』淇濇寔闈為樆濉炰笌鐭墽琛岋紱寤鸿浠呭仛閫氱煡鎶曢€掋€?
 */
OsalStatus_t osal_timer_create(OsalTimer_t* out_timer, const char* name, uint32_t period_ms, OsalTimerMode_e mode,
                                void* user_id, OsalTimerCallback_t cb);

/**
 * @brief 鍚姩瀹氭椂鍣紙绾跨▼涓婁笅鏂囷級
 * @param timer 瀹氭椂鍣ㄥ彞鏌?
 * @param timeout_ms 瓒呮椂鏃堕棿锛坢s锛夛紝鍙敤 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
OsalStatus_t osal_timer_start(OsalTimer_t timer, uint32_t timeout_ms);

/**
 * @brief 鍋滄瀹氭椂鍣紙绾跨▼涓婁笅鏂囷級
 * @param timer 瀹氭椂鍣ㄥ彞鏌?
 * @param timeout_ms 瓒呮椂鏃堕棿锛坢s锛夛紝鍙敤 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
OsalStatus_t osal_timer_stop(OsalTimer_t timer, uint32_t timeout_ms);

/**
 * @brief 閲嶇疆瀹氭椂鍣紙绾跨▼涓婁笅鏂囷級
 * @param timer 瀹氭椂鍣ㄥ彞鏌?
 * @param timeout_ms 瓒呮椂鏃堕棿锛坢s锛夛紝鍙敤 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 * @note reset 璇箟涓衡€滈噸鍚?閲嶈鈥濓細浠ュ綋鍓嶆椂鍒婚噸鏂拌鏃讹紱鏈繍琛屾椂绛変环浜?start銆?
 */
OsalStatus_t osal_timer_reset(OsalTimer_t timer, uint32_t timeout_ms);

/**
 * @brief 鍒犻櫎瀹氭椂鍣紙绾跨▼涓婁笅鏂囷級
 * @param timer 瀹氭椂鍣ㄥ彞鏌?
 * @param timeout_ms 瓒呮椂鏃堕棿锛坢s锛夛紝鍙敤 OSAL_WAIT_FOREVER
 * @return `OSAL_OK` 鎴愬姛锛涘け璐ヨ繑鍥?`OSAL_WOULD_BLOCK/OSAL_TIMEOUT/OSAL_INVALID`
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 * @note 涓ユ牸鍓嶇疆鏉′欢锛氳皟鐢ㄦ柟闇€纭繚鏃犲苟鍙戣闂€佹棤骞跺彂鍥炶皟銆?
 */
OsalStatus_t osal_timer_delete(OsalTimer_t timer, uint32_t timeout_ms);

/**
 * @brief 鑾峰彇鐢ㄦ埛鎸囬拡锛堢嚎绋嬩笂涓嬫枃锛?
 * @param timer 瀹氭椂鍣ㄥ彞鏌?
 * @return 鐢ㄦ埛鎸囬拡锛涘弬鏁伴潪娉曟垨涓婁笅鏂囪鐢ㄦ椂杩斿洖 NULL
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 */
void* osal_timer_get_id(OsalTimer_t timer);

/**
 * @brief 璁剧疆鐢ㄦ埛鎸囬拡锛堢嚎绋嬩笂涓嬫枃锛?
 * @param timer 瀹氭椂鍣ㄥ彞鏌?
 * @param id 鐢ㄦ埛鎸囬拡
 * @note 绂佹鍦?ISR 涓皟鐢ㄣ€?
 * @note 鍙傛暟闈炴硶鎴栦笂涓嬫枃璇敤鏃跺畨鍏ㄨ繑鍥烇紝涓嶄骇鐢熷壇浣滅敤銆?
 */
void osal_timer_set_id(OsalTimer_t timer, void* id);

#endif


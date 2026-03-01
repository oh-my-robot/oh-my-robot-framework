/**
 * @file main.c
 * @brief OSAL event_flags 鐙珛渚嬬▼
 * @details
 * - 鏈緥绋嬬敤浜庨獙璇?event_flags 鐨勬牳蹇冨悎鍚屼笌杈圭晫璇箟銆?
 * - 瑙傛祴鍙橀噺锛?
 *   - `g_event_result.total`锛氱疮璁℃柇瑷€鏁伴噺
 *   - `g_event_result.failed`锛氬け璐ユ柇瑷€鏁伴噺
 *   - `g_event_result.done`锛氱敤渚嬫槸鍚︽墽琛屽畬鎴愶紙1 琛ㄧず瀹屾垚锛?
 */
#include "osal/osal.h"
#include "osal/osal_config.h"

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} osal_event_test_result_s;

static OsalEventFlags_t g_event              = NULL;
static OsalThread_t g_test_thread             = NULL;
static OsalThread_t g_setter_thread           = NULL;
static osal_event_test_result_s g_event_result = {0u, 0u, 0u};

/**
 * @brief 绠€鍗曟柇瑷€璁℃暟鍣?
 * @param condition 闈?0 琛ㄧず鏂█閫氳繃
 */
static void osal_event_expect(int condition)
{
    g_event_result.total++;
    if (!condition)
        g_event_result.failed++;
}

/**
 * @brief 杈呭姪绾跨▼锛氬欢鏃跺悗璁剧疆浜嬩欢浣?
 * @note 鐢ㄤ簬瑙﹀彂 wait 鐨勫敜閱掕矾寰勩€?
 */
static void osal_event_setter_thread_entry(void *arg)
{
    uint32_t flags_to_set = (uint32_t)(uintptr_t)arg;

    (void)osal_sleep_ms(20u);
    (void)osal_event_flags_set(g_event, flags_to_set);
    osal_thread_exit();
}

/**
 * @brief event_flags 璇箟娴嬭瘯绾跨▼
 * @details 楠岃瘉鐐瑰垎涓冪粍锛?
 * 1) 鍒涘缓鍙傛暟鏍￠獙涓庡熀纭€鍒涘缓鍒犻櫎
 * 2) wait 鍙傛暟涓庤秴鏃惰竟鐣?
 * 3) wait_any + clear 璇箟
 * 4) wait_all 璇箟
 * 5) NO_CLEAR 璇箟
 * 6) 鍙敤浣嶆帺鐮侀潪娉曡緭鍏ユ牎楠?
 * 7) from_isr 鍦ㄧ嚎绋嬩笂涓嬫枃璇敤锛坅ssert 鍏抽棴鏃舵墽琛岋級
 */
static void osal_event_test_thread_entry(void *arg)
{
    OsalEventFlags_t event_temp  = NULL;
    OsalThreadAttr_s setter_attr = {
        "osal_event_setter",
        512u * OSAL_STACK_WORD_BYTES,
        2u,
    };
    uint32_t out_value          = 0u;
    uint32_t invalid_flags      = ~OSAL_EVENT_FLAGS_USABLE_MASK;
    uint32_t invalid_single_bit = invalid_flags & (0u - invalid_flags);

    (void)arg;

    /* 缁?1锛氬垱寤哄弬鏁颁笌鍩虹琛屼负 */
    osal_event_expect(osal_event_flags_create(NULL) == OSAL_INVALID);
    osal_event_expect(osal_event_flags_create(&g_event) == OSAL_OK);
    osal_event_expect(osal_event_flags_delete(NULL) == OSAL_INVALID);

    /* 缁?2锛歸ait 鍙傛暟涓庤秴鏃惰竟鐣?*/
    osal_event_expect(osal_event_flags_wait(g_event, 0u, &out_value, 0u, 0u) == OSAL_INVALID);
    osal_event_expect(osal_event_flags_wait(g_event, 0x01u, &out_value, 0u, 0u) == OSAL_WOULD_BLOCK);

    /* 缁?3锛歸ait_any + clear 璇箟 */
    osal_event_expect(osal_thread_create(&g_setter_thread, &setter_attr, osal_event_setter_thread_entry, (void *)0x01u) ==
                      OSAL_OK);
    osal_event_expect(osal_event_flags_wait(g_event, 0x01u, &out_value, OSAL_WAIT_FOREVER, 0u) == OSAL_OK);
    osal_event_expect((out_value & 0x01u) != 0u);
    osal_event_expect(osal_event_flags_wait(g_event, 0x01u, &out_value, 0u, 0u) == OSAL_WOULD_BLOCK);

    /* 缁?4锛歸ait_all 璇箟 */
    osal_event_expect(osal_event_flags_set(g_event, 0x03u) == OSAL_OK);
    osal_event_expect(osal_event_flags_wait(g_event, 0x03u, &out_value, 0u, OSAL_EVENT_FLAGS_WAIT_ALL) == OSAL_OK);
    osal_event_expect((out_value & 0x03u) == 0x03u);

    /* 缁?5锛歂O_CLEAR 璇箟 */
    osal_event_expect(osal_event_flags_set(g_event, 0x04u) == OSAL_OK);
    osal_event_expect(osal_event_flags_wait(g_event, 0x04u, &out_value, 0u, OSAL_EVENT_FLAGS_NO_CLEAR) == OSAL_OK);
    osal_event_expect(osal_event_flags_wait(g_event, 0x04u, &out_value, 0u, 0u) == OSAL_OK);
    osal_event_expect(osal_event_flags_clear(g_event, 0x04u) == OSAL_OK);
    osal_event_expect(osal_event_flags_wait(g_event, 0x04u, &out_value, 0u, 0u) == OSAL_WOULD_BLOCK);

    /* 缁?6锛氬彲鐢ㄤ綅鎺╃爜闈炴硶杈撳叆鏍￠獙 */
    if (invalid_single_bit != 0u) {
        osal_event_expect(osal_event_flags_set(g_event, invalid_single_bit) == OSAL_INVALID);
        osal_event_expect(osal_event_flags_clear(g_event, invalid_single_bit) == OSAL_INVALID);
        osal_event_expect(osal_event_flags_wait(g_event, invalid_single_bit, &out_value, 0u, 0u) == OSAL_INVALID);
    }

    /* 缁?7锛歠rom_isr 璇敤杩斿洖鐮侊紙浠呭湪绂佺敤 assert 鏃堕獙璇侊級 */
#ifndef __OM_USE_ASSERT
    osal_event_expect(osal_event_flags_set_from_isr(g_event, 0x08u) == OSAL_INVALID);
#endif

    osal_event_expect(osal_event_flags_delete(g_event) == OSAL_OK);
    g_event = NULL;

    /* 琛ュ厖锛氬眬閮ㄥ璞＄敓鍛藉懆鏈?*/
    osal_event_expect(osal_event_flags_create(&event_temp) == OSAL_OK);
    osal_event_expect(osal_event_flags_delete(event_temp) == OSAL_OK);

    g_event_result.done = 1u;
    for (;;)
        osal_sleep_ms(1000u);
}

/**
 * @brief 渚嬬▼鍏ュ彛
 * @return 鍒涘缓娴嬭瘯绾跨▼澶辫触杩斿洖 -1锛涙垚鍔熷悗鍚姩璋冨害鍣?
 */
int main(void)
{
    OsalThreadAttr_s test_attr = {
        "osal_event_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_event_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}


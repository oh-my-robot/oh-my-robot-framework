/**
 * @file main.c
 * @brief OSAL time 鐙珛渚嬬▼
 * @details
 * - 鏈緥绋嬬敤浜庨獙璇?time 鍘熻鍚堝悓锛岃鐩栬浆鎹€佹瘮杈冦€乻leep銆乨elay_until 鍏抽敭璇箟銆?
 * - 瑙傛祴鍙橀噺锛?
 *   - `g_time_result.total`锛氱疮璁℃柇瑷€鏁伴噺
 *   - `g_time_result.failed`锛氬け璐ユ柇瑷€鏁伴噺
 *   - `g_time_result.done`锛氱敤渚嬫槸鍚︽墽琛屽畬鎴愶紙1 琛ㄧず瀹屾垚锛?
 */
#include <stdint.h>

#include "osal/osal.h"
#include "osal/osal_config.h"

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} OsalTimeTestResult;

static OsalThread* g_test_thread = NULL;
static OsalTimeTestResult g_time_result = {0u, 0u, 0u};

/**
 * @brief 绠€鍗曟柇瑷€璁℃暟鍣?
 * @param condition 闈?0 琛ㄧず鏂█閫氳繃
 */
static void osal_time_expect(int condition)
{
    g_time_result.total++;
    if (!condition)
        g_time_result.failed++;
}

/**
 * @brief 鏃堕棿杞崲璇箟娴嬭瘯
 * @details 楠岃瘉 us/ns 鍒?ms 鐨勫悜涓婂彇鏁磋竟鐣屻€?
 */
static void osal_time_test_conversion_group(void)
{
    osal_time_expect(osal_time_us_to_ms_ceil(0ULL) == 0u);
    osal_time_expect(osal_time_us_to_ms_ceil(1ULL) == 1u);
    osal_time_expect(osal_time_us_to_ms_ceil(999ULL) == 1u);
    osal_time_expect(osal_time_us_to_ms_ceil(1000ULL) == 1u);
    osal_time_expect(osal_time_us_to_ms_ceil(1001ULL) == 2u);

    osal_time_expect(osal_time_ns_to_ms_ceil(0ULL) == 0u);
    osal_time_expect(osal_time_ns_to_ms_ceil(1ULL) == 1u);
    osal_time_expect(osal_time_ns_to_ms_ceil(999999ULL) == 1u);
    osal_time_expect(osal_time_ns_to_ms_ceil(1000000ULL) == 1u);
    osal_time_expect(osal_time_ns_to_ms_ceil(1000001ULL) == 2u);
}

/**
 * @brief 鍗曡皟鏃堕挓涓庡洖缁曞畨鍏ㄦ瘮杈冩祴璇?
 * @details
 * 1) 瀹炴椂璇绘暟涓嶅簲鍊掗€€锛?
 * 2) `osal_time_before/after` 瀵硅嚜鐒跺洖缁曞満鏅彲鐢ㄣ€?
 */
static void osal_time_test_monotonic_group(void)
{
    OsalTimeMs now_first = osal_time_now_monotonic();
    OsalTimeMs now_second = osal_time_now_monotonic();

    osal_time_expect(!osal_time_before(now_second, now_first));

    osal_time_expect(osal_time_after(11u, 10u));
    osal_time_expect(osal_time_before(10u, 11u));

    osal_time_expect(osal_time_after(0x00000010u, 0xFFFFFFF0u));
    osal_time_expect(osal_time_before(0xFFFFFFF0u, 0x00000010u));
}

/**
 * @brief sleep 璇箟娴嬭瘯
 * @details
 * - `OSAL_WAIT_FOREVER` 瀵?sleep 闈炴硶锛?
 * - 0ms/1ms sleep 搴旇繑鍥炴垚鍔熴€?
 */
static void osal_time_test_sleep_group(void)
{
    osal_time_expect(osal_sleep_ms(OSAL_WAIT_FOREVER) == OSAL_INVALID);
    osal_time_expect(osal_sleep_ms(0u) == OSAL_OK);
    osal_time_expect(osal_sleep_ms(1u) == OSAL_OK);
}

/**
 * @brief delay_until 鍩虹璇箟娴嬭瘯
 * @details
 * 1) 鍙傛暟鏍￠獙锛?
 * 2) 棣栨 `cursor=0` 鑷姩鍒濆鍖栵紱
 * 3) 姝ｅ父鍛ㄦ湡鎺ㄨ繘鍙帹杩涗竴涓懆鏈熴€?
 */
static void osal_time_test_delay_until_base_group(void)
{
    OsalTimeMs deadline_cursor_ms = 0u;
    OsalTimeMs now_before_ms = 0u;
    OsalTimeMs expected_next_deadline_ms = 0u;
    uint32_t missed_periods = 0u;

    osal_time_expect(osal_delay_until(NULL, 10u, NULL) == OSAL_INVALID);
    osal_time_expect(osal_delay_until(&deadline_cursor_ms, 0u, NULL) == OSAL_INVALID);

    now_before_ms = osal_time_now_monotonic();
    deadline_cursor_ms = 0u;
    missed_periods = 0xFFFFFFFFu;
    osal_time_expect(osal_delay_until(&deadline_cursor_ms, 5u, &missed_periods) == OSAL_OK);
    osal_time_expect(!osal_time_before(deadline_cursor_ms, (OsalTimeMs)(now_before_ms + 5u)));
    osal_time_expect(missed_periods == 0u);

    deadline_cursor_ms = (OsalTimeMs)(osal_time_now_monotonic() + 3u);
    expected_next_deadline_ms = (OsalTimeMs)(deadline_cursor_ms + 7u);
    missed_periods = 0xFFFFFFFFu;
    osal_time_expect(osal_delay_until(&deadline_cursor_ms, 7u, &missed_periods) == OSAL_OK);
    osal_time_expect(deadline_cursor_ms == expected_next_deadline_ms);
    osal_time_expect(missed_periods == 0u);
}

/**
 * @brief delay_until 杩囨湡杩借刀璇箟娴嬭瘯
 * @details
 * - 褰?deadline 宸茶繃鏈熸椂搴旇繑鍥?`OSAL_OK`锛屽苟缁熻 `missed_periods`锛?
 * - 鏈疆璋冪敤鍚庢父鏍囦粎鎺ㄨ繘涓€涓懆鏈燂紙涓嶈法澶氬懆鏈熻烦璺冿級銆?
 */
static void osal_time_test_delay_until_catchup_group(void)
{
    OsalTimeMs now_ms = osal_time_now_monotonic();
    OsalTimeMs old_deadline_ms = (OsalTimeMs)(now_ms - 25u);
    OsalTimeMs deadline_cursor_ms = old_deadline_ms;
    uint32_t missed_periods = 0u;

    osal_time_expect(osal_delay_until(&deadline_cursor_ms, 10u, &missed_periods) == OSAL_OK);
    osal_time_expect(missed_periods >= 2u);
    osal_time_expect(deadline_cursor_ms == (OsalTimeMs)(old_deadline_ms + 10u));

    now_ms = osal_time_now_monotonic();
    old_deadline_ms = (OsalTimeMs)(now_ms - 8u);
    deadline_cursor_ms = old_deadline_ms;
    osal_time_expect(osal_delay_until(&deadline_cursor_ms, 4u, NULL) == OSAL_OK);
    osal_time_expect(deadline_cursor_ms == (OsalTimeMs)(old_deadline_ms + 4u));
}

/**
 * @brief time 璇箟娴嬭瘯绾跨▼
 * @details 楠岃瘉鐐瑰垎浜旂粍锛?
 * 1) 鍗曚綅杞崲杈圭晫
 * 2) 鍗曡皟鏃堕挓涓庡洖缁曟瘮杈?
 * 3) sleep 鍚堝悓
 * 4) delay_until 鍩虹琛屼负
 * 5) delay_until 杩囨湡杩借刀琛屼负
 */
static void osal_time_test_thread_entry(void* arg)
{
    (void)arg;

    osal_time_test_conversion_group();
    osal_time_test_monotonic_group();
    osal_time_test_sleep_group();
    osal_time_test_delay_until_base_group();
    osal_time_test_delay_until_catchup_group();

    g_time_result.done = 1u;
    for (;;)
        osal_sleep_ms(1000u);
}

/**
 * @brief 渚嬬▼鍏ュ彛
 * @return 鍒涘缓娴嬭瘯绾跨▼澶辫触杩斿洖 -1锛涙垚鍔熷悗鍚姩璋冨害鍣?
 */
int main(void)
{
    OsalThreadAttr test_attr = {
        "osal_time_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_time_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}


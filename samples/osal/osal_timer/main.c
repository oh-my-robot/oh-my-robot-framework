/**
 * @file main.c
 * @brief OSAL timer 鐙珛渚嬬▼
 * @details
 * - 鏈緥绋嬬敤浜庨獙璇?timer 鐨勭敓鍛藉懆鏈熷悎鍚屼笌鍏抽敭杈圭晫璇箟銆?
 * - 瑕嗙洊鐐癸細
 *   1) create 鍙傛暟鏍￠獙涓庢ā寮忔牎楠?
 *   2) get_id/set_id 涓€鑷存€?
 *   3) reset 璇箟锛堟湭杩愯鏃剁瓑浠?start锛?
 *   4) start/stop/reset/delete 绌哄彞鏌勯槻鎶?
 *   5) one-shot 涓?periodic 琛屼负
 */
#include "osal/osal.h"
#include "osal/osal_config.h"

#define OSAL_TIMER_PERIODIC_PERIOD_MS (20u)
#define OSAL_TIMER_ONE_SHOT_PERIOD_MS (30u)

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} osal_timer_test_result_s;

static OsalThread_t g_test_thread = NULL;
static OsalTimer_t g_periodic_timer = NULL;
static OsalTimer_t g_one_shot_timer = NULL;
static osal_timer_test_result_s g_timer_result = {0u, 0u, 0u};
static volatile uint32_t g_periodic_hits = 0u;
static volatile uint32_t g_periodic_id_matches = 0u;
static volatile uint32_t g_one_shot_hits = 0u;
static uint32_t g_periodic_id_a = 0xA5A5A5A5u;
static uint32_t g_periodic_id_b = 0x5A5A5A5Au;

/**
 * @brief 绠€鍗曟柇瑷€璁℃暟鍣?
 * @param condition 闈?0 琛ㄧず鏂█閫氳繃
 */
static void osal_timer_expect(int condition)
{
    g_timer_result.total++;
    if (!condition)
        g_timer_result.failed++;
}

/**
 * @brief 鍛ㄦ湡瀹氭椂鍣ㄥ洖璋冿紙闈為樆濉烇級
 */
static void osal_periodic_timer_cb(OsalTimer_t timer)
{
    if (osal_timer_get_id(timer) == &g_periodic_id_b)
        g_periodic_id_matches++;
    g_periodic_hits++;
}

/**
 * @brief 鍗曟瀹氭椂鍣ㄥ洖璋冿紙闈為樆濉烇級
 */
static void osal_one_shot_timer_cb(OsalTimer_t timer)
{
    (void)timer;
    g_one_shot_hits++;
}

/**
 * @brief timer 鍚堝悓娴嬭瘯绾跨▼
 */
static void osal_timer_test_thread_entry(void* arg)
{
    uint32_t snapshot = 0u;
    uint32_t one_shot_snapshot = 0u;

    (void)arg;

    /* 缁?1锛歝reate 鍙傛暟涓庢ā寮忔牎楠?*/
    osal_timer_expect(osal_timer_create(NULL, "bad", 1u, OSAL_TIMER_PERIODIC, NULL, osal_periodic_timer_cb) ==
                      OSAL_INVALID);
    osal_timer_expect(osal_timer_create(&g_periodic_timer, "bad", 0u, OSAL_TIMER_PERIODIC, NULL, osal_periodic_timer_cb) ==
                      OSAL_INVALID);
    osal_timer_expect(osal_timer_create(&g_periodic_timer, "bad", 1u, OSAL_TIMER_PERIODIC, NULL, NULL) == OSAL_INVALID);
    osal_timer_expect(osal_timer_create(&g_periodic_timer, "bad", 1u, (OsalTimerMode_e)2u, NULL, osal_periodic_timer_cb) ==
                      OSAL_INVALID);

    /* 缁?2锛氬垱寤?periodic 瀹氭椂鍣ㄤ笌 id 璇诲啓 */
    osal_timer_expect(osal_timer_create(&g_periodic_timer, "osal_timer_periodic", OSAL_TIMER_PERIODIC_PERIOD_MS,
                                        OSAL_TIMER_PERIODIC, &g_periodic_id_a, osal_periodic_timer_cb) == OSAL_OK);
    osal_timer_expect(osal_timer_get_id(g_periodic_timer) == &g_periodic_id_a);
    osal_timer_set_id(g_periodic_timer, &g_periodic_id_b);
    osal_timer_expect(osal_timer_get_id(g_periodic_timer) == &g_periodic_id_b);

    /* 缁?3锛氱┖鍙ユ焺闃叉姢 */
    osal_timer_expect(osal_timer_start(NULL, 0u) == OSAL_INVALID);
    osal_timer_expect(osal_timer_stop(NULL, 0u) == OSAL_INVALID);
    osal_timer_expect(osal_timer_reset(NULL, 0u) == OSAL_INVALID);
    osal_timer_expect(osal_timer_delete(NULL, 0u) == OSAL_INVALID);
    osal_timer_expect(osal_timer_get_id(NULL) == NULL);
    osal_timer_set_id(NULL, &g_periodic_id_a);
    osal_timer_expect(1);

    /* 缁?4锛歳eset锛堟湭杩愯鎬侊級搴旂瓑浠?start */
    snapshot = g_periodic_hits;
    osal_timer_expect(osal_timer_reset(g_periodic_timer, 0u) == OSAL_OK);
    (void)osal_sleep_ms(90u);
    osal_timer_expect(g_periodic_hits > snapshot);
    osal_timer_expect(g_periodic_id_matches > 0u);

    /* 缁?5锛歴top 鍚庝笉鍐嶈Е鍙?*/
    snapshot = g_periodic_hits;
    osal_timer_expect(osal_timer_stop(g_periodic_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    (void)osal_sleep_ms(70u);
    osal_timer_expect(g_periodic_hits == snapshot);

    /* 缁?6锛氳繍琛屾€?reset 淇濇寔閲嶈璇箟 */
    osal_timer_expect(osal_timer_start(g_periodic_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    (void)osal_sleep_ms(40u);
    snapshot = g_periodic_hits;
    osal_timer_expect(osal_timer_reset(g_periodic_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    (void)osal_sleep_ms(80u);
    osal_timer_expect(g_periodic_hits > snapshot);

    /* 缁?7锛歰ne-shot 浠呰Е鍙戜竴娆?*/
    osal_timer_expect(osal_timer_create(&g_one_shot_timer, "osal_timer_one_shot", OSAL_TIMER_ONE_SHOT_PERIOD_MS,
                                        OSAL_TIMER_ONE_SHOT, NULL, osal_one_shot_timer_cb) == OSAL_OK);
    osal_timer_expect(osal_timer_start(g_one_shot_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    (void)osal_sleep_ms(120u);
    one_shot_snapshot = g_one_shot_hits;
    osal_timer_expect(one_shot_snapshot == 1u);
    (void)osal_sleep_ms(80u);
    osal_timer_expect(g_one_shot_hits == one_shot_snapshot);

    /* 缁?8锛氳祫婧愰噴鏀?*/
    osal_timer_expect(osal_timer_stop(g_periodic_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    osal_timer_expect(osal_timer_delete(g_one_shot_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    g_one_shot_timer = NULL;
    osal_timer_expect(osal_timer_delete(g_periodic_timer, OSAL_WAIT_FOREVER) == OSAL_OK);
    g_periodic_timer = NULL;

    g_timer_result.done = 1u;
    for (;;)
        (void)osal_sleep_ms(1000u);
}

/**
 * @brief 渚嬬▼鍏ュ彛
 * @return 鍒涘缓娴嬭瘯绾跨▼澶辫触杩斿洖 -1锛涙垚鍔熷悗鍚姩璋冨害鍣?
 */
int main(void)
{
    OsalThreadAttr_s test_attr = {
        "osal_timer_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_timer_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}


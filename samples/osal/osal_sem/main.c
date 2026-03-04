/**
 * @file main.c
 * @brief OSAL semaphore 鐙珛渚嬬▼
 * @details
 * - 鏈緥绋嬬敤浜庨獙璇?semaphore 鐨勬牳蹇冨悎鍚岋紝涓嶄緷璧栧叾浠栧悓姝ュ師璇涔夈€?
 * - 瑙傛祴鍙橀噺锛?
 *   - `g_sem_result.total`锛氱疮璁℃柇瑷€鏁伴噺
 *   - `g_sem_result.failed`锛氬け璐ユ柇瑷€鏁伴噺
 *   - `g_sem_result.done`锛氱敤渚嬫槸鍚︽墽琛屽畬鎴愶紙1 琛ㄧず瀹屾垚锛?
 */
#include "osal/osal.h"
#include "osal/osal_config.h"

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} OsalSemTestResult;

static OsalSem* g_sem = NULL;
static OsalThread* g_test_thread = NULL;
static OsalThread* g_post_thread = NULL;
static OsalSemTestResult g_sem_result = {0u, 0u, 0u};

/**
 * @brief 绠€鍗曟柇瑷€璁℃暟鍣?
 * @param condition 闈?0 琛ㄧず鏂█閫氳繃
 */
static void osal_sem_expect(int condition)
{
    g_sem_result.total++;
    if (!condition)
        g_sem_result.failed++;
}

/**
 * @brief 杈呭姪绾跨▼锛氬欢鏃跺悗 post 涓€娆?
 * @note 鐢ㄤ簬瑙﹀彂 `wait(OSAL_WAIT_FOREVER)` 鐨勫敜閱掕矾寰勩€?
 */
static void osal_sem_post_once_thread(void* arg)
{
    (void)arg;
    (void)osal_sleep_ms(20u);
    (void)osal_sem_post(g_sem);
    osal_thread_exit();
}

/**
 * @brief semaphore 璇箟娴嬭瘯绾跨▼
 * @details 楠岃瘉鐐瑰垎鍥涚粍锛?
 * 1) 鍙傛暟鍚堟硶鎬э紙init_count > max_count锛?
 * 2) 璁℃暟琛屼负锛坓et_count銆亀ould-block銆乸ost 绱姞锛?
 * 3) 婊¤鏁拌涓猴紙post 杩斿洖 `OSAL_NO_RESOURCE`锛?
 * 4) `OSAL_WAIT_FOREVER` 璇箟锛堢敱杈呭姪绾跨▼ post 鍞ら啋锛?
 */
static void osal_sem_test_thread_entry(void* arg)
{
    OsalSem* invalid_sem = NULL;
    uint32_t sem_count = 0u;
    OsalThreadAttr post_attr = {
        "osal_sem_poster",
        512u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    (void)arg;

    /* 缁?1锛氬弬鏁板悎娉曟€?*/
    osal_sem_expect(osal_sem_create(&invalid_sem, 1u, 2u) == OSAL_INVALID);

    /* 缁?2锛氬熀纭€璁℃暟琛屼负 */
    osal_sem_expect(osal_sem_create(&g_sem, 2u, 0u) == OSAL_OK);
    osal_sem_expect(osal_sem_get_count(g_sem, &sem_count) == OSAL_OK && sem_count == 0u);
    osal_sem_expect(osal_sem_wait(g_sem, 0u) == OSAL_WOULD_BLOCK);

    osal_sem_expect(osal_sem_post(g_sem) == OSAL_OK);
    osal_sem_expect(osal_sem_post(g_sem) == OSAL_OK);
    osal_sem_expect(osal_sem_get_count(g_sem, &sem_count) == OSAL_OK && sem_count == 2u);

    /* 缁?3锛氭弧璁℃暟鏃?post 涓嶉樆濉烇紝杩斿洖 NO_RESOURCE */
    osal_sem_expect(osal_sem_post(g_sem) == OSAL_NO_RESOURCE);

    osal_sem_expect(osal_sem_wait(g_sem, 0u) == OSAL_OK);
    osal_sem_expect(osal_sem_wait(g_sem, 0u) == OSAL_OK);
    osal_sem_expect(osal_sem_wait(g_sem, 0u) == OSAL_WOULD_BLOCK);

    /* 缁?4锛歐AIT_FOREVER 鐢辫緟鍔╃嚎绋?post 鍞ら啋 */
    osal_sem_expect(osal_thread_create(&g_post_thread, &post_attr, osal_sem_post_once_thread, NULL) == OSAL_OK);
    osal_sem_expect(osal_sem_wait(g_sem, OSAL_WAIT_FOREVER) == OSAL_OK);
    osal_sem_expect(osal_sem_get_count(g_sem, &sem_count) == OSAL_OK && sem_count == 0u);

    /* 鏀跺熬锛氶噴鏀捐祫婧?*/
    osal_sem_expect(osal_sem_delete(g_sem) == OSAL_OK);
    g_sem = NULL;

    g_sem_result.done = 1u;
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
        "osal_sem_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_sem_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}


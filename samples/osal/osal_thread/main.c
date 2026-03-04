/**
 * @file main.c
 * @brief OSAL thread 鐙珛渚嬬▼
 * @details
 * - 鏈緥绋嬬敤浜庨獙璇?thread 鐢熷懡鍛ㄦ湡鍚堝悓涓庡叧閿竟鐣岃涓恒€?
 * - 瑕嗙洊鐐癸細
 *   1) create 鍙傛暟鏍￠獙
 *   2) self / yield 鍩虹璇箟
 *   3) join 鍙€夎兘鍔涳紙褰撳墠 FreeRTOS 绔繑鍥?`OSAL_NOT_SUPPORTED`锛?
 *   4) terminate(self) 闈炴硶绾︽潫
 *   5) terminate(other) 鍗遍櫓璇箟淇濈暀
 */
#include "osal/osal.h"
#include "osal/osal_config.h"

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} OsalThreadTestResult;

static OsalThread* g_test_thread = NULL;
static OsalThread* g_worker_thread = NULL;
static volatile uint32_t g_worker_counter = 0u;
static OsalThreadTestResult g_thread_result = {0u, 0u, 0u};

/**
 * @brief 绠€鍗曟柇瑷€璁℃暟鍣?
 * @param condition 闈?0 琛ㄧず鏂█閫氳繃
 */
static void osal_thread_expect(int condition)
{
    g_thread_result.total++;
    if (!condition)
        g_thread_result.failed++;
}

/**
 * @brief 琚粓姝㈢洰鏍囩嚎绋?
 * @note 鎸佺画鏇存柊璁℃暟锛岀敤浜庤娴?terminate(other) 鐢熸晥銆?
 */
static void osal_thread_worker_entry(void* arg)
{
    (void)arg;

    for (;;)
    {
        g_worker_counter++;
        (void)osal_sleep_ms(5u);
    }
}

/**
 * @brief thread 鍚堝悓娴嬭瘯绾跨▼
 */
static void osal_thread_test_entry(void* arg)
{
    OsalThreadAttr worker_attr = {
        "osal_thread_worker",
        512u * OSAL_STACK_WORD_BYTES,
        2u,
    };
    uint32_t snapshot = 0u;

    (void)arg;

    /* 缁?1锛歝reate 鍙傛暟鏍￠獙 */
    osal_thread_expect(osal_thread_create(NULL, &worker_attr, osal_thread_worker_entry, NULL) == OSAL_INVALID);
    osal_thread_expect(osal_thread_create(&g_worker_thread, &worker_attr, NULL, NULL) == OSAL_INVALID);

    /* 缁?2锛歴elf / yield 鍩虹璇箟 */
    osal_thread_expect(osal_thread_self() != NULL);
    osal_thread_yield();
    osal_thread_expect(1);

    /* 缁?3锛歫oin 鍙€夎兘鍔涳紙褰撳墠绔彛搴旇繑鍥?NOT_SUPPORTED锛?*/
    osal_thread_expect(osal_thread_join(NULL, 0u) == OSAL_INVALID);
    osal_thread_expect(osal_thread_create(&g_worker_thread, &worker_attr, osal_thread_worker_entry, NULL) == OSAL_OK);
    osal_thread_expect(osal_thread_join(g_worker_thread, 0u) == OSAL_NOT_SUPPORTED);

    /* 缁?4锛歵erminate(self) 闈炴硶绾︽潫 */
    osal_thread_expect(osal_thread_terminate(osal_thread_self()) == OSAL_INVALID);

    /* 缁?5锛歵erminate(other) 鍗遍櫓璇箟淇濈暀 */
    (void)osal_sleep_ms(30u);
    osal_thread_expect(g_worker_counter > 0u);
    osal_thread_expect(osal_thread_terminate(g_worker_thread) == OSAL_OK);
    g_worker_thread = NULL;

    snapshot = g_worker_counter;
    (void)osal_sleep_ms(20u);
    osal_thread_expect(g_worker_counter == snapshot);

    g_thread_result.done = 1u;
    for (;;)
        (void)osal_sleep_ms(1000u);
}

/**
 * @brief 渚嬬▼鍏ュ彛
 * @return 鍒涘缓娴嬭瘯绾跨▼澶辫触杩斿洖 -1锛涙垚鍔熷悗鍚姩璋冨害鍣?
 */
int main(void)
{
    OsalThreadAttr test_attr = {
        "osal_thread_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_thread_test_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}


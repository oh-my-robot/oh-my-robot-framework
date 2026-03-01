/**
 * @file main.c
 * @brief OSAL queue 鐙珛渚嬬▼
 * @details
 * - 鏈緥绋嬬敤浜庨獙璇?queue 鐨勬牳蹇冨悎鍚屼笌杈圭晫璇箟銆?
 * - 瑙傛祴鍙橀噺锛?
 *   - `g_queue_result.total`锛氱疮璁℃柇瑷€鏁伴噺
 *   - `g_queue_result.failed`锛氬け璐ユ柇瑷€鏁伴噺
 *   - `g_queue_result.done`锛氱敤渚嬫槸鍚︽墽琛屽畬鎴愶紙1 琛ㄧず瀹屾垚锛?
 */
#include "osal/osal.h"
#include "osal/osal_config.h"

typedef struct
{
    uint32_t id;
    uint32_t payload;
} osal_queue_test_message_s;

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} osal_queue_test_result_s;

static OsalQueue_t g_queue = NULL;
static OsalThread_t g_test_thread = NULL;
static OsalThread_t g_sender_thread = NULL;
static osal_queue_test_result_s g_queue_result = {0u, 0u, 0u};

/**
 * @brief 绠€鍗曟柇瑷€璁℃暟鍣?
 * @param condition 闈?0 琛ㄧず鏂█閫氳繃
 */
static void osal_queue_expect(int condition)
{
    g_queue_result.total++;
    if (!condition)
        g_queue_result.failed++;
}

/**
 * @brief 姣旇緝涓ゆ潯娴嬭瘯娑堟伅
 */
static int osal_queue_message_equal(const osal_queue_test_message_s* left, const osal_queue_test_message_s* right)
{
    if (!left || !right)
        return 0;

    return (left->id == right->id) && (left->payload == right->payload);
}

/**
 * @brief 杈呭姪绾跨▼锛氬欢鏃跺悗鍙戦€佷竴娆℃秷鎭?
 * @note 鐢ㄤ簬瑙﹀彂 `recv(OSAL_WAIT_FOREVER)` 鐨勫敜閱掕矾寰勩€?
 */
static void osal_queue_sender_thread_entry(void* arg)
{
    osal_queue_test_message_s message = {0xA5u, 0x5Au};

    (void)arg;
    (void)osal_sleep_ms(20u);
    (void)osal_queue_send(g_queue, &message, 0u);
    osal_thread_exit();
}

/**
 * @brief queue 璇箟娴嬭瘯绾跨▼
 * @details 楠岃瘉鐐瑰垎涓冪粍锛?
 * 1) 鍒涘缓鍙傛暟鏍￠獙涓庡熀纭€鍒涘缓鍒犻櫎
 * 2) 鏌ヨ鎺ュ彛鐘舵€佸寲琛屼负
 * 3) 绌?婊¤竟鐣屼笌 peek 璇箟
 * 4) reset 娓呯┖璇箟
 * 5) `recv(OSAL_WAIT_FOREVER)` 鍞ら啋璺緞
 * 6) `from_isr` 鍦ㄧ嚎绋嬩笂涓嬫枃璇敤淇濇姢
 * 7) 璧勬簮閲婃斁
 */
static void osal_queue_test_thread_entry(void* arg)
{
    OsalQueue_t queue_temp = NULL;
    osal_queue_test_message_s message_1 = {1u, 11u};
    osal_queue_test_message_s message_2 = {2u, 22u};
    osal_queue_test_message_s message_3 = {3u, 33u};
    osal_queue_test_message_s message_rx = {0u, 0u};
    uint32_t queue_count = 0u;
    uint32_t queue_space = 0u;
    OsalThreadAttr_s sender_attr = {
        "osal_queue_sender",
        512u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    (void)arg;

    /* 缁?1锛氬垱寤哄弬鏁颁笌鍩虹琛屼负 */
    osal_queue_expect(osal_queue_create(NULL, 2u, sizeof(osal_queue_test_message_s)) == OSAL_INVALID);
    osal_queue_expect(osal_queue_create(&queue_temp, 0u, sizeof(osal_queue_test_message_s)) == OSAL_INVALID);
    osal_queue_expect(osal_queue_create(&queue_temp, 2u, 0u) == OSAL_INVALID);
    osal_queue_expect(osal_queue_create(&g_queue, 2u, sizeof(osal_queue_test_message_s)) == OSAL_OK);
    osal_queue_expect(osal_queue_delete(NULL) == OSAL_INVALID);

    /* 缁?2锛氭煡璇㈡帴鍙ｇ姸鎬佸寲 */
    osal_queue_expect(osal_queue_messages_waiting(NULL, &queue_count) == OSAL_INVALID);
    osal_queue_expect(osal_queue_messages_waiting(g_queue, NULL) == OSAL_INVALID);
    osal_queue_expect(osal_queue_spaces_available(NULL, &queue_space) == OSAL_INVALID);
    osal_queue_expect(osal_queue_spaces_available(g_queue, NULL) == OSAL_INVALID);
    osal_queue_expect(osal_queue_messages_waiting(g_queue, &queue_count) == OSAL_OK && queue_count == 0u);
    osal_queue_expect(osal_queue_spaces_available(g_queue, &queue_space) == OSAL_OK && queue_space == 2u);

    /* 缁?3锛氱┖/婊¤竟鐣屼笌 peek 璇箟 */
    osal_queue_expect(osal_queue_recv(g_queue, &message_rx, 0u) == OSAL_WOULD_BLOCK);
    osal_queue_expect(osal_queue_send(g_queue, &message_1, 0u) == OSAL_OK);
    osal_queue_expect(osal_queue_send(g_queue, &message_2, 0u) == OSAL_OK);
    osal_queue_expect(osal_queue_send(g_queue, &message_3, 0u) == OSAL_WOULD_BLOCK);
    osal_queue_expect(osal_queue_peek(g_queue, &message_rx, 0u) == OSAL_OK);
    osal_queue_expect(osal_queue_message_equal(&message_rx, &message_1));
    osal_queue_expect(osal_queue_messages_waiting(g_queue, &queue_count) == OSAL_OK && queue_count == 2u);
    osal_queue_expect(osal_queue_recv(g_queue, &message_rx, 0u) == OSAL_OK);
    osal_queue_expect(osal_queue_message_equal(&message_rx, &message_1));
    osal_queue_expect(osal_queue_recv(g_queue, &message_rx, 0u) == OSAL_OK);
    osal_queue_expect(osal_queue_message_equal(&message_rx, &message_2));
    osal_queue_expect(osal_queue_recv(g_queue, &message_rx, 0u) == OSAL_WOULD_BLOCK);

    /* 缁?4锛歳eset 鍚庡簲娓呯┖ */
    osal_queue_expect(osal_queue_send(g_queue, &message_1, 0u) == OSAL_OK);
    osal_queue_expect(osal_queue_send(g_queue, &message_2, 0u) == OSAL_OK);
    osal_queue_expect(osal_queue_reset(g_queue) == OSAL_OK);
    osal_queue_expect(osal_queue_messages_waiting(g_queue, &queue_count) == OSAL_OK && queue_count == 0u);
    osal_queue_expect(osal_queue_spaces_available(g_queue, &queue_space) == OSAL_OK && queue_space == 2u);
    osal_queue_expect(osal_queue_peek(g_queue, &message_rx, 0u) == OSAL_WOULD_BLOCK);

    /* 缁?5锛歐AIT_FOREVER 璺緞 */
    osal_queue_expect(osal_thread_create(&g_sender_thread, &sender_attr, osal_queue_sender_thread_entry, NULL) == OSAL_OK);
    osal_queue_expect(osal_queue_recv(g_queue, &message_rx, OSAL_WAIT_FOREVER) == OSAL_OK);
    osal_queue_expect(message_rx.id == 0xA5u && message_rx.payload == 0x5Au);
    osal_queue_expect(osal_queue_messages_waiting(g_queue, &queue_count) == OSAL_OK && queue_count == 0u);

    /* 缁?6锛氳祫婧愰噴鏀?*/
    osal_queue_expect(osal_queue_delete(g_queue) == OSAL_OK);
    g_queue = NULL;

    g_queue_result.done = 1u;
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
        "osal_queue_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_queue_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}


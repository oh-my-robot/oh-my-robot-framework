/**
 * @file main.c
 * @brief OSAL mutex 鐙珛渚嬬▼
 * @details
 * - 鏈緥绋嬬敤浜庨獙璇?mutex 鐨勬墍鏈夋潈涓庨潪閫掑綊璇箟鍚堝悓銆?
 * - 瑙傛祴鍙橀噺锛?
 *   - `g_mutex_result.total`锛氱疮璁℃柇瑷€鏁伴噺
 *   - `g_mutex_result.failed`锛氬け璐ユ柇瑷€鏁伴噺
 *   - `g_mutex_result.done`锛氱敤渚嬫槸鍚︽墽琛屽畬鎴愶紙1 琛ㄧず瀹屾垚锛?
 */
#include "osal/osal.h"
#include "osal/osal_config.h"

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} OsalMutexTestResult;

static OsalMutex* g_mutex = NULL;
static OsalSem* g_owner_ready_sem = NULL;
static OsalSem* g_owner_done_sem = NULL;

static OsalThread* g_test_thread = NULL;
static OsalThread* g_owner_thread = NULL;

static OsalMutexTestResult g_mutex_result = {0u, 0u, 0u};

/**
 * @brief 绠€鍗曟柇瑷€璁℃暟鍣?
 * @param condition 闈?0 琛ㄧず鏂█閫氳繃
 */
static void osal_mutex_expect(int condition)
{
    g_mutex_result.total++;
    if (!condition)
        g_mutex_result.failed++;
}

/**
 * @brief owner 绾跨▼
 * @details
 * - 鎸佹湁 mutex 涓€娈垫椂闂达紝鍒堕€犺法绾跨▼绔炰簤绐楀彛銆?
 * - 閫氳繃涓や釜 semaphore 涓庢祴璇曠嚎绋嬪悓姝ワ細
 *   - `g_owner_ready_sem`锛氬凡鎸侀攣
 *   - `g_owner_done_sem`锛氬凡閲婃斁骞剁粨鏉?
 */
static void osal_mutex_owner_thread_entry(void* arg)
{
    (void)arg;

    if (osal_mutex_lock(g_mutex, OSAL_WAIT_FOREVER) == OSAL_OK)
    {
        (void)osal_sem_post(g_owner_ready_sem);
        (void)osal_sleep_ms(80u);
        (void)osal_mutex_unlock(g_mutex);
    }

    (void)osal_sem_post(g_owner_done_sem);
    osal_thread_exit();
}

/**
 * @brief mutex 璇箟娴嬭瘯绾跨▼
 * @details 楠岃瘉鐐瑰垎浜旂粍锛?
 * 1) 鍩虹鍒涘缓涓庡悓姝ヨ祫婧愬垱寤?
 * 2) 闈?owner 瑙ｉ攣搴旇繑鍥?`OSAL_INVALID`
 * 3) 闈為€掑綊琛屼负锛氬悓绾跨▼浜屾 lock(0) 杩斿洖 `OSAL_WOULD_BLOCK`
 * 4) 璺ㄧ嚎绋嬫寔閿佹湡闂达細闈?owner 瑙ｉ攣涓庨潪闃诲 lock 琛屼负
 * 5) owner 閲婃斁鍚庡彲鍐嶆 lock/unlock 鎴愬姛
 */
static void osal_mutex_test_thread_entry(void* arg)
{
    OsalThreadAttr owner_attr = {
        "osal_mutex_owner",
        512u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    (void)arg;

    /* 缁?1锛氬熀纭€鍒涘缓 */
    osal_mutex_expect(osal_mutex_create(&g_mutex) == OSAL_OK);
    osal_mutex_expect(osal_sem_create(&g_owner_ready_sem, 1u, 0u) == OSAL_OK);
    osal_mutex_expect(osal_sem_create(&g_owner_done_sem, 1u, 0u) == OSAL_OK);

    /* 缁?2锛氭湭鎸佹湁鍗宠В閿侊紝蹇呴』鎶?INVALID */
    osal_mutex_expect(osal_mutex_unlock(g_mutex) == OSAL_INVALID);

    /* 缁?3锛氶潪閫掑綊璇箟楠岃瘉 */
    osal_mutex_expect(osal_mutex_lock(g_mutex, OSAL_WAIT_FOREVER) == OSAL_OK);
    osal_mutex_expect(osal_mutex_lock(g_mutex, 0u) == OSAL_WOULD_BLOCK);
    osal_mutex_expect(osal_mutex_unlock(g_mutex) == OSAL_OK);

    /* 缁?4锛氳法绾跨▼ owner 鎸侀攣绐楀彛 */
    osal_mutex_expect(osal_thread_create(&g_owner_thread, &owner_attr, osal_mutex_owner_thread_entry, NULL) == OSAL_OK);
    osal_mutex_expect(osal_sem_wait(g_owner_ready_sem, OSAL_WAIT_FOREVER) == OSAL_OK);

    osal_mutex_expect(osal_mutex_unlock(g_mutex) == OSAL_INVALID);
    osal_mutex_expect(osal_mutex_lock(g_mutex, 0u) == OSAL_WOULD_BLOCK);

    /* 缁?5锛歰wner 閲婃斁鍚庢仮澶嶅彲鐢?*/
    osal_mutex_expect(osal_sem_wait(g_owner_done_sem, OSAL_WAIT_FOREVER) == OSAL_OK);
    osal_mutex_expect(osal_mutex_lock(g_mutex, 0u) == OSAL_OK);
    osal_mutex_expect(osal_mutex_unlock(g_mutex) == OSAL_OK);

    /* 鏀跺熬锛氶噴鏀捐祫婧?*/
    osal_mutex_expect(osal_sem_delete(g_owner_ready_sem) == OSAL_OK);
    osal_mutex_expect(osal_sem_delete(g_owner_done_sem) == OSAL_OK);
    osal_mutex_expect(osal_mutex_delete(g_mutex) == OSAL_OK);

    g_owner_ready_sem = NULL;
    g_owner_done_sem = NULL;
    g_mutex = NULL;
    g_mutex_result.done = 1u;

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
        "osal_mutex_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_mutex_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}


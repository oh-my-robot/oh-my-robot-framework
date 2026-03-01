#include "osal/osal.h"

/*
 * OSAL 缁煎悎娴嬭瘯绀轰緥锛?
 * 1) 绾跨▼ + 闃熷垪锛氱敓浜ц€呭悜闃熷垪鎶曢€掓暟鎹紝娑堣垂鑰呭彇鏁版嵁
 * 2) 浜嬩欢锛氱敓浜ц€呭悜娑堣垂鑰呭彂閫佷簨浠堕€氱煡
 * 3) 瀹氭椂鍣?+ 淇″彿閲忥細瀹氭椂鍣ㄥ懆鏈熻Е鍙戯紝鍞ら啋鐩戞帶绾跨▼
 * 4) 浜掓枼閿侊細淇濇姢鍏变韩璁℃暟鍣?
 * 5) 鏃堕棿鎺ュ彛锛氳褰曡繍琛屾椂闂存埑
 *
 * 浣跨敤鏂规硶锛氬湪 RTOS 宸ョ▼涓紪璇戣鏂囦欢锛屽苟鍦?main 涓皟鐢?osal_kernel_start()
 * 娉ㄦ剰锛氭湰绀轰緥涓嶄緷璧栨爣鍑嗚緭鍑猴紝寤鸿閫氳繃鏂偣鎴栫洃瑙嗗彉閲忚瀵熺粨鏋?
 */

#define TEST_QUEUE_LEN 8U
#define TEST_TIMER_MS 100U
#define TEST_PROD_PERIOD 10U

typedef struct
{
    uint32_t seq;
    uint32_t time_ms;
} osal_test_msg_s;

static OsalQueue_t g_queue;
static OsalMutex_t g_mutex;
static OsalSem_t g_sem;
static OsalEventFlags_t g_event;
static OsalThread_t g_thread_prod;
static OsalThread_t g_thread_cons;
static OsalThread_t g_thread_mon;
static OsalThread_t g_thread_cnt;
static OsalThread_t g_thread_edge;
static OsalTimer_t g_timer;

/* 杩愯鐘舵€佺粺璁★紝鏂逛究璋冭瘯瑙傚療 */
static volatile uint32_t g_produced_cnt;
static volatile uint32_t g_consumed_cnt;
static volatile uint32_t g_monitor_cnt;
static volatile uint32_t g_shared_counter;
static volatile uint32_t g_counter_hits;
static volatile uint32_t g_edge_tests;
static volatile uint32_t g_edge_failures;
static volatile uint32_t g_edge_done;

static void _timer_cb(OsalTimer_t timer)
{
    (void)timer;
    /* 瀹氭椂鍣ㄥ洖璋冧腑鍙戦€佷俊鍙烽噺锛屽敜閱掔洃鎺х嚎绋?*/
    (void)osal_sem_post(g_sem);
}

static void _producer_thread(void* arg)
{
    (void)arg;
    osal_test_msg_s msg;
    OsalTimeMs_t last_ms = osal_time_now_monotonic();

    while (1)
    {
        /* 鐢熸垚鏁版嵁 */
        msg.seq = g_produced_cnt++;
        msg.time_ms = (uint32_t)osal_time_now_monotonic();

        /* 淇濇姢鍏变韩璁℃暟鍣?*/
        if (osal_mutex_lock(g_mutex, OSAL_WAIT_FOREVER) == OSAL_OK)
        {
            g_shared_counter++;
            (void)osal_mutex_unlock(g_mutex);
        }

        /* 鎶曢€掑埌闃熷垪锛岃嫢瓒呮椂鍒欐斁寮冩湰娆?*/
        (void)osal_queue_send(g_queue, &msg, 5U);

        /* 閫氱煡娑堣垂鑰呯嚎绋嬶紙浜嬩欢瀵硅薄鍖栵紝閬垮厤绾跨▼閫氱煡浣嶅啿绐侊級 */
        (void)osal_event_flags_set(g_event, 0x01U);

        /* 鍥哄畾鍛ㄦ湡杩愯 */
        (void)osal_delay_until(&last_ms, TEST_PROD_PERIOD, NULL);
    }
}

static void _consumer_thread(void* arg)
{
    (void)arg;
    osal_test_msg_s msg;

    while (1)
    {
        uint32_t value = 0;
        /* 绛夊緟浜嬩欢閫氱煡 */
        if (osal_event_flags_wait(g_event, 0x01U, &value, OSAL_WAIT_FOREVER, 0U) == OSAL_OK)
        {
            /* 鏀跺埌浜嬩欢鍚庡皾璇曞彇闃熷垪鏁版嵁 */
            if (osal_queue_recv(g_queue, &msg, 10U) == OSAL_OK)
            {
                g_consumed_cnt++;
            }
        }
    }
}

static void _monitor_thread(void* arg)
{
    (void)arg;
    while (1)
    {
        /* 鐢卞畾鏃跺櫒鍛ㄦ湡鍞ら啋 */
        if (osal_sem_wait(g_sem, OSAL_WAIT_FOREVER) == OSAL_OK)
        {
            g_monitor_cnt++;
        }
    }
}

static void _counter_thread(void* arg)
{
    (void)arg;
    /* 楂橀绔炰簤浜掓枼閿侊紝鐢ㄤ簬楠岃瘉浜掓枼鏁堟灉 */
    while (1)
    {
        if (osal_mutex_lock(g_mutex, 1U) == OSAL_OK)
        {
            g_shared_counter++;
            g_counter_hits++;
            (void)osal_mutex_unlock(g_mutex);
        }
        /* 璁╁嚭CPU锛岄伩鍏嶇嫭鍗?*/
        osal_sleep_ms(1U);
    }
}

static void _edge_thread(void* arg)
{
    (void)arg;
    uint32_t tests = 0;
    uint32_t failures = 0;

    /* 闃熷垪杈圭晫娴嬭瘯 */
    {
        OsalQueue_t q = NULL;
        uint8_t item = 0x5A;

        tests++;
        if (osal_queue_create(NULL, 1U, 1U) != OSAL_INVALID)
            failures++;

        tests++;
        if (osal_queue_create(&q, 0U, 1U) != OSAL_INVALID)
            failures++;

        tests++;
        if (osal_queue_create(&q, 1U, 0U) != OSAL_INVALID)
            failures++;

        tests++;
        if (osal_queue_create(&q, 1U, 1U) != OSAL_OK)
            failures++;

        tests++;
        if (osal_queue_recv(q, &item, 0U) != OSAL_WOULD_BLOCK)
            failures++;

        tests++;
        if (osal_queue_send(q, NULL, 0U) != OSAL_INVALID)
            failures++;

        tests++;
        if (osal_queue_send(q, &item, 0U) != OSAL_OK)
            failures++;

        tests++;
        if (osal_queue_send(q, &item, 0U) != OSAL_WOULD_BLOCK)
            failures++;

        tests++;
        if (osal_queue_recv(q, &item, 0U) != OSAL_OK)
            failures++;

        tests++;
        if (osal_queue_delete(NULL) != OSAL_INVALID)
            failures++;

        tests++;
        if (osal_queue_delete(q) != OSAL_OK)
            failures++;
    }

    /* 淇″彿閲忚竟鐣屾祴璇?*/
    {
        OsalSem_t s = NULL;
        uint32_t sem_count = 0u;

        tests++;
        if (osal_sem_create(&s, 1U, 2U) != OSAL_INVALID)
            failures++;

        tests++;
        if (osal_sem_create(&s, 1U, 0U) != OSAL_OK)
            failures++;

        tests++;
        if (osal_sem_get_count(s, &sem_count) != OSAL_OK || sem_count != 0u)
            failures++;

        tests++;
        if (osal_sem_wait(s, 0U) != OSAL_WOULD_BLOCK)
            failures++;

        tests++;
        if (osal_sem_post(s) != OSAL_OK)
            failures++;

        tests++;
        if (osal_sem_get_count(s, &sem_count) != OSAL_OK || sem_count != 1u)
            failures++;

        tests++;
        if (osal_sem_post(s) != OSAL_NO_RESOURCE)
            failures++;

        tests++;
        if (osal_sem_wait(s, 0U) != OSAL_OK)
            failures++;

        tests++;
        if (osal_sem_get_count(s, &sem_count) != OSAL_OK || sem_count != 0u)
            failures++;

        tests++;
        if (osal_sem_delete(NULL) != OSAL_INVALID)
            failures++;

        tests++;
        if (osal_sem_delete(s) != OSAL_OK)
            failures++;
    }

    /* 浜掓枼閿佽竟鐣屾祴璇?*/
    {
        OsalMutex_t m = NULL;

        tests++;
        if (osal_mutex_create(&m) != OSAL_OK)
            failures++;

        tests++;
        if (osal_mutex_unlock(m) != OSAL_INVALID)
            failures++;

        tests++;
        if (osal_mutex_lock(m, OSAL_WAIT_FOREVER) != OSAL_OK)
            failures++;

        tests++;
        if (osal_mutex_lock(m, 0U) != OSAL_WOULD_BLOCK)
            failures++;

        tests++;
        if (osal_mutex_unlock(m) != OSAL_OK)
            failures++;

        tests++;
        if (osal_mutex_delete(NULL) != OSAL_INVALID)
            failures++;

        tests++;
        if (osal_mutex_delete(m) != OSAL_OK)
            failures++;
    }

    /* 浜嬩欢杈圭晫娴嬭瘯 */
    {
        uint32_t value = 0;
        OsalEventFlags_t event = NULL;

        tests++;
        if (osal_event_flags_create(&event) != OSAL_OK)
            failures++;

        tests++;
        if (osal_event_flags_wait(event, 0x02U, &value, 0U, 0U) != OSAL_WOULD_BLOCK)
            failures++;

        (void)osal_event_flags_delete(event);
    }

    /* 瀹氭椂鍣ㄨ竟鐣屾祴璇?*/
    {
        OsalTimer_t timer = NULL;

        tests++;
        if (osal_timer_create(NULL, "bad_timer", 1U, OSAL_TIMER_PERIODIC, NULL, _timer_cb) != OSAL_INVALID)
            failures++;

        tests++;
        if (osal_timer_create(&timer, "bad_timer", 0U, OSAL_TIMER_PERIODIC, NULL, _timer_cb) != OSAL_INVALID)
            failures++;

        tests++;
        if (osal_timer_create(&timer, "bad_timer2", 1U, OSAL_TIMER_PERIODIC, NULL, NULL) != OSAL_INVALID)
            failures++;
    }

    /* 鏃堕棿鎺ュ彛杈圭晫娴嬭瘯 */
    {
        OsalTimeMs_t ms = osal_time_now_monotonic();
        OsalTimeMs_t ms_next = osal_time_now_monotonic();
        tests++;
        if (osal_time_before(ms_next, ms))
            failures++;
    }

    g_edge_tests = tests;
    g_edge_failures = failures;
    g_edge_done = 1U;

    while (1)
    {
        osal_sleep_ms(1000U);
    }
}

int main(void)
{
    /* 鍒涘缓浜嬩欢瀵硅薄 */
    if (osal_event_flags_create(&g_event) != OSAL_OK)
        return -1;

    /* 鍒涘缓闃熷垪 */
    if (osal_queue_create(&g_queue, TEST_QUEUE_LEN, sizeof(osal_test_msg_s)) != OSAL_OK)
        return -1;

    /* 鍒涘缓浜掓枼閿?*/
    if (osal_mutex_create(&g_mutex) != OSAL_OK)
        return -1;

    /* 鍒涘缓淇″彿閲?*/
    if (osal_sem_create(&g_sem, 10U, 0U) != OSAL_OK)
        return -1;

    /* 鍒涘缓绾跨▼ */
    OsalThreadAttr_s attr_prod = {"osal_prod", 512U * OSAL_STACK_WORD_BYTES, 2U};
    OsalThreadAttr_s attr_cons = {"osal_cons", 512U * OSAL_STACK_WORD_BYTES, 2U};
    OsalThreadAttr_s attr_mon = {"osal_mon", 512U * OSAL_STACK_WORD_BYTES, 1U};
    OsalThreadAttr_s attr_cnt = {"osal_cnt", 512U * OSAL_STACK_WORD_BYTES, 1U};
    OsalThreadAttr_s attr_edge = {"osal_edge", 768U * OSAL_STACK_WORD_BYTES, 2U};

    if (osal_thread_create(&g_thread_prod, &attr_prod, _producer_thread, NULL) != OSAL_OK)
        return -1;
    if (osal_thread_create(&g_thread_cons, &attr_cons, _consumer_thread, NULL) != OSAL_OK)
        return -1;
    if (osal_thread_create(&g_thread_mon, &attr_mon, _monitor_thread, NULL) != OSAL_OK)
        return -1;
    if (osal_thread_create(&g_thread_cnt, &attr_cnt, _counter_thread, NULL) != OSAL_OK)
        return -1;
    if (osal_thread_create(&g_thread_edge, &attr_edge, _edge_thread, NULL) != OSAL_OK)
        return -1;

    /* 鍒涘缓骞跺惎鍔ㄥ畾鏃跺櫒 */
    if (osal_timer_create(&g_timer, "osal_timer", TEST_TIMER_MS, OSAL_TIMER_PERIODIC, NULL, _timer_cb) != OSAL_OK)
        return -1;
    if (osal_timer_start(g_timer, OSAL_WAIT_FOREVER) != OSAL_OK)
        return -1;

    /* 鍚姩璋冨害鍣?*/
    return osal_kernel_start();
}


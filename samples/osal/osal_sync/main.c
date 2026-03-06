#include "osal/osal.h"

/*
 * OSAL 综合测试示例
 * 1) 线程 + 队列：生产者向队列投递数据，消费者取数据
 * 2) 事件：生产者向消费者发送事件通知
 * 3) 定时器 + 信号量：定时器周期触发，唤醒监控线程
 * 4) 互斥锁：保护共享计数器
 * 5) 时间接口：记录运行时间戳
 *
 * 使用方法：在 RTOS 工程中编译该文件，并在 main 中调用 `osal_kernel_start()`
 * 注意：本示例不依赖标准输出，建议通过断点或监视变量观察结果
 */

#define TEST_QUEUE_LEN 8U
#define TEST_TIMER_MS 100U
#define TEST_PROD_PERIOD 10U

typedef struct
{
    uint32_t sequenceNumber;
    uint32_t timestampMs;
} OsalTestMessage;

static OsalQueue* g_queue;
static OsalMutex* g_mutex;
static OsalSem* g_sem;
static OsalEventFlags* g_event;
static OsalThread* g_producer_thread;
static OsalThread* g_consumer_thread;
static OsalThread* g_monitor_thread;
static OsalThread* g_counter_thread;
static OsalThread* g_edge_test_thread;
static OsalTimer* g_timer;

/* 运行状态统计，便于调试观察 */
static volatile uint32_t g_produced_cnt;
static volatile uint32_t g_consumed_cnt;
static volatile uint32_t g_monitor_cnt;
static volatile uint32_t g_shared_counter;
static volatile uint32_t g_counter_hits;
static volatile uint32_t g_edge_tests;
static volatile uint32_t g_edge_failures;
static volatile uint32_t g_edge_done;

static void osal_sync_timer_callback(OsalTimer* timer)
{
    (void)timer;
    /* 定时器回调中发送信号量，唤醒监控线程 */
    (void)osal_sem_post(g_sem);
}

static void osal_sync_producer_thread_entry(void* arg)
{
    (void)arg;
    OsalTestMessage message;
    OsalTimeMs last_wake_time_ms = osal_time_now_monotonic();

    while (1)
    {
        /* 生成数据 */
        message.sequenceNumber = g_produced_cnt++;
        message.timestampMs = (uint32_t)osal_time_now_monotonic();

        /* 保护共享计数器 */
        if (osal_mutex_lock(g_mutex, OSAL_WAIT_FOREVER) == OSAL_OK)
        {
            g_shared_counter++;
            (void)osal_mutex_unlock(g_mutex);
        }

        /* 投递到队列，若超时则放弃本次 */
        (void)osal_queue_send(g_queue, &message, 5U);

        /* 通知消费者线程（事件对象化，避免线程通知位冲突） */
        (void)osal_event_flags_set(g_event, 0x01U);

        /* 固定周期运行 */
        (void)osal_delay_until(&last_wake_time_ms, TEST_PROD_PERIOD, NULL);
    }
}

static void osal_sync_consumer_thread_entry(void* arg)
{
    (void)arg;
    OsalTestMessage message;

    while (1)
    {
        uint32_t event_flags_value = 0;
        /* 等待事件通知 */
        if (osal_event_flags_wait(g_event, 0x01U, &event_flags_value, OSAL_WAIT_FOREVER, 0U) == OSAL_OK)
        {
            /* 收到事件后尝试取队列数据 */
            if (osal_queue_recv(g_queue, &message, 10U) == OSAL_OK)
            {
                g_consumed_cnt++;
            }
        }
    }
}

static void osal_sync_monitor_thread_entry(void* arg)
{
    (void)arg;
    while (1)
    {
        /* 由定时器周期唤醒 */
        if (osal_sem_wait(g_sem, OSAL_WAIT_FOREVER) == OSAL_OK)
        {
            g_monitor_cnt++;
        }
    }
}

static void osal_sync_counter_thread_entry(void* arg)
{
    (void)arg;
    /* 高频竞争互斥锁，用于验证互斥效果 */
    while (1)
    {
        if (osal_mutex_lock(g_mutex, 1U) == OSAL_OK)
        {
            g_shared_counter++;
            g_counter_hits++;
            (void)osal_mutex_unlock(g_mutex);
        }
        /* 让出 CPU，避免独占 */
        osal_sleep_ms(1U);
    }
}

static void osal_sync_edge_test_thread_entry(void* arg)
{
    (void)arg;
    uint32_t test_count = 0;
    uint32_t failure_count = 0;

    /* 队列边界测试 */
    {
        OsalQueue* test_queue = NULL;
        uint8_t item = 0x5A;

        test_count++;
        if (osal_queue_create(NULL, 1U, 1U) != OSAL_INVALID)
            failure_count++;

        test_count++;
        if (osal_queue_create(&test_queue, 0U, 1U) != OSAL_INVALID)
            failure_count++;

        test_count++;
        if (osal_queue_create(&test_queue, 1U, 0U) != OSAL_INVALID)
            failure_count++;

        test_count++;
        if (osal_queue_create(&test_queue, 1U, 1U) != OSAL_OK)
            failure_count++;

        test_count++;
        if (osal_queue_recv(test_queue, &item, 0U) != OSAL_WOULD_BLOCK)
            failure_count++;

        test_count++;
        if (osal_queue_send(test_queue, NULL, 0U) != OSAL_INVALID)
            failure_count++;

        test_count++;
        if (osal_queue_send(test_queue, &item, 0U) != OSAL_OK)
            failure_count++;

        test_count++;
        if (osal_queue_send(test_queue, &item, 0U) != OSAL_WOULD_BLOCK)
            failure_count++;

        test_count++;
        if (osal_queue_recv(test_queue, &item, 0U) != OSAL_OK)
            failure_count++;

        test_count++;
        if (osal_queue_delete(NULL) != OSAL_INVALID)
            failure_count++;

        test_count++;
        if (osal_queue_delete(test_queue) != OSAL_OK)
            failure_count++;
    }

    /* 信号量边界测试 */
    {
        OsalSem* test_semaphore = NULL;
        uint32_t semaphore_count = 0u;

        test_count++;
        if (osal_sem_create(&test_semaphore, 1U, 2U) != OSAL_INVALID)
            failure_count++;

        test_count++;
        if (osal_sem_create(&test_semaphore, 1U, 0U) != OSAL_OK)
            failure_count++;

        test_count++;
        if (osal_sem_get_count(test_semaphore, &semaphore_count) != OSAL_OK || semaphore_count != 0u)
            failure_count++;

        test_count++;
        if (osal_sem_wait(test_semaphore, 0U) != OSAL_WOULD_BLOCK)
            failure_count++;

        test_count++;
        if (osal_sem_post(test_semaphore) != OSAL_OK)
            failure_count++;

        test_count++;
        if (osal_sem_get_count(test_semaphore, &semaphore_count) != OSAL_OK || semaphore_count != 1u)
            failure_count++;

        test_count++;
        if (osal_sem_post(test_semaphore) != OSAL_NO_RESOURCE)
            failure_count++;

        test_count++;
        if (osal_sem_wait(test_semaphore, 0U) != OSAL_OK)
            failure_count++;

        test_count++;
        if (osal_sem_get_count(test_semaphore, &semaphore_count) != OSAL_OK || semaphore_count != 0u)
            failure_count++;

        test_count++;
        if (osal_sem_delete(NULL) != OSAL_INVALID)
            failure_count++;

        test_count++;
        if (osal_sem_delete(test_semaphore) != OSAL_OK)
            failure_count++;
    }

    /* 互斥锁边界测试 */
    {
        OsalMutex* test_mutex = NULL;

        test_count++;
        if (osal_mutex_create(&test_mutex) != OSAL_OK)
            failure_count++;

        test_count++;
        if (osal_mutex_unlock(test_mutex) != OSAL_INVALID)
            failure_count++;

        test_count++;
        if (osal_mutex_lock(test_mutex, OSAL_WAIT_FOREVER) != OSAL_OK)
            failure_count++;

        test_count++;
        if (osal_mutex_lock(test_mutex, 0U) != OSAL_WOULD_BLOCK)
            failure_count++;

        test_count++;
        if (osal_mutex_unlock(test_mutex) != OSAL_OK)
            failure_count++;

        test_count++;
        if (osal_mutex_delete(NULL) != OSAL_INVALID)
            failure_count++;

        test_count++;
        if (osal_mutex_delete(test_mutex) != OSAL_OK)
            failure_count++;
    }

    /* 事件边界测试 */
    {
        uint32_t event_flags_value = 0;
        OsalEventFlags* test_event_flags = NULL;

        test_count++;
        if (osal_event_flags_create(&test_event_flags) != OSAL_OK)
            failure_count++;

        test_count++;
        if (osal_event_flags_wait(test_event_flags, 0x02U, &event_flags_value, 0U, 0U) != OSAL_WOULD_BLOCK)
            failure_count++;

        (void)osal_event_flags_delete(test_event_flags);
    }

    /* 定时器边界测试 */
    {
        OsalTimer* test_timer = NULL;

        test_count++;
        if (osal_timer_create(NULL, "bad_timer", 1U, OSAL_TIMER_PERIODIC, NULL, osal_sync_timer_callback) != OSAL_INVALID)
            failure_count++;

        test_count++;
        if (osal_timer_create(&test_timer, "bad_timer", 0U, OSAL_TIMER_PERIODIC, NULL, osal_sync_timer_callback) != OSAL_INVALID)
            failure_count++;

        test_count++;
        if (osal_timer_create(&test_timer, "bad_timer2", 1U, OSAL_TIMER_PERIODIC, NULL, NULL) != OSAL_INVALID)
            failure_count++;
    }

    /* 时间接口边界测试 */
    {
        OsalTimeMs first_time_ms = osal_time_now_monotonic();
        OsalTimeMs second_time_ms = osal_time_now_monotonic();
        test_count++;
        if (osal_time_before(second_time_ms, first_time_ms))
            failure_count++;
    }

    g_edge_tests = test_count;
    g_edge_failures = failure_count;
    g_edge_done = 1U;

    while (1)
    {
        osal_sleep_ms(1000U);
    }
}

int main(void)
{
    /* 创建事件对象 */
    if (osal_event_flags_create(&g_event) != OSAL_OK)
        return -1;

    /* 创建队列 */
    if (osal_queue_create(&g_queue, TEST_QUEUE_LEN, sizeof(OsalTestMessage)) != OSAL_OK)
        return -1;

    /* 创建互斥锁 */
    if (osal_mutex_create(&g_mutex) != OSAL_OK)
        return -1;

    /* 创建信号量 */
    if (osal_sem_create(&g_sem, 10U, 0U) != OSAL_OK)
        return -1;

    /* 创建线程 */
    OsalThreadAttr producer_thread_attr = {"osal_prod", 512U * OSAL_STACK_WORD_BYTES, 2U};
    OsalThreadAttr consumer_thread_attr = {"osal_cons", 512U * OSAL_STACK_WORD_BYTES, 2U};
    OsalThreadAttr monitor_thread_attr = {"osal_mon", 512U * OSAL_STACK_WORD_BYTES, 1U};
    OsalThreadAttr counter_thread_attr = {"osal_cnt", 512U * OSAL_STACK_WORD_BYTES, 1U};
    OsalThreadAttr edge_test_thread_attr = {"osal_edge", 768U * OSAL_STACK_WORD_BYTES, 2U};

    if (osal_thread_create(&g_producer_thread, &producer_thread_attr, osal_sync_producer_thread_entry, NULL) != OSAL_OK)
        return -1;
    if (osal_thread_create(&g_consumer_thread, &consumer_thread_attr, osal_sync_consumer_thread_entry, NULL) != OSAL_OK)
        return -1;
    if (osal_thread_create(&g_monitor_thread, &monitor_thread_attr, osal_sync_monitor_thread_entry, NULL) != OSAL_OK)
        return -1;
    if (osal_thread_create(&g_counter_thread, &counter_thread_attr, osal_sync_counter_thread_entry, NULL) != OSAL_OK)
        return -1;
    if (osal_thread_create(&g_edge_test_thread, &edge_test_thread_attr, osal_sync_edge_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    /* 创建并启动定时器 */
    if (osal_timer_create(&g_timer, "osal_timer", TEST_TIMER_MS, OSAL_TIMER_PERIODIC, NULL, osal_sync_timer_callback) != OSAL_OK)
        return -1;
    if (osal_timer_start(g_timer, OSAL_WAIT_FOREVER) != OSAL_OK)
        return -1;

    /* 启动调度 */
    return osal_kernel_start();
}

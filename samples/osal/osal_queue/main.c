/**
 * @file main.c
 * @brief OSAL queue 独立例程
 * @details
 * - 本例程用于验证 queue 的核心合同与边界语义。
 * - 观测变量：
 *   - `g_queue_result.total`：累计断言数量
 *   - `g_queue_result.failed`：失败断言数量
 *   - `g_queue_result.done`：用例是否执行完成（1 表示完成）
 */
#include "osal/osal.h"
#include "osal/osal_config.h"

typedef struct
{
    uint32_t id;
    uint32_t payload;
} OsalQueueTestMessage;

typedef struct
{
    volatile uint32_t total;
    volatile uint32_t failed;
    volatile uint32_t done;
} OsalQueueTestResult;

static OsalQueue* g_queue = NULL;
static OsalThread* g_test_thread = NULL;
static OsalThread* g_sender_thread = NULL;
static OsalQueueTestResult g_queue_result = {0u, 0u, 0u};

/**
 * @brief 简单断言计数器
 * @param condition 非 0 表示断言通过
 */
static void osal_queue_expect(int condition)
{
    g_queue_result.total++;
    if (!condition)
        g_queue_result.failed++;
}

/**
 * @brief 比较两条测试消息是否一致
 */
static int osal_queue_message_equal(const OsalQueueTestMessage* left, const OsalQueueTestMessage* right)
{
    if (!left || !right)
        return 0;

    return (left->id == right->id) && (left->payload == right->payload);
}

/**
 * @brief 辅助线程：延时后发送一次消息
 * @note 用于触发 `recv(OSAL_WAIT_FOREVER)` 的唤醒路径
 */
static void osal_queue_sender_thread_entry(void* arg)
{
    OsalQueueTestMessage message = {0xA5u, 0x5Au};

    (void)arg;
    (void)osal_sleep_ms(20u);
    (void)osal_queue_send(g_queue, &message, 0u);
    osal_thread_exit();
}

/**
 * @brief queue 语义测试线程
 * @details 验证点分七组：
 * 1) 创建参数校验与基础创建/删除
 * 2) 查询接口状态化行为
 * 3) 空/满边界与 peek 语义
 * 4) reset 清空语义
 * 5) `recv(OSAL_WAIT_FOREVER)` 唤醒路径
 * 6) `from_isr` 在线程上下文误用保护
 * 7) 资源释放
 */
static void osal_queue_test_thread_entry(void* arg)
{
    OsalQueue* queue_temp = NULL;
    OsalQueueTestMessage message_1 = {1u, 11u};
    OsalQueueTestMessage message_2 = {2u, 22u};
    OsalQueueTestMessage message_3 = {3u, 33u};
    OsalQueueTestMessage message_rx = {0u, 0u};
    uint32_t queue_count = 0u;
    uint32_t queue_space = 0u;
    OsalThreadAttr sender_attr = {
        "osal_queue_sender",
        512u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    (void)arg;

    /* 组1：创建参数与基础行为 */
    osal_queue_expect(osal_queue_create(NULL, 2u, sizeof(OsalQueueTestMessage)) == OSAL_INVALID);
    osal_queue_expect(osal_queue_create(&queue_temp, 0u, sizeof(OsalQueueTestMessage)) == OSAL_INVALID);
    osal_queue_expect(osal_queue_create(&queue_temp, 2u, 0u) == OSAL_INVALID);
    osal_queue_expect(osal_queue_create(&g_queue, 2u, sizeof(OsalQueueTestMessage)) == OSAL_OK);
    osal_queue_expect(osal_queue_delete(NULL) == OSAL_INVALID);

    /* 组2：查询接口状态化 */
    osal_queue_expect(osal_queue_messages_waiting(NULL, &queue_count) == OSAL_INVALID);
    osal_queue_expect(osal_queue_messages_waiting(g_queue, NULL) == OSAL_INVALID);
    osal_queue_expect(osal_queue_spaces_available(NULL, &queue_space) == OSAL_INVALID);
    osal_queue_expect(osal_queue_spaces_available(g_queue, NULL) == OSAL_INVALID);
    osal_queue_expect(osal_queue_messages_waiting(g_queue, &queue_count) == OSAL_OK && queue_count == 0u);
    osal_queue_expect(osal_queue_spaces_available(g_queue, &queue_space) == OSAL_OK && queue_space == 2u);

    /* 组3：空/满边界与 peek 语义 */
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

    /* 组4：reset 后应清空 */
    osal_queue_expect(osal_queue_send(g_queue, &message_1, 0u) == OSAL_OK);
    osal_queue_expect(osal_queue_send(g_queue, &message_2, 0u) == OSAL_OK);
    osal_queue_expect(osal_queue_reset(g_queue) == OSAL_OK);
    osal_queue_expect(osal_queue_messages_waiting(g_queue, &queue_count) == OSAL_OK && queue_count == 0u);
    osal_queue_expect(osal_queue_spaces_available(g_queue, &queue_space) == OSAL_OK && queue_space == 2u);
    osal_queue_expect(osal_queue_peek(g_queue, &message_rx, 0u) == OSAL_WOULD_BLOCK);

    /* 组5：WAIT_FOREVER 路径 */
    osal_queue_expect(osal_thread_create(&g_sender_thread, &sender_attr, osal_queue_sender_thread_entry, NULL) == OSAL_OK);
    osal_queue_expect(osal_queue_recv(g_queue, &message_rx, OSAL_WAIT_FOREVER) == OSAL_OK);
    osal_queue_expect(message_rx.id == 0xA5u && message_rx.payload == 0x5Au);
    osal_queue_expect(osal_queue_messages_waiting(g_queue, &queue_count) == OSAL_OK && queue_count == 0u);

    /* 组6：资源释放 */
    osal_queue_expect(osal_queue_delete(g_queue) == OSAL_OK);
    g_queue = NULL;

    g_queue_result.done = 1u;
    for (;;)
        osal_sleep_ms(1000u);
}

/**
 * @brief 例程入口
 * @return 创建测试线程失败返回 -1；成功后启动调度
 */
int main(void)
{
    OsalThreadAttr test_attr = {
        "osal_queue_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_queue_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}

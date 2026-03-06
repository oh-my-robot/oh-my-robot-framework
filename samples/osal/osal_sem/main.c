/**
 * @file main.c
 * @brief OSAL semaphore 独立例程
 * @details
 * - 本例程用于验证 semaphore 的核心合同，不依赖其他同步原语语义。
 * - 观测变量：
 *   - `g_sem_result.total`：累计断言数量
 *   - `g_sem_result.failed`：失败断言数量
 *   - `g_sem_result.done`：用例是否执行完成（1 表示完成）
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
 * @brief 简单断言计数器
 * @param condition 非 0 表示断言通过
 */
static void osal_sem_expect(int condition)
{
    g_sem_result.total++;
    if (!condition)
        g_sem_result.failed++;
}

/**
 * @brief 辅助线程：延时后 post 一次
 * @note 用于触发 `wait(OSAL_WAIT_FOREVER)` 的唤醒路径
 */
static void osal_sem_post_once_thread(void* arg)
{
    (void)arg;
    (void)osal_sleep_ms(20u);
    (void)osal_sem_post(g_sem);
    osal_thread_exit();
}

/**
 * @brief semaphore 语义测试线程
 * @details 验证点分四组：
 * 1) 参数合法性（`init_count > max_count`）
 * 2) 计数行为（get_count、would-block、post 累加）
 * 3) 满计数行为（post 返回 `OSAL_NO_RESOURCE`）
 * 4) `OSAL_WAIT_FOREVER` 语义（由辅助线程 post 唤醒）
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

    /* 组1：参数合法性 */
    osal_sem_expect(osal_sem_create(&invalid_sem, 1u, 2u) == OSAL_INVALID);

    /* 组2：基础计数行为 */
    osal_sem_expect(osal_sem_create(&g_sem, 2u, 0u) == OSAL_OK);
    osal_sem_expect(osal_sem_get_count(g_sem, &sem_count) == OSAL_OK && sem_count == 0u);
    osal_sem_expect(osal_sem_wait(g_sem, 0u) == OSAL_WOULD_BLOCK);

    osal_sem_expect(osal_sem_post(g_sem) == OSAL_OK);
    osal_sem_expect(osal_sem_post(g_sem) == OSAL_OK);
    osal_sem_expect(osal_sem_get_count(g_sem, &sem_count) == OSAL_OK && sem_count == 2u);

    /* 组3：满计数时 post 不阻塞，返回 NO_RESOURCE */
    osal_sem_expect(osal_sem_post(g_sem) == OSAL_NO_RESOURCE);

    osal_sem_expect(osal_sem_wait(g_sem, 0u) == OSAL_OK);
    osal_sem_expect(osal_sem_wait(g_sem, 0u) == OSAL_OK);
    osal_sem_expect(osal_sem_wait(g_sem, 0u) == OSAL_WOULD_BLOCK);

    /* 组4：WAIT_FOREVER 由辅助线程 post 唤醒 */
    osal_sem_expect(osal_thread_create(&g_post_thread, &post_attr, osal_sem_post_once_thread, NULL) == OSAL_OK);
    osal_sem_expect(osal_sem_wait(g_sem, OSAL_WAIT_FOREVER) == OSAL_OK);
    osal_sem_expect(osal_sem_get_count(g_sem, &sem_count) == OSAL_OK && sem_count == 0u);

    /* 收尾：释放资源 */
    osal_sem_expect(osal_sem_delete(g_sem) == OSAL_OK);
    g_sem = NULL;

    g_sem_result.done = 1u;
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
        "osal_sem_test",
        768u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    if (osal_thread_create(&g_test_thread, &test_attr, osal_sem_test_thread_entry, NULL) != OSAL_OK)
        return -1;

    return osal_kernel_start();
}

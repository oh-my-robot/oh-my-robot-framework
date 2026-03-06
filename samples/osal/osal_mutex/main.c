/**
 * @file main.c
 * @brief OSAL mutex 独立例程
 * @details
 * - 本例程用于验证 mutex 的所有权与非递归语义合同。
 * - 观测变量：
 *   - `g_mutex_result.total`：累计断言数量
 *   - `g_mutex_result.failed`：失败断言数量
 *   - `g_mutex_result.done`：用例是否执行完成（1 表示完成）
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
 * @brief 简单断言计数器
 * @param condition 非 0 表示断言通过
 */
static void osal_mutex_expect(int condition)
{
    g_mutex_result.total++;
    if (!condition)
        g_mutex_result.failed++;
}

/**
 * @brief owner 线程
 * @details
 * - 持有 mutex 一段时间，制造跨线程竞争窗口。
 * - 通过两个 semaphore 与测试线程同步：
 *   - `g_owner_ready_sem`：已持锁
 *   - `g_owner_done_sem`：已释放并结束
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
 * @brief mutex 语义测试线程
 * @details 验证点分五组：
 * 1) 基础创建与同步资源创建
 * 2) 非 owner 解锁应返回 `OSAL_INVALID`
 * 3) 非递归行为：同线程二次 lock(0) 返回 `OSAL_WOULD_BLOCK`
 * 4) 跨线程持锁期间：非 owner 解锁与非阻塞 lock 行为
 * 5) owner 释放后可再次 lock/unlock 成功
 */
static void osal_mutex_test_thread_entry(void* arg)
{
    OsalThreadAttr owner_attr = {
        "osal_mutex_owner",
        512u * OSAL_STACK_WORD_BYTES,
        2u,
    };

    (void)arg;

    /* 组1：基础创建 */
    osal_mutex_expect(osal_mutex_create(&g_mutex) == OSAL_OK);
    osal_mutex_expect(osal_sem_create(&g_owner_ready_sem, 1u, 0u) == OSAL_OK);
    osal_mutex_expect(osal_sem_create(&g_owner_done_sem, 1u, 0u) == OSAL_OK);

    /* 组2：未持有即解锁，必须返回 INVALID */
    osal_mutex_expect(osal_mutex_unlock(g_mutex) == OSAL_INVALID);

    /* 组3：非递归语义验证 */
    osal_mutex_expect(osal_mutex_lock(g_mutex, OSAL_WAIT_FOREVER) == OSAL_OK);
    osal_mutex_expect(osal_mutex_lock(g_mutex, 0u) == OSAL_WOULD_BLOCK);
    osal_mutex_expect(osal_mutex_unlock(g_mutex) == OSAL_OK);

    /* 组4：跨线程 owner 持锁窗口 */
    osal_mutex_expect(osal_thread_create(&g_owner_thread, &owner_attr, osal_mutex_owner_thread_entry, NULL) == OSAL_OK);
    osal_mutex_expect(osal_sem_wait(g_owner_ready_sem, OSAL_WAIT_FOREVER) == OSAL_OK);

    osal_mutex_expect(osal_mutex_unlock(g_mutex) == OSAL_INVALID);
    osal_mutex_expect(osal_mutex_lock(g_mutex, 0u) == OSAL_WOULD_BLOCK);

    /* 组5：owner 释放后恢复可用 */
    osal_mutex_expect(osal_sem_wait(g_owner_done_sem, OSAL_WAIT_FOREVER) == OSAL_OK);
    osal_mutex_expect(osal_mutex_lock(g_mutex, 0u) == OSAL_OK);
    osal_mutex_expect(osal_mutex_unlock(g_mutex) == OSAL_OK);

    /* 收尾：释放资源 */
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
 * @brief 例程入口
 * @return 创建测试线程失败返回 -1；成功后启动调度
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

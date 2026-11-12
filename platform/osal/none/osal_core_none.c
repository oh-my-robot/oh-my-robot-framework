/**
 * @file   osal_core_none.c
 * @brief  osal-none 端口核心：中断面 + 内存面 + 忙等轮询核心
 *
 * - 中断面：is_in_isr 与临界区收敛到 arch 钩子（host 模拟 ISR/临界区对象，
 *   target = PRIMASK/IPSR）。单执行流无任务↔任务并发，临界区只服务任务↔ISR。
 * - 内存面：heap_1 形态静态池（只分配不释放，free 无操作）——单程生命周期
 *   语义；0 配置 = 禁用堆。
 * - 忙等核心：osal_none_poll_wait 是 sem/mutex/queue/event 等待的统一实现，
 *   等待期间内嵌刷新到期软件定时器（配合 osal_timer_none.c）。
 */

#include "osal/osal_core.h"

#include "osal_none_internal.h"

/*===========================================================================
 * 中断面
 *===========================================================================*/

int osal_is_in_isr(void)
{
    return osal_none_arch_in_isr();
}

void osal_irq_lock_task(void)
{
    if (osal_is_in_isr())
    {
        return; /* 误用：ISR 中调任务临界区——静默拒绝（见 internal.h 纪律） */
    }
    osal_none_arch_crit_task_enter();
}

void osal_irq_unlock_task(void)
{
    if (osal_is_in_isr())
    {
        return;
    }
    osal_none_arch_crit_task_exit();
}

OsalIrqIsrState osal_irq_lock_from_isr(void)
{
    if (!osal_is_in_isr())
    {
        return (OsalIrqIsrState)0u; /* 误用：任务上下文调 ISR 临界区 */
    }
    osal_none_arch_crit_isr_enter();
    return (OsalIrqIsrState)1u; /* 状态标记：已进入（host 无掩码态可保存） */
}

void osal_irq_unlock_from_isr(OsalIrqIsrState state)
{
    if (!osal_is_in_isr())
    {
        return;
    }
    if (state != (OsalIrqIsrState)0u)
    {
        osal_none_arch_crit_isr_exit();
    }
}

/*===========================================================================
 * 内存面（heap_1 形态静态池）
 *===========================================================================*/

#if (OM_OSAL_NONE_HEAP_SIZE > 0u)
static union
{
    max_align_t align; /* 池基按最大自然对齐 */
    unsigned char raw[OM_OSAL_NONE_HEAP_SIZE];
} s_heap;
static size_t s_used; /* 已分配字节（只增不减） */
#endif

void *osal_malloc(size_t size)
{
    size_t want;

    if (osal_is_in_isr())
    {
        return NULL; /* 契约：ISR 禁分配（静默 NULL，同 internal.h 纪律） */
    }

#if (OM_OSAL_NONE_HEAP_SIZE == 0u)
    (void)size;
    return NULL; /* 堆禁用形态：纯静态对象 */
#else
    if (size == 0u)
    {
        return NULL;
    }
    if (size > (size_t)-1 - (OSAL_NONE_ALIGN - 1u))
    {
        return NULL; /* 溢出 */
    }
    want = (size + (OSAL_NONE_ALIGN - 1u)) & ~((size_t)OSAL_NONE_ALIGN - 1u);
    if (s_used > sizeof(s_heap.raw) || want > sizeof(s_heap.raw) - s_used)
    {
        return NULL; /* 耗尽（heap_1 不回收） */
    }
    void *p = s_heap.raw + s_used;
    s_used += want;
    return p;
#endif
}

void osal_free(void *ptr)
{
    /* 只分配不释放：单程生命周期语义（同 heap_1 design-by-contract）。
     * 重复 free / free 后再分配不产生副作用。 */
    (void)ptr;
}

/*===========================================================================
 * 忙等轮询核心（A1：忙等 + 内嵌刷新）
 *===========================================================================*/

OsalStatus osal_none_check_blocking_ctx(void)
{
    if (osal_is_in_isr())
    {
        return OSAL_INVALID;
    }
    if (osal_none_timer_cb_active())
    {
        return OSAL_INVALID; /* 定时器回调内禁止阻塞（契约：回调非阻塞） */
    }
    return OSAL_OK;
}

OsalStatus osal_none_poll_wait(OsalNonePollCond cond, void *arg, uint32_t timeout_ms)
{
    OsalTimeMs deadline = 0u;
    uint32_t wait_ms = timeout_ms;

    if (!cond)
    {
        return OSAL_INVALID;
    }

    if (wait_ms == OSAL_WAIT_FOREVER)
    {
        wait_ms = OSAL_WAIT_FOREVER; /* 死等：不设截止 */
    }
    else
    {
        /* 有限超时按 2^31 ms 饱和：osal 时间合同比较窗口上限（约 24.8 天） */
        if (wait_ms > 0x7FFFFFFFu)
        {
            wait_ms = 0x7FFFFFFFu;
        }
        if (wait_ms != 0u)
        {
            deadline = osal_none_time_now_ms() + wait_ms;
        }
    }

    for (;;)
    {
        if (cond(arg))
        {
            return OSAL_OK;
        }
        if (wait_ms == 0u)
        {
            return OSAL_WOULD_BLOCK;
        }
        if (wait_ms != OSAL_WAIT_FOREVER && osal_time_after(osal_none_time_now_ms(), deadline))
        {
            return OSAL_TIMEOUT;
        }
        osal_none_timer_refresh();
        osal_none_arch_wait_pause();
    }
}

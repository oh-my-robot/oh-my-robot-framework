/**
 * @file   osal_none_arch_x64.c
 * @brief  osal-none 端口 x64 host（Windows/mingw）arch 实现
 *
 * host 环境无真实中断——以"真实模拟裸机行为"为准则：
 * - 临界区 = 临界区对象（模拟关中断互斥：任务侧与模拟 ISR 侧共用，
 *   使 ISR 模拟辅助线程与主执行流的对象访问互斥，可暴露竞态）；
 * - ISR 上下文 = 可注入标志（isr_enter/exit 测试钩子，仅 host 提供）；
 * - 时间 = QPC 自由运行计数换算毫秒（随 CPU 时钟走，冻结即暂停——
 *   与目标 CPU 时钟域计数器同构）；
 * - 忙等让步 = Sleep(0)（语义不变，避免 host 测试空转整核）。
 *
 * 临界区假定：任何并发使用前已由主执行流完成首次初始化（测试结构保证）。
 */

#include <windows.h>

#include "osal_none_internal.h"

static CRITICAL_SECTION s_crit;
static bool s_crit_ready;
static volatile LONG s_sim_isr; /* >0 = 模拟中断上下文中 */

static void osal_none_crit_ensure(void)
{
    if (!s_crit_ready)
    {
        InitializeCriticalSection(&s_crit);
        s_crit_ready = true;
    }
}

int osal_none_arch_in_isr(void)
{
    return (s_sim_isr > 0L) ? 1 : 0;
}

void osal_none_arch_sim_isr_enter(void)
{
    InterlockedIncrement(&s_sim_isr);
}

void osal_none_arch_sim_isr_exit(void)
{
    InterlockedDecrement(&s_sim_isr);
}

void osal_none_arch_crit_task_enter(void)
{
    osal_none_crit_ensure();
    EnterCriticalSection(&s_crit);
}

void osal_none_arch_crit_task_exit(void)
{
    LeaveCriticalSection(&s_crit);
}

void osal_none_arch_crit_isr_enter(void)
{
    osal_none_crit_ensure();
    EnterCriticalSection(&s_crit);
}

void osal_none_arch_crit_isr_exit(void)
{
    LeaveCriticalSection(&s_crit);
}

void osal_none_arch_wait_pause(void)
{
    Sleep(0); /* 让出时间片给同优先级就绪线程（含模拟 ISR 辅助线程） */
}

OsalTimeMs osal_none_time_now_ms(void)
{
    static uint64_t s_freq;
    LARGE_INTEGER counter;
    uint64_t now_ms;

    if (s_freq == 0u)
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        s_freq = (uint64_t)freq.QuadPart;
    }
    QueryPerformanceCounter(&counter);
    now_ms = ((uint64_t)counter.QuadPart * 1000u) / s_freq;
    return (OsalTimeMs)now_ms; /* 32 位截断 = 自然回绕（时间合同语义） */
}

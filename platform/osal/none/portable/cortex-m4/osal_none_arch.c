/**
 * @file   osal_none_arch.c
 * @brief  osal-none 端口目标实现（ARMv7-M / Cortex-M4）
 *
 * 本文件 = 该 arch 的完整移植面（钩子契约见 osal_none_internal.h）：
 * - 时基 = SysTick 自由运行计数（CPU 时钟域；无中断依赖、中断全关可计时；
 *   **不依赖调试域**——DWT CYCCNT 无调试器连接时不计数，bootloader 语义
 *   不得依赖调试器）。24 位向下计数：相位模差累加（不丢拍）+ 余数进位
 *   得毫秒，零除法零回绕处理；
 * - SysTick **懒启动**（RVR==0 时按 SystemCoreClock 配置，纯计数不使能
 *   TICKINT——不劫持异常；需要 tick 中断的工程自行置 TICKINT 位）；
 *   SystemCoreClock 须由板级时钟初始化先行给出；
 * - 临界区 = PRIMASK 屏蔽 + 嵌套深度（单执行流单一嵌套主；ISR 抢占窗口
 *   由"先查深度再屏蔽"的天然时序保证互斥）；
 * - ISR 判断 = IPSR 读。
 * 寄存器直映射（ARMv7-M 架构规范），纯 C + 通用内联汇编，零 vendor 头依赖
 * （gnu-rm / armclang / tiarmclang 通用）。
 */

#include "osal_none_internal.h"

#define SYST_CSR_REG (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR_REG (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR_REG (*(volatile uint32_t *)0xE000E018u)

#define SYST_CSR_ENABLE    (1u << 0)
#define SYST_CSR_CLKSOURCE (1u << 2)

#define SYST_COUNT_MASK 0x00FFFFFFu

extern uint32_t SystemCoreClock;

static volatile uint32_t s_crit_depth; /* PRIMASK 嵌套深度（任务/ISR 共享，单核互斥） */
static volatile uint32_t s_crit_key;

static uint32_t s_time_phase_last; /* 上一采样相位（周期内单调递增位置） */
static uint32_t s_time_rem;        /* 未满 1ms 的周期余数 */
static uint32_t s_time_ms;         /* 毫秒累计（以 wrap 与余数进位推得） */

static uint32_t osal_none_primask_get(void)
{
    uint32_t r;

    __asm volatile("mrs %0, primask" : "=r"(r));
    return r;
}

static void osal_none_primask_set(uint32_t value)
{
    __asm volatile("msr primask, %0" : : "r"(value));
}

int osal_none_arch_in_isr(void)
{
    uint32_t ipsr;

    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return (ipsr != 0u) ? 1 : 0;
}

static void osal_none_crit_enter(void)
{
    if (s_crit_depth == 0u)
    {
        s_crit_key = osal_none_primask_get();
        osal_none_primask_set(1u);
    }
    s_crit_depth++;
}

static void osal_none_crit_exit(void)
{
    if (s_crit_depth > 0u)
    {
        s_crit_depth--;
    }
    if (s_crit_depth == 0u)
    {
        osal_none_primask_set(s_crit_key);
    }
}

void osal_none_arch_crit_task_enter(void)
{
    osal_none_crit_enter();
}

void osal_none_arch_crit_task_exit(void)
{
    osal_none_crit_exit();
}

void osal_none_arch_crit_isr_enter(void)
{
    osal_none_crit_enter();
}

void osal_none_arch_crit_isr_exit(void)
{
    osal_none_crit_exit();
}

void osal_none_arch_wait_pause(void)
{
    /* 真自旋：目标形态无让步点 */
}

OsalTimeMs osal_none_time_now_ms(void)
{
    static uint32_t s_cycles_per_ms; /* SystemCoreClock 缓存（板级时钟后固定） */
    uint32_t rvr;
    uint32_t phase;
    uint32_t delta;

    rvr = SYST_RVR_REG;
    if (rvr == 0u)
    {
        /* SysTick 懒启动（纯计数；TICKINT 保持调用方设置） */
        if (SystemCoreClock == 0u)
        {
            return (OsalTimeMs)s_time_ms; /* 时钟未初始化（板级 init 前置契约） */
        }
        SYST_RVR_REG = (SystemCoreClock / 1000u) - 1u;
        SYST_CVR_REG = 0u;
        SYST_CSR_REG |= (SYST_CSR_ENABLE | SYST_CSR_CLKSOURCE);
        rvr = SYST_RVR_REG;
    }
    if (s_cycles_per_ms == 0u)
    {
        s_cycles_per_ms = SystemCoreClock / 1000u;
    }

    phase = (rvr - SYST_CVR_REG) & SYST_COUNT_MASK;        /* 周期内单调递增相位 */
    delta = (phase - s_time_phase_last) & SYST_COUNT_MASK; /* 模差：跨重装点无损 */
    s_time_phase_last = phase;

    s_time_rem += delta;
    while (s_time_rem >= s_cycles_per_ms)
    {
        s_time_rem -= s_cycles_per_ms;
        s_time_ms++;
    }
    return (OsalTimeMs)s_time_ms;
}

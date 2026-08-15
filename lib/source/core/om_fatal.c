/**
 * @file  om_fatal.c
 * @brief 致命错误设施实现（kernel-core，OS 无关，经 tar_awcore 编译）
 * @details
 * - om_fatal_error()：唯一入口，语义=记录(预留)→调 handler→禁中断 halt，永不返回；
 * - 可重入保护：fatal 中再触发（如 handler 内再次 fatal / ISR 中断路径）直接 halt；
 * - 禁中断经 om_interrupt.h 的 om_hw_disable_interrupt_force()（port 层 port_int_disable，
 *   "错误处理等极端场景"），与 bsp_cpu.c 既有用法一致——fatal 后中断必须静默；
 * - 不依赖调度器/malloc/日志：调度器前与 ISR 上下文均可调；"记录"在日志设施就绪前为空操作。
 */
#include "core/om_fatal.h"
#include "core/om_interrupt.h"

/** @brief 重入保护标志：fatal 进行中再触发直接 halt（不重复调 handler） */
static volatile OmBool g_fatal_entered = 0;

OM_WEAK void om_fatal_handler(OmFatalReason reason, OmRet cause)
{
    (void)reason;
    (void)cause;
    /* 默认空实现：返回后由 om_fatal_error() 入口禁中断 halt 兜底 */
}

void om_fatal_error(OmFatalReason reason, OmRet cause)
{
    if (g_fatal_entered)
    {
        /* 重入：不再调 handler，立即禁中断 halt */
        om_hw_disable_interrupt_force();
        while (1)
        {
        }
    }
    g_fatal_entered = 1;

    om_fatal_handler(reason, cause);

    /* handler 返回 → 禁中断 halt 兜底（fatal 永不返回） */
    om_hw_disable_interrupt_force();
    while (1)
    {
    }
}

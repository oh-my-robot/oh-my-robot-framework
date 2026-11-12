/**
 * @file   osal_none_internal.h
 * @brief  osal-none 端口内部共享声明（族文件 + arch 文件间的私有契约）
 *
 * 本头不进公共 include 路径（仅端口源与 host 测试包含）。
 *
 * 端口模型（单执行流）：
 * - 无任务调度、无上下文切换：thread 面为直调占位（见 osal_thread_none.c）；
 * - 等待 = 忙等轮询（条件 + 截止时刻 + 内嵌软件定时器刷新）；
 * - 时间 = arch 注入的自由运行计数毫秒（本端口不拥有时钟硬件）；
 * - 临界区 = 任务↔ISR 共享面互斥（host 用临界区对象模拟关中断）。
 *
 * 误用纪律：阻塞类调用在 ISR/定时器回调中一律返回 OSAL_INVALID，
 * 不依赖断言（host 测试需覆盖误用路径，断言死循环会挂死测试）。
 */

#ifndef OM_OSAL_NONE_INTERNAL_H
#define OM_OSAL_NONE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "osal/osal_core.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h"

#include "osal_none_cfg.h"

/* 块/对象对齐（8 字节；本形态不分配任务栈，不承担 ABI 栈对齐 16） */
#define OSAL_NONE_ALIGN 8u

/*===========================================================================
 * arch 钩子（每环境一个文件实现；host = osal_none_arch_x64.c）
 *===========================================================================*/

/* 单调毫秒（自由运行计数换算；时间随 CPU 时钟走，冻结即暂停） */
OsalTimeMs osal_none_time_now_ms(void);

/* 忙等循环让步点（host = Sleep(0) 让出时间片；target = 空操作） */
void osal_none_arch_wait_pause(void);

/* 当前是否处于中断上下文（host = 模拟 ISR 标志；target = IPSR 读） */
int osal_none_arch_in_isr(void);

/* 临界区：任务上下文关中断（host = 临界区对象；target = PRIMASK 屏蔽） */
void osal_none_arch_crit_task_enter(void);
void osal_none_arch_crit_task_exit(void);

/* 临界区：ISR 上下文（屏蔽态保存/恢复由 osal_irq_lock_from_isr 层负责；
 * arch 层只需进入/退出互斥。host 与任务面共用同一临界区对象） */
void osal_none_arch_crit_isr_enter(void);
void osal_none_arch_crit_isr_exit(void);

/* host 模拟 ISR 测试钩子（成对调用；期间 osal_is_in_isr() 为真）。
 * 仅 host arch 实现；target 不提供、不得调用。 */
void osal_none_arch_sim_isr_enter(void);
void osal_none_arch_sim_isr_exit(void);

/*===========================================================================
 * 忙等轮询（单流等待核心）
 *===========================================================================*/

/* 条件函数：临界区内完成"检查 + 取用"，返回 true = 条件满足且已取用 */
typedef bool (*OsalNonePollCond)(void *arg);

/* 忙等轮询：cond 满足 → OSAL_OK；timeout==0 → OSAL_WOULD_BLOCK；
 * timeout>0 截止到期 → OSAL_TIMEOUT；OSAL_WAIT_FOREVER 死等（仅条件可解除）。
 * 等待期间内嵌刷新到期软件定时器（osal_none_timer_refresh）。
 * 单次等待超过 2^31 ms 按 2^31 饱和（osal 时间合同比较窗口上限）。 */
OsalStatus osal_none_poll_wait(OsalNonePollCond cond, void *arg, uint32_t timeout_ms);

/*===========================================================================
 * 软件定时器（由等待/sleep 循环内嵌刷新；回调不得阻塞）
 *===========================================================================*/

void osal_none_timer_refresh(void);

/* 定时器回调执行中（阻塞类调用经 osal_none_check_blocking_ctx 拒绝） */
bool osal_none_timer_cb_active(void);

/*===========================================================================
 * 线程占位（唯一执行体身份）
 *===========================================================================*/

/* 唯一执行体单例句柄（osal_thread_self / mutex owner 身份） */
OsalThread *osal_none_executor_handle(void);

/*===========================================================================
 * 上下文门禁
 *===========================================================================*/

/* 阻塞类调用（wait/sleep/lock）入口检查：ISR 或定时器回调中 → OSAL_INVALID */
OsalStatus osal_none_check_blocking_ctx(void);

#endif /* OM_OSAL_NONE_INTERNAL_H */

/**
 * @file   osal_core.h
 * @brief  OSAL host 裁剪头（flash_dev_test 用）
 *
 * 本目录为 samples/host 本地覆盖目录（仿 workqueue/osal 惯例）：
 * 只承载 flash 链路所需的 osal 声明与常量，规避 lib/osal/include 对
 * om_osal_portdef.h（FreeRTOS 端口）的强依赖。实现见 ../host_osal.c。
 */

#ifndef OM_OSAL_CORE_H
#define OM_OSAL_CORE_H

#include <stdint.h>

typedef int32_t OsalStatus;

#define OSAL_OK            (0)  /* 成功 */
#define OSAL_TIMEOUT       (-1) /* 超时（timeout > 0 的 WAIT_FOREVER 语义超时）*/
#define OSAL_WOULD_BLOCK   (-2) /* 非阻塞调用（timeout=0）条件不满足 */
#define OSAL_NO_RESOURCE   (-3) /* 资源不足（内存/句柄/队列槽位）*/
#define OSAL_INVALID       (-4) /* 参数/句柄/上下文非法*/
#define OSAL_NOT_SUPPORTED (-5) /* 端口不支持*/
#define OSAL_INTERNAL      (-6) /* 内部错误 */

#define OSAL_WAIT_FOREVER  ((uint32_t)0xFFFFFFFFU)

typedef uint32_t OsalIrqIsrState;

int osal_is_in_isr(void);
void osal_irq_lock_task(void);
void osal_irq_unlock_task(void);
OsalIrqIsrState osal_irq_lock_from_isr(void);
void osal_irq_unlock_from_isr(OsalIrqIsrState state);

void osal_sleep_ms(uint32_t ms);

/* 上下文自动分派锁（host 无 ISR 语境：恒走 task 路径） */
static inline void osal_irq_lock(OsalIrqIsrState *key)
{
    osal_irq_lock_task();
    *key = 0U;
}

static inline void osal_irq_unlock(OsalIrqIsrState key)
{
    (void)key;
    osal_irq_unlock_task();
}

#ifndef OSAL_ASSERT
#include <assert.h>
#define OSAL_ASSERT(expr) assert(expr)
#endif

/* 优先级语义带（与 lib/osal/include/osal/osal_priority.h 一致） */
#define OSAL_PRIO_BAND_WIDTH        4u
#define OSAL_PRIO_IDLE_BASE         0u
#define OSAL_PRIO_LOW_BASE          (OSAL_PRIO_IDLE_BASE + OSAL_PRIO_BAND_WIDTH)
#define OSAL_PRIO_NORMAL_BASE       (OSAL_PRIO_LOW_BASE + OSAL_PRIO_BAND_WIDTH)
#define OSAL_PRIO_ABOVE_NORMAL_BASE (OSAL_PRIO_NORMAL_BASE + OSAL_PRIO_BAND_WIDTH)
#define OSAL_PRIO_HIGH_BASE         (OSAL_PRIO_ABOVE_NORMAL_BASE + OSAL_PRIO_BAND_WIDTH)
#define OSAL_PRIO_CRITICAL_BASE     (OSAL_PRIO_HIGH_BASE + OSAL_PRIO_BAND_WIDTH)

#endif /* OM_OSAL_CORE_H */

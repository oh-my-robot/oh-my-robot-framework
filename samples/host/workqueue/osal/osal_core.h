#ifndef OM_OSAL_CORE_H
#define OM_OSAL_CORE_H

#include <stdint.h>

typedef int32_t OsalStatus;

#define OSAL_OK           (0)
#define OSAL_TIMEOUT      (-1)
#define OSAL_WOULD_BLOCK  (-2)

#define OSAL_WAIT_FOREVER ((uint32_t)0xFFFFFFFFU)

typedef uint32_t OsalIrqIsrState;

int osal_is_in_isr(void);
void osal_irq_lock_task(void);
void osal_irq_unlock_task(void);
OsalIrqIsrState osal_irq_lock_from_isr(void);
void osal_irq_unlock_from_isr(OsalIrqIsrState state);

static inline void osal_irq_lock(OsalIrqIsrState *key)
{
    if (osal_is_in_isr()) {
        *key = osal_irq_lock_from_isr();
    } else {
        osal_irq_lock_task();
        *key = 0U;
    }
}

static inline void osal_irq_unlock(OsalIrqIsrState key)
{
    if (osal_is_in_isr()) {
        osal_irq_unlock_from_isr(key);
    } else {
        osal_irq_unlock_task();
    }
}

void osal_sleep_ms(uint32_t ms);

/** host 测试桩：不执行任何运行时检查；触发点表明不变量已破坏 */
#ifndef OSAL_ASSERT
#define OSAL_ASSERT(expr) ((void)0)
#endif

#endif

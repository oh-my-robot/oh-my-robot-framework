#ifndef OM_OSAL_CORE_H
#define OM_OSAL_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "osal_config.h"

/* OSAL 统一状态码（Phase 1 合同）*/
typedef int32_t OsalStatus_t;

#define OSAL_OK (0)               /* 成功 */
#define OSAL_TIMEOUT (-1)         /* 超时（timeout > 0 的 WAIT_FOREVER 语义超时）*/
#define OSAL_WOULD_BLOCK (-2)     /* 非阻塞调用（timeout=0）条件不满足 */
#define OSAL_NO_RESOURCE (-3)     /* 资源不足（内存/句柄/队列槽位）*/
#define OSAL_INVALID (-4)         /* 参数/句柄/上下文非法*/
#define OSAL_NOT_SUPPORTED (-5)   /* 端口不支持*/
#define OSAL_INTERNAL (-6)        /* 内部错误 */

/**
 * @brief 判断当前是否处于中断上下文
 * @return 1 表示在中断中，0 表示在线程上下文
 */
int osal_is_in_isr(void);

/**
 * @brief ISR 临界区状态类型
 * @note 由端口实现定义其具体含义；在 FreeRTOS 端映射为中断屏蔽旧状态
 */
typedef uint32_t OsalIrqIsrState_t;

/**
 * @brief 在线程上下文进入临界区
 * @note 禁止ISR 中调用
 */
void osal_irq_lock_task(void);

/**
 * @brief 在线程上下文退出临界区
 * @note 禁止ISR 中调用
 */
void osal_irq_unlock_task(void);

/**
 * @brief ISR 上下文进入临界区并保存中断状态
 * @return ISR 临界区恢复状态
 * @note 禁止在线程上下文调用
 */
OsalIrqIsrState_t osal_irq_lock_from_isr(void);

/**
 * @brief ISR 上下文恢复中断状态
 * @param state `osal_irq_lock_from_isr` 返回的状态
 * @note 禁止在线程上下文调用
 */
void osal_irq_unlock_from_isr(OsalIrqIsrState_t state);

/**
 * @brief OSAL 内存申请
 * @param size 申请字节数
 * @return 成功返回指针，失败返回 NULL
 * @note 严格模式下禁止在 ISR 中调用；若误用则触发断言，运行时返回 NULL
 */
void* osal_malloc(size_t size);

/**
 * @brief OSAL 内存释放
 * @param ptr 指针
 * @note 严格模式下禁止在 ISR 中调用；若误用则触发断言，运行时直接返回
 */
void osal_free(void* ptr);

#ifdef __OM_USE_ASSERT
/* 断言失败后进入死循环，便于调试定位*/
#define OSAL_ASSERT(expr)                                                                                                                  \
    do                                                                                                                                     \
    {                                                                                                                                      \
        if (!(expr))                                                                                                                       \
        {                                                                                                                                  \
            for (;;)                                                                                                                       \
            {                                                                                                                              \
            }                                                                                                                              \
        }                                                                                                                                  \
    } while (0)
#else
#define OSAL_ASSERT(expr) ((void)0)
#endif

/* 断言当前处于线程上下文*/
#define OSAL_ASSERT_IN_TASK() OSAL_ASSERT(!osal_is_in_isr())

#endif

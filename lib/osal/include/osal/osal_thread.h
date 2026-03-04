#ifndef OM_OSAL_THREAD_H
#define OM_OSAL_THREAD_H

#include <stdint.h>

#include "osal_core.h"

typedef struct OsalThreadHandle_s OsalThread;
typedef void (*OsalThreadEntryFunction)(void* arg);

typedef struct
{
    const char* name;    /* 线程名称，用于调试*/
    uint32_t stackSize; /* 栈大小（字节）*/
    uint32_t priority;   /* 线程优先级*/
} OsalThreadAttr;

/**
 * @brief 创建线程（线程上下文）
 * @param thread 输出线程句柄（必须非 NULL）
 * @param attr 线程属性；可为 NULL。`attr->stackSize==0` 时使用端口默认栈大小（字节）
 * @param entry 线程入口函数
 * @param arg 入口参数
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID/OSAL_NO_RESOURCE/OSAL_INTERNAL`
 * @note 禁止ISR 中调用
 */
OsalStatus osal_thread_create(OsalThread** thread, const OsalThreadAttr* attr, OsalThreadEntryFunction entry,
                                 void* arg);

/**
 * @brief 获取当前线程句柄（线程上下文）
 * @return 当前线程句柄；在 ISR 中误用时返回 NULL
 */
OsalThread* osal_thread_self(void);

/**
 * @brief 等待线程结束（可选能力，线程上下文）
 * @param thread 目标线程
 * @param timeout_ms 超时（支持 `OSAL_WAIT_FOREVER`）
 * @return `OSAL_OK` 成功；`OSAL_NOT_SUPPORTED` 端口不支持持；其他为错误码
 * @note 当前 FreeRTOS 端口保持 `OSAL_NOT_SUPPORTED`
 * @note 禁止ISR 中调用
 */
OsalStatus osal_thread_join(OsalThread* thread, uint32_t timeout_ms);

/**
 * @brief 主动让出 CPU（线程上下文）
 * @note 禁止ISR 中调用
 */
void osal_thread_yield(void);

/**
 * @brief 退出当前线程（线程上下文）
 * @note 禁止ISR 中调用
 */
void osal_thread_exit(void);

/**
 * @brief 终止指定线程（线程上下文）
 * @param thread 线程句柄
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID/OSAL_NOT_SUPPORTED/OSAL_INTERNAL`
 * @note 禁止ISR 中调用
 * @warning 危险语义：允许跨线程终止，但不承诺自动清理目标线程持有的业务资源（锁/外设/内存等）
 * @warning `terminate(self)` 视为非法，必须使用 `osal_thread_exit` 完成自退出
 * @note 推荐优先使用“协作退出”模型；跨线程终止仅用于受控场景
 */
OsalStatus osal_thread_terminate(OsalThread* thread);

/**
 * @brief 启动调度器（RTOS 场景，线程上下文）
 * @return `OSAL_OK` 成功；失败返`OSAL_INVALID/OSAL_INTERNAL`
 * @note 禁止ISR 中调用
 */
OsalStatus osal_kernel_start(void);

#endif

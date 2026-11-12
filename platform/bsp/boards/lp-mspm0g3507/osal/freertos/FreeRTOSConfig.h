/*
 * FreeRTOS Kernel V11.1.0
 *
 * 为 LP_MSPM0G3507 (Cortex-M0+, 32MHz, 32KB SRAM) 定制的配置文件。
 *
 * 本文件基于 MSPM0 SDK 自带的 FreeRTOSConfig.h（V10.4.3 版本配置）
 * 升级至 V11.1.0 API，同时保留了 TI 工具链特有项（TLS、PTLS、
 * traceTASK_DELETE 等）。
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*---------------------------------------------------------------------------*/
/* 硬件描述                                                                  */
/*---------------------------------------------------------------------------*/

#define configCPU_CLOCK_HZ               ((unsigned long) 32000000)

/*---------------------------------------------------------------------------*/
/* 调度行为                                                                  */
/*---------------------------------------------------------------------------*/

#define configTICK_RATE_HZ               1000
#define configUSE_PREEMPTION             1
#define configUSE_TIME_SLICING           0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TICKLESS_IDLE          1
#define configMAX_PRIORITIES             (32UL)
#define configMINIMAL_STACK_SIZE         ((unsigned short) 128)
#define configMAX_TASK_NAME_LEN          16

/*
 * V11.1.0: configTICK_TYPE_WIDTH_IN_BITS 替代 configUSE_16_BIT_TICKS
 */
#define configTICK_TYPE_WIDTH_IN_BITS    TICK_TYPE_WIDTH_32_BITS

#define configIDLE_SHOULD_YIELD          0
#define configTASK_NOTIFICATION_ARRAY_ENTRIES  2  /* index 1 used by Completion TN backend */
#define configQUEUE_REGISTRY_SIZE        0
#define configENABLE_BACKWARD_COMPATIBILITY    1  /* OSAL port uses V10 type names */
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS  2  /* TI 需要 TLS */
#define configUSE_MINI_LIST_ITEM         1
#define configSTACK_DEPTH_TYPE           size_t
#define configMESSAGE_BUFFER_LENGTH_TYPE size_t
#define configHEAP_CLEAR_MEMORY_ON_FREE  0
#define configSTATS_BUFFER_MAX_LENGTH    0xFFFF
#define configUSE_NEWLIB_REENTRANT       0

/*---------------------------------------------------------------------------*/
/* 软件定时器                                                                */
/*---------------------------------------------------------------------------*/

#define configUSE_TIMERS                 1
#define configTIMER_TASK_PRIORITY        (5)
#define configTIMER_QUEUE_LENGTH         20
#define configTIMER_TASK_STACK_DEPTH     (configMINIMAL_STACK_SIZE)

/*---------------------------------------------------------------------------*/
/* 事件组 / 流缓冲区                                                         */
/*---------------------------------------------------------------------------*/

#define configUSE_EVENT_GROUPS           1
#define configUSE_STREAM_BUFFERS         1
#define configUSE_TASK_NOTIFICATIONS     1
#define configUSE_MUTEXES                1
#define configUSE_RECURSIVE_MUTEXES      1
#define configUSE_COUNTING_SEMAPHORES    1
#define configUSE_QUEUE_SETS             0
#define configUSE_APPLICATION_TASK_TAG   0

/*---------------------------------------------------------------------------*/
/* 内存分配                                                                  */
/*---------------------------------------------------------------------------*/

#define configSUPPORT_STATIC_ALLOCATION  1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configTOTAL_HEAP_SIZE            ((size_t)(16 * 1024))
#define configAPPLICATION_ALLOCATED_HEAP 0
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP  0
#define configENABLE_HEAP_PROTECTOR      0

/*---------------------------------------------------------------------------*/
/* 中断嵌套                                                                  */
/*---------------------------------------------------------------------------*/

/* Cortex-M0+ 只实现 2 位优先级 */
#ifdef __NVIC_PRIO_BITS
    #define configPRIO_BITS  __NVIC_PRIO_BITS
#else
    #define configPRIO_BITS  2       /* 4 级优先级 */
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         0x03
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    1

#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configMAX_API_CALL_INTERRUPT_PRIORITY  configMAX_SYSCALL_INTERRUPT_PRIORITY

/*---------------------------------------------------------------------------*/
/* Hook 函数                                                                 */
/*---------------------------------------------------------------------------*/

#define configUSE_IDLE_HOOK              0
#define configUSE_TICK_HOOK              0
#define configUSE_MALLOC_FAILED_HOOK     0
#define configUSE_DAEMON_TASK_STARTUP_HOOK  0
#define configUSE_SB_COMPLETED_CALLBACK  0

/*---------------------------------------------------------------------------*/
/* 运行时统计                                                                */
/*---------------------------------------------------------------------------*/

#define configGENERATE_RUN_TIME_STATS             0
#define configUSE_TRACE_FACILITY                  1
#define configUSE_STATS_FORMATTING_FUNCTIONS      0

/*---------------------------------------------------------------------------*/
/* 协程                                                                      */
/*---------------------------------------------------------------------------*/

#define configUSE_CO_ROUTINES            0
#define configMAX_CO_ROUTINE_PRIORITIES  1

/*---------------------------------------------------------------------------*/
/* 调试                                                                      */
/*---------------------------------------------------------------------------*/

/* 断言：configASSERT 失败 → vAssertCalled（端口层 osal_fatal_freertos.c 强符号实现
 * → om_fatal_error(OM_FATAL_ASSERT)，统一收口替代本地死循环，见 ADR-0014） */
extern void vAssertCalled(const char *file, int line);
#define configASSERT(x) if ((x) == 0) vAssertCalled(__FILE__, __LINE__)

/*---------------------------------------------------------------------------*/
/* MPU — Cortex-M0+ 无硬件 MPU                                              */
/*---------------------------------------------------------------------------*/

#define configENABLE_MPU               0
#define configENABLE_FPU               0   /* MSPM0G3507 无 FPU */
#define configENABLE_MVE               0

/*---------------------------------------------------------------------------*/
/* 栈溢出检查                                                                */
/*---------------------------------------------------------------------------*/

#define configCHECK_FOR_STACK_OVERFLOW  2

/*---------------------------------------------------------------------------*/
/* TI 工具链特有                                                             */
/*---------------------------------------------------------------------------*/

#if defined(__TI_COMPILER_VERSION__) || defined(__ti_version__)
    /* PTLS_taskDeleteHook 由应用层提供桩实现 */
    void PTLS_taskDeleteHook(void *tcb);
    #define traceTASK_DELETE(pxTCB)  PTLS_taskDeleteHook(pxTCB)

    #define configNUM_THREAD_LOCAL_STORAGE_POINTERS  2
    #define PTLS_TLS_INDEX  0   /* ti.posix.freertos.PTLS */
    #define NDK_TLS_INDEX   1   /* 预留给 NDK */

#elif defined(__IAR_SYSTEMS_ICC__)
    #ifndef __IAR_SYSTEMS_ASM__
        #include <ti/posix/freertos/Mtx.h>
        #define traceTASK_DELETE(pxTCB)  Mtx_taskDeleteHook(pxTCB)
    #endif

    #define configNUM_THREAD_LOCAL_STORAGE_POINTERS  2
    #define MTX_TLS_INDEX   0   /* ti.posix.freertos.Mtx */
    #define NDK_TLS_INDEX   1

#elif defined(__ARMCC_VERSION)
    #define configNUM_THREAD_LOCAL_STORAGE_POINTERS  2
    #define NDK_TLS_INDEX   1

#elif defined(__GNUC__)
    #define configNUM_THREAD_LOCAL_STORAGE_POINTERS  1
    #define NDK_TLS_INDEX   0
    /* 注意：newlib 所需的 system locks 未实现 */
#endif

/*---------------------------------------------------------------------------*/
/* 精简 API                                                                  */
/*---------------------------------------------------------------------------*/

#define INCLUDE_vTaskPrioritySet                 1
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_vTaskCleanUpResources            0
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_uxTaskGetStackHighWaterMark      0
#define INCLUDE_xTaskGetIdleTaskHandle           0
#define INCLUDE_eTaskGetState                    1
#define INCLUDE_xTaskResumeFromISR               0
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_xSemaphoreGetMutexHolder         1
#define INCLUDE_xTimerPendFunctionCall           0
#define INCLUDE_xTaskAbortDelay                  0
#define INCLUDE_xTaskGetHandle                   0
#define INCLUDE_xEventGroupSetBitFromISR         1

/*---------------------------------------------------------------------------*/
/* 空闲功耗                                                                  */
/*---------------------------------------------------------------------------*/

#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP     2

/*---------------------------------------------------------------------------*/
/* ROV（TI 运行时对象查看工具）                                              */
/*---------------------------------------------------------------------------*/

#define configENABLE_ISR_STACK_INIT              0

/*---------------------------------------------------------------------------*/
/* CMSIS 中断处理程序映射                                                    */
/*---------------------------------------------------------------------------*/

#ifndef __TI_COMPILER_VERSION__
    #define xPortPendSVHandler    PendSV_Handler
    #define vPortSVCHandler       SVC_Handler
    #define xPortSysTickHandler   SysTick_Handler
#endif

/*---------------------------------------------------------------------------*/
/* configCHECK_HANDLER_INSTALLATION                                         */
/*---------------------------------------------------------------------------*/

/*
 * V11.1.0 端口默认使用直接路由：
 *   SVC → SVC_Handler → vPortSVCHandler_C
 *   PendSV → PendSV_Handler
 *   SysTick → SysTick_Handler
 * 这里配置为 0 因为 SysTick_Handler 需要额外调用 xPortSysTickHandler；
 * 实际挂载点在 main.c 中直接定义 ISR。
 */
#define configCHECK_HANDLER_INSTALLATION         0

/*---------------------------------------------------------------------------*/
/* V11.1.0 内核提供的静态内存回调（Idle / Timer 任务）                      */
/*---------------------------------------------------------------------------*/

#define configKERNEL_PROVIDED_STATIC_MEMORY      1

#endif /* FREERTOS_CONFIG_H */

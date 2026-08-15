/**
 * @file  om_system_startup.c
 * @brief 系统启动编排（init 子系统的一部分，kernel 层）
 * @details 参考成熟框架（Zephyr kernel/init.c、RT-Thread rtthread_startup）：
 *          "内核负责启动编排，app 的 main 只调一行"。
 *
 *          步骤：
 *            1. 调度器前：om_do_initcalls(EARLIEST, SERVICE) —— EARLIEST+BOARD+DRIVER
 *               （板级硬件自举 + 外设注册 + 驱动），不可阻塞；
 *            2. 建 init 线程（CRITICAL 带，高于业务线程）：调度器启动后跑
 *               om_do_initcalls(SERVICE, COUNT) —— SERVICE+SYSTEM+APPLICATION+LATE，
 *               可阻塞/IPC/建线程；app 自身启动设置也经 OM_INIT_APPLICATION 分散加载，
 *               在此自动执行（无需显式注册）；
 *            3. osal_kernel_start()：启动调度器，正常不返回。
 *
 *          与 om_init.c 同属 init 子系统（头文件统一在 core/om_init.h）。本文件因调用
 *          osal（建线程/起调度器），由 tar_awkernel 编译（依赖 core + osal）；tar_awcore
 *          的 glob 排除它，以保持 kernel-core 子模块 OS 无关（供主机侧测试）。
 */

#include "core/om_init.h"
#include "osal/osal.h"
#include "osal/osal_priority.h"

#ifndef OM_INIT_THREAD_STACK_SIZE
/** @brief init 线程栈大小（字节），可按工程覆盖 */
#define OM_INIT_THREAD_STACK_SIZE 2048u
#endif

/** @brief init 线程：调度器启动后跑 SERVICE+SYSTEM+APPLICATION+LATE（可阻塞），完成后自退出 */
static void om_init_thread(void *arg)
{
    (void)arg;
    (void)om_do_initcalls(OM_INIT_LEVEL_SERVICE, OM_INIT_LEVEL_COUNT);
    osal_thread_exit(); /* 任务入口不得返回，须自退出 */
}

void om_system_startup(void)
{
    /* 1. 调度器前：板级硬件自举 + 外设注册 + 驱动（不可阻塞） */
    (void)om_do_initcalls(OM_INIT_LEVEL_EARLIEST, OM_INIT_LEVEL_SERVICE);

    /* 2. init 线程：CRITICAL 带，确保 SERVICE/SYSTEM/APPLICATION/LATE 先于业务线程完成。
     *    app 的启动设置（建业务线程等）应注册为 OM_INIT_APPLICATION，由此自动执行。 */
    OsalThread *init_task = NULL;
    OsalThreadAttr init_attr = {
        .name = "om_init",
        .priority = OSAL_PRIO_CRITICAL_BASE,
        .stackSize = OM_INIT_THREAD_STACK_SIZE,
    };
    (void)osal_thread_create(&init_task, &init_attr, om_init_thread, NULL);

    /* 3. 启动调度器，正常不返回 */
    (void)osal_kernel_start();
    while (1)
    {
    }
}

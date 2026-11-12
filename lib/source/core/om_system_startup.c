/**
 * @file  om_system_startup.c
 * @brief 系统启动编排（init 子系统的一部分，kernel 层）
 * @details 参考成熟框架（Zephyr kernel/init.c、RT-Thread rtthread_startup）：
 *          "内核负责启动编排，app 的 main 只调一行"。
 *
 *          启动按"调度器边界"拆两级（ADR-0014）——每级都是自洽状态：
 *            om_startup_pre_scheduler()   调度器前 EARLIEST+BOARD+DRIVER（板级硬件自举 +
 *                                         外设注册 + 驱动），不可阻塞；返回首个失败错误码
 *            om_startup_post_scheduler()  建 init 线程（CRITICAL 带，跑 SERVICE..LATE，
 *                                         可阻塞/IPC/建线程；app 设置经 OM_INIT_APPLICATION
 *                                         在此自动执行）→ osal_kernel_start()（不返回）
 *            om_system_startup()          = pre + post 的默认组合接线
 *
 *          失败路径统一经 om_fatal_error()（见 core/om_fatal.h）——pre 失败（initcall 返回
 *          错误）/ init 线程创建失败 / 调度器启动失败均 fatal；自定义启动序列（强 main）可
 *          自行组合 pre/post 并决定 fatal 或降级。
 *
 *          与 om_init.c 同属 init 子系统（头文件统一在 core/om_init.h）。本文件因调用
 *          osal（建线程/起调度器），由 tar_awkernel 编译（依赖 core + osal）；tar_awcore
 *          的 glob 排除它，以保持 kernel-core 子模块 OS 无关（供主机侧测试）。
 */

#include "core/om_fatal.h"
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
    OmRet ret = om_do_initcalls(OM_INIT_LEVEL_SERVICE, OM_INIT_LEVEL_COUNT);
    if (ret != OM_OK)
    {
        /* 启动期任何级别 initcall 失败均为致命（与 pre 段对称）：带病启动比显式停机更危险
         * （机器人场景），由 handler 决定受控恢复（亮灯/软复位/跳 bootloader）。
         * detail 携带失败回调名（om_init_last_fail_name），handler 可定位失败点。 */
        const OmFatalContext ctx = {.detail = om_init_last_fail_name()};
        om_fatal_error(OM_FATAL_STARTUP, ret, &ctx);
    }
    osal_thread_exit(); /* 任务入口不得返回，须自退出 */
}

OmRet om_startup_pre_scheduler(void)
{
    /* 调度器前：板级硬件自举 + 外设注册 + 驱动（不可阻塞）；失败向调用方传播 */
    return om_do_initcalls(OM_INIT_LEVEL_EARLIEST, OM_INIT_LEVEL_SERVICE);
}

void om_startup_post_scheduler(void)
{
    /* init 线程：CRITICAL 带，确保 SERVICE/SYSTEM/APPLICATION/LATE 先于业务线程完成。
     * app 的启动设置（建业务线程等）应注册为 OM_INIT_APPLICATION，由此自动执行。 */
    OsalThread *init_task = NULL;
    OsalThreadAttr init_attr = {
        .name = "om_init",
        .priority = OSAL_PRIO_CRITICAL_BASE,
        .stackSize = OM_INIT_THREAD_STACK_SIZE,
    };
    if (osal_thread_create(&init_task, &init_attr, om_init_thread, NULL) != OSAL_OK)
    {
        om_fatal_error(OM_FATAL_STARTUP, OM_ERR_NO_MEM, NULL);
    }

    /* 启动调度器，正常不返回；返回即调度器启动失败 */
    (void)osal_kernel_start();
    om_fatal_error(OM_FATAL_STARTUP, OM_ERR_IO, NULL);
}

void om_system_startup(void)
{
    OmRet ret = om_startup_pre_scheduler();
    if (ret != OM_OK)
    {
        const OmFatalContext ctx = {.detail = om_init_last_fail_name()};
        om_fatal_error(OM_FATAL_STARTUP, ret, &ctx);
    }
    om_startup_post_scheduler();
}

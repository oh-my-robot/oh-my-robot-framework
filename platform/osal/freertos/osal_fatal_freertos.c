/**
 * @file  osal_fatal_freertos.c
 * @brief FreeRTOS 致命错误触发源收口（ADR-0014）——configASSERT 失败 / 任务栈溢出
 * @details FreeRTOS 的致命错误经两个回调暴露，此处统一收口到 om_fatal_error：
 *          - vAssertCalled：configASSERT(x) 失败时调用（板级 FreeRTOSConfig.h 引用），
 *            file/line 随 fatal context 扩展提供（ADR-0014 后续演进），当前暂不使用；
 *          - vApplicationStackOverflowHook：configCHECK_FOR_STACK_OVERFLOW=1/2 时由
 *            FreeRTOS 检测调用——注意已运行在溢出栈上，调用链保持最小（勿在此做事）。
 *          板级不再自写实现：删除 FreeRTOSConfig.h 中的 printf 宏 / 本地死循环。
 *          本文件进 tar_os（platform/osal/freertos），依赖方向 platform → kernel 合法。
 */
#include "core/om_fatal.h"
#include "FreeRTOS.h"
#include "task.h" /* TaskHandle_t */

void vAssertCalled(const char *file, int line)
{
    const OmFatalContext ctx = {.file = file, .line = line};
    om_fatal_error(OM_FATAL_ASSERT, OM_ERR_CONFLICT, &ctx);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    const OmFatalContext ctx = {.detail = pcTaskName};
    om_fatal_error(OM_FATAL_STACK_OVERFLOW, OM_ERR_OVERFLOW, &ctx);
}

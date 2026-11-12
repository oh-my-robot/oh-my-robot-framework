/**
 * @file main.c
 * @brief fatal 现场输出组合示范：handler 覆盖（om_log_panic 记录现场）→ 恢复策略（不返回）；
 *        OM_INIT_APPLICATION 触发断言（fatal 演示）。
 * @details 记录不属于 fatal 设施（实现位于 services/log 的 om_log_panic——故障直出）；
 *          组合点 = handler 覆盖（上层）+ 串口后端接线（log_port_init，DRIVER 级）；
 *          fatal 设施零改动。断言触发 → 现场经 panic 通道直出串口。
 */

#include "core/om_assert.h"
#include "core/om_fatal.h"
#include "core/om_init.h"
#include "drivers/peripheral/serial/log_serial_backend.h"
#include "services/log/log.h"

#include "bsp_serial.h" /* BSP_LOG_SERIAL_NAME：板级日志口选择 */

static LogSerialBackend g_log_serial_backend;

OM_LOG_MODULE(fatal_panic, OM_LOG_LEVEL_INFO);

#if OM_USE_LOG /* 组合层可裁剪（零日志配置下无记录——fatal 设施本身不受影响） */
/** @brief 接线（DRIVER 级，调度器前）：device_find 板级日志口 → 注册后端（含 panic 钩子）
 *  @return OM_OK 成功；失败传播（open/注册错误码） */
static OmRet log_port_init(void)
{
    return om_log_serial_backend_register(&g_log_serial_backend,
                                          device_find((char *)BSP_LOG_SERIAL_NAME), "serial", OM_LOG_LEVEL_INFO);
}
OM_INIT_DRIVER(log_port_init);
#endif /* OM_USE_LOG */

/** @brief handler 覆盖：记录现场（log 服务故障直出）→ 不返回（程序死亡）
 *  @param reason 触发源类别
 *  @param cause 具体错误码（OmRet）
 *  @param ctx 触发点上下文（file/line/pc/detail） */
void om_fatal_handler(OmFatalReason reason, OmRet cause, const OmFatalContext *ctx)
{
#if OM_USE_LOG /* 记录点 = 下游组合（裁剪下不记录——fatal 设施语义不变） */
    om_log_panic(&_om_log_module, OM_LOG_LEVEL_FATAL,
                 "fatal reason=%d cause=%d at %s:%d pc=0x%lx detail=%s",
                 (int)reason, (int)cause,
                 (ctx->file == NULL) ? "?" : ctx->file,
                 ctx->line,
                 (unsigned long)ctx->pc,
                 (ctx->detail == NULL) ? "?" : ctx->detail);
#endif
    /* 恢复策略（亮灯/软复位/跳 bootloader）在此展开——示范从略，不得返回 */
    for (;;)
    {
    }
}

/** @brief 触发点（APPLICATION 级）：故意断言失败 → om_fatal_error → handler */
static OmRet trigger_fatal(void)
{
    OM_ASSERT(0 && "fatal panic demo: intentional assert");
    return OM_OK;
}
OM_INIT_APPLICATION(trigger_fatal);

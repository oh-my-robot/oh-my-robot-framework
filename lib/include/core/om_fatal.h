/**
 * @file  om_fatal.h
 * @brief 致命错误设施（kernel-core，OS 无关）
 * @details 不可恢复错误的统一收敛入口（参考 Zephyr z_fatal_error / k_sys_fatal_error_handler、
 *          Linux panic）。所有致命触发源（启动失败、断言、硬件故障、栈溢出）统一经
 *          om_fatal_error() 进入，用户可覆盖 om_fatal_handler() 挂恢复动作（亮灯/软复位/
 *          跳 bootloader）。设计边界见 ADR-0014：
 *          - 设施与触发源分离：本文件只定义设施；触发源各自接入（当前仅 init 子系统）
 *          - om_fatal_error() 为强符号唯一入口，**永不返回**：调 handler 后禁中断 halt 兜底
 *          - handler 为框架侧 weak 扩展点（weak 仅限框架侧扩展点，应用层不得自行使用）
 *          - 原因枚举只增不改；OmRet 保持"返回值"语义，与"致命原因"语义不混用
 */
#ifndef __OM_FATAL_H__
#define __OM_FATAL_H__

#include "core/om_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 致命错误原因类别（只增不改；按触发源追加，如断言/硬件故障/栈溢出） */
typedef enum {
    OM_FATAL_STARTUP = 0, /**< 启动期失败：initcall 返回错误 / init 线程创建失败 / 调度器启动失败 */
} OmFatalReason;

/**
 * @brief 致命错误唯一入口：调 handler → 禁中断 halt。**永不返回**，任何上下文可调（ISR 安全）。
 * @param reason 原因类别
 * @param cause  具体错误码（如 initcall 返回的 OmRet；无则 OM_ERR_IO 兜底）
 */
void om_fatal_error(OmFatalReason reason, OmRet cause);

/**
 * @brief 用户可覆盖的致命错误处理（框架侧 weak 扩展点：默认空实现定义在 om_fatal.c，
 *        用户定义同名强函数自动覆盖——weak 属性只随默认实现，不在本声明上）
 * @details 覆盖实现不得返回（自行禁中断 halt / 软复位 / 跳 bootloader）；若返回，
 *          由 om_fatal_error() 入口禁中断 halt 兜底——"fatal 永不返回"由入口强制而非约定。
 */
void om_fatal_handler(OmFatalReason reason, OmRet cause);

#ifdef __cplusplus
}
#endif

#endif /* __OM_FATAL_H__ */

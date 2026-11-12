/**
 * @file log_serial_backend.h
 * @brief 串口日志后端工厂（drivers 层）
 * @details 依赖 services/log/log.h（drivers 可单向依赖 services 开放接口）
 *          开放的首个 drivers→services 跨层依赖（按服务逐个开放，当前仅 log）。
 *          实例由调用者静态分配（Workqueue 先例），container_of 模式取实例
 *          （backend 内嵌位置任意，多实例支持）。
 *          接线（组合层，3 行）：
 *            static LogSerialBackend g_log_serial;
 *            OM_INIT_DRIVER(log_port_init);  // device_find(BSP_LOG_SERIAL_NAME) → register
 */

#ifndef __OM_LOG_SERIAL_BACKEND_H__
#define __OM_LOG_SERIAL_BACKEND_H__

#include "drivers/model/device.h"
#include "services/log/log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 串口日志后端实例（调用者静态分配；backend **须为首成员**——本实现回调经
 *         首成员直接强转取实例，_Static_assert 编译期保障（log.h 框架侧无此要求，
 *         其他后端实现可用 container_of 任意位置内嵌））
 *  @note 多实例支持：每实例一个变量，各自持有 Device；实例生命周期由调用者管理 */
typedef struct {
    OmLogBackend backend; /* 首成员（offset 0；本文件 _Static_assert 强制） */
    Device *dev;          /* 串口设备（register 内部 open 后持有） */
} LogSerialBackend;

/** @brief 注册串口日志后端：内部 open(SERIAL_O_NBLCK_TX) 并注册到 log 服务
 *  @param inst 实例（调用者静态分配，零初始化即可）
 *  @param serial_dev 串口设备（device_find 获得；NULL → OM_ERR_INVALID_ARG）
 *  @param name 后端名（log 服务按名查找用）
 *  @param level 初始级别（过滤语义同 om_log_backend_register）
 *  @return OM_OK 成功；OM_ERR_INVALID_ARG 参数非法；open 失败或 log 注册失败传播其错误码
 *  @note 快速提交契约：push = device_write 非阻塞提交（NBLCK 打开语义；txFifo 满截断
 *        静默，溢出经设备 err_cb 暴露——失败感知分层，后端自持诊断）；线程上下文调用
 *        （内部 device_open 禁 ISR）；**日志口专用**——同一设备不应同时以其他模式
 *        （如业务阻塞收发）使用，需要复用请先 om_log_serial_backend_unregister
 *  @note panic 提交 = 轮询直写（putByte 逐字节，故障上下文 DMA/中断死仍有效）；
 *        push = 非阻塞快提交（正常路径）——双通道互补 */
#if OM_USE_LOG /* 裁剪契约（log.h 同款）：接口消失（编译期信号）——类型保留 */
OmRet om_log_serial_backend_register(LogSerialBackend *inst, Device *serial_dev, const char *name, OmLogLevel level);

/** @brief 注销串口日志后端并释放设备：注销 log 注册 + device_close
 *  @param inst 已注册的实例
 *  @return OM_OK 成功；OM_ERR_INVALID_ARG 参数非法；未注册（不在 log 后端表内）→
 *          OM_ERR_NOT_FOUND（设备保持打开，不产生副作用）
 *  @note 复用路径：注销后设备可重新 open 用于其他需求；对称于 register（配对防漏 close） */
OmRet om_log_serial_backend_unregister(LogSerialBackend *inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __OM_LOG_SERIAL_BACKEND_H__ */

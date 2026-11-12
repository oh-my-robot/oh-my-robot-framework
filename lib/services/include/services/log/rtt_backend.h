/**
 * @file rtt_backend.h
 * @brief RTT 日志后端（SEGGER Real Time Transfer——调试通道，高带宽零误码）
 * @details 输出经调试接口（SWD/JTAG）由宿主直读目标 RAM 环形缓冲——零引脚、1-10MB/s、
 *          内存级提交无硬件等待；与串口后端（独立引脚/低带宽）互补。
 *          库本体：third_party/segger-rtt/（BSD 许可，保留版权声明）。
 *          实例由调用者静态分配，multi-instance = multi-backend（同一通道按后端分级别过滤）。
 *          依赖契约：push/panic = SEGGER_RTT_Write（SPSC 环形——log 服务断言单生产者：
 *          就绪路径=日志线程；兜底路径=调用侧；panic 时线程已死——单写者恒成立）。
 *          v1 固定上向通道 0（Terminal）：SEGGER 库默认仅配置通道 0（_DoInit 只挂默认缓冲），
 *          通道 1..MAX-1 描述符零态（pBuffer NULL——写出将写坏内存，故禁止）；
 *          多通道留后续（SEGGER_RTT_ConfigUpBuffer 自备缓冲接入）。
 *          注册方式两种：①宏配置（OM_LOG_RTT=1，om_config.h）零接线隐藏注册——
 *          默认实例由本文件自注册（OM_INIT_DRIVER，早于业务日志）；②本 API 显式注册
 *          （多实例/自定义名）——与默认后端同名时注册表查重返回 OM_ERR_ALREADY（二选一）。
 */

#ifndef __OM_RTT_BACKEND_H__
#define __OM_RTT_BACKEND_H__

#include "services/log/log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief RTT 日志后端实例（调用者静态分配）
 *  @note backend 须为首成员——直接强转取实例（_Static_assert 编译期保障，与串口后端同款） */
typedef struct {
    OmLogBackend backend; /* 内嵌成员（首成员——直接强转定位） */
} OmRttBackend;

/** @brief 注册 RTT 日志后端（固定 RTT 上向通道 0）
 *  @param inst 实例（调用者静态分配，零初始化即可）
 *  @param name 后端名（log 服务按名查找用）
 *  @param level 初始级别（过滤语义同 om_log_backend_register）
 *  @return OM_OK 成功；OM_ERR_INVALID_ARG 参数非法；注册失败传播其错误码
 *  @note 线程上下文调用；push/panic = SEGGER_RTT_Write（字节拷入环形缓冲——满丢弃；
 *        单生产者由 log 服务保证——后端无锁）；panic 同 push（内存通道无硬件依赖，
 *        崩溃时仍可靠——与串口轮询钩子本质不同） */
#if OM_USE_LOG /* 裁剪契约（log.h 同款）：接口消失（编译期信号）——类型保留 */
OmRet om_rtt_backend_register(OmRttBackend *inst, const char *name, OmLogLevel level);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __OM_RTT_BACKEND_H__ */

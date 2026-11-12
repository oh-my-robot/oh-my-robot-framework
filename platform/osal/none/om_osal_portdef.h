/*===========================================================================
 * om_osal_portdef.h — osal-none（裸机单执行流）端口强制定义
 *
 * 本文件由 OS 端口实现者维护，提供该端口下所有 OSAL 层的硬性参数。
 * 性质：强制入口——osal_config.h 无条件包含；移植者必须确保本文件存在且值正确。
 * 位置：platform/osal/<os>/om_osal_portdef.h
 *
 * 裁剪/语义声明：
 * - 本端口无后台执行者（无线程调度）：依赖执行者的机制（workqueue 等）按
 *   os 轴编译期分支处理（见 lib/async workqueue 坍缩语义）；此处只声明
 *   OSAL 层参数与能力位。
 * - 同步加速能力：不提供（SYNC_ACCEL_CAP_* = 0）。
 *===========================================================================*/

#ifndef OM_OSAL_PORTDEF_H
#define OM_OSAL_PORTDEF_H

/*---------------------------------------------------------------------------
 * OSAL 参数（与 freertos 端口对齐，保持跨端口契约一致）
 *---------------------------------------------------------------------------*/
#ifndef OM_OSAL_PRIORITY_MAX
#define OM_OSAL_PRIORITY_MAX 32u
#endif

#ifndef OM_OSAL_TASK_NAME_MAX
#define OM_OSAL_TASK_NAME_MAX 16u
#endif

/*---------------------------------------------------------------------------
 * 同步加速后端能力声明：本端口不提供任何加速能力
 *---------------------------------------------------------------------------*/
#ifndef OM_SYNC_ACCEL_CAP_COMPLETION
#define OM_SYNC_ACCEL_CAP_COMPLETION 0
#endif

#endif /* OM_OSAL_PORTDEF_H */

/*===========================================================================
 * om_osal_portdef.h — FreeRTOS OSAL 端口强制定义
 *
 * 本文件由 OS 端口实现者维护，提供该 OS 下所有 OSAL 层的硬性参数。
 *
 * 性质：强制入口 — 框架 #error 校验，移植者必须确保本文件存在且值正确。
 * 位置：platform/osal/<os>/om_osal_portdef.h
 *
 * 移植者说明：
 *   - 本文件中的宏均无默认值；若缺失，osal_config.h 将产生编译期 #error。
 *   - 值必须与实际 FreeRTOSConfig.h 一致（OSAL_PRIORITY_MAX 对齐
 *     configMAX_PRIORITIES，等等）。
 *===========================================================================*/

#ifndef OM_OSAL_PORTDEF_H
#define OM_OSAL_PORTDEF_H

/*---------------------------------------------------------------------------
 * OSAL 参数 — 对齐 FreeRTOSConfig.h 默认值
 *
 * 若项目的 FreeRTOSConfig.h 使用了不同值（如 configMAX_PRIORITIES = 10），
 * 请在 builds 配置或 -D 编译选项中覆写。
 *---------------------------------------------------------------------------*/
#ifndef OM_OSAL_PRIORITY_MAX
#define OM_OSAL_PRIORITY_MAX              32u
#endif

#ifndef OM_OSAL_TASK_NAME_MAX
#define OM_OSAL_TASK_NAME_MAX             16u
#endif

/*---------------------------------------------------------------------------
 * sync 加速后端能力声明
 *
 * 本段声明该 OS 端口**提供**了哪些加速后端能力。
 * 应用层通过 om_config.h 中的 OM_SYNC_ACCEL 开关**选择**是否启用。
 *
 * 逻辑链：
 *   om_osal_portdef.h  →  声明 CAP_* = 1  (OS 端口能做)
 *   om_config.h        →  OM_SYNC_ACCEL 开关 (应用要开)
 *   completion.c       →  两者同时为 1 时走加速路径，否则回退 reference
 *---------------------------------------------------------------------------*/

/* FreeRTOS CAS + Task Notification (index 1) */
#ifndef OM_SYNC_ACCEL_CAP_COMPLETION
#define OM_SYNC_ACCEL_CAP_COMPLETION      1
#endif

#endif /* OM_OSAL_PORTDEF_H */

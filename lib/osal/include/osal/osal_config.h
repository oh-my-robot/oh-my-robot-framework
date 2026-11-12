#ifndef OM_OSAL_CONFIG_H
#define OM_OSAL_CONFIG_H

#include <stdint.h>

#include "core/om_config.h"
#include "osal_port.h" /* OSAL 端口选择与校验*/

/*
 * 强制引入 OS 端口定义文件。
 * 每个 OS 端口必须在其 platform/osal/<os>/ 目录下提供 om_osal_portdef.h。
 * 该文件包含该 OS 下 OSAL 层的硬性参数，无框架级默认值。
 * 若缺失，编译器将报 "file not found" 错误。
 */
#include "om_osal_portdef.h"

/* 等待无限时长的超时值*/
#ifndef OSAL_WAIT_FOREVER
#ifdef OM_OSAL_WAIT_FOREVER
#define OSAL_WAIT_FOREVER OM_OSAL_WAIT_FOREVER
#else
#define OSAL_WAIT_FOREVER 0xFFFFFFFFu
#endif
#endif

/* OSAL栈单位大小（字节），用于端口适配校验 */
#ifndef OSAL_STACK_WORD_BYTES
#ifdef OM_OSAL_STACK_WORD_BYTES
#define OSAL_STACK_WORD_BYTES OM_OSAL_STACK_WORD_BYTES
#else
#define OSAL_STACK_WORD_BYTES 4u
#endif
#endif

/* OSAL 最大优先级数量 */
#ifndef OSAL_PRIORITY_MAX
#ifdef OM_OSAL_PRIORITY_MAX
#define OSAL_PRIORITY_MAX OM_OSAL_PRIORITY_MAX
#else
#define OSAL_PRIORITY_MAX 32u
#endif
#endif

/* 任务名称最大长度*/
#ifndef OSAL_TASK_NAME_MAX
#ifdef OM_OSAL_TASK_NAME_MAX
#define OSAL_TASK_NAME_MAX OM_OSAL_TASK_NAME_MAX
#else
#define OSAL_TASK_NAME_MAX 16u
#endif
#endif

/* 语义优先级带宽宏（依赖 OSAL_PRIORITY_MAX，须在此之后引入） */
#include "osal_priority.h"

#endif

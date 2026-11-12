/**
 * @file   osal_priority.h
 * @brief  OSAL 优先级 host 裁剪头（spi_nor_test 用）
 *
 * 覆盖 lib/osal 版：后者经 osal_config.h 依赖端口定义。本测试只用到优先级
 * 带宽宏，而它们已由本地 osal_core.h 提供，故此处只做转发。
 */

#ifndef OM_OSAL_PRIORITY_H
#define OM_OSAL_PRIORITY_H

#include "osal_core.h"

#endif /* OM_OSAL_PRIORITY_H */

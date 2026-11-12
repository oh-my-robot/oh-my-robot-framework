#ifndef OM_OSAL_H
#define OM_OSAL_H

/* OSAL 统一入口头文件，包含核心与最小原语集（队列/事件语义由
 * 纯数据结构 + 原语组合实现，见 ADR-0023） */
#include "osal_core.h"
#include "osal_mutex.h"
#include "osal_sem.h"
#include "osal_thread.h"
#include "osal_time.h"
#include "osal_timer.h"

#endif

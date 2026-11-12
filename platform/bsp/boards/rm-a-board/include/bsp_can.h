/**
 * @file  bsp_can.h
 * @brief rm-a-board CAN 板配置 shim（板瘦身：类型/宏/契约已上移 can/bsp_can_f4.h）
 * @details 本文件只留板配置；全部类型与板数据契约见共享适配层头。
 */
#ifndef __BSP_CAN_H__
#define __BSP_CAN_H__
#ifdef OM_USE_BOARDCFG
#include "om_boardcfg.h" /* 工程板级覆写（boardcfg 契约见 ADR-0017）——须在默认值定义之前 */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 板配置：启用哪些 CAN 实例 */
#define USE_CAN1
#define USE_CAN2

/* 实例数：必须等于 bsp_can_data.c 中 gBspCan 条目数（数据文件内编译期校验） */
#define BSP_CAN_COUNT (2U)

#include "can/bsp_can_f4.h"

#ifdef __cplusplus
}
#endif

#endif /* __BSP_CAN_H__ */

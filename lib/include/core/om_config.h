#ifndef __OM_CONFIG_H__
#define __OM_CONFIG_H__

/* 功能裁剪 */
#define OM_USE_ASSERT  /* 框架断言（OM_ASSERT → om_fatal_error(OM_FATAL_ASSERT)，见 core/om_assert.h）。 \
                        * 默认启用；需关闭时在 om_appcfg.h 中 #undef OM_USE_ASSERT */

/* 抽象层裁剪 */
#define OM_USE_HAL_SERIALS
#define OM_USE_HAL_CAN
#define OM_USE_HAL_SPI
#define OM_USE_HAL_PWM

/*
 * sync 加速开关（统一入口）：
 *
 * - OM_SYNC_ACCEL: 全局加速开关，用于标记是否启用加速后端
 *
 * 说明：
 * - 分原语 capability 由 port 层注入（如 OM_SYNC_ACCEL_CAP_COMPLETION）。
 * - 当前 completion 无独立加速后端，默认回退 reference。
 */
#ifndef OM_SYNC_ACCEL
#define OM_SYNC_ACCEL 0
#endif

/*
 * 应用覆写入口：用户可在工程中放置 om_appcfg.h 以覆盖上述默认值。
 * 该文件不是必须的。启用方式：在包含 om_config.h 之前定义 OM_USE_APPCFG，
 * 或通过构建系统注入 -DOM_USE_APPCFG。
 */
#ifdef OM_USE_APPCFG
#include "om_appcfg.h"
#endif

#endif // __OM_CONFIG_H__

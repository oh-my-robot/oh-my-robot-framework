#ifndef __OM_CONFIG_H__
#define __OM_CONFIG_H__

/* 应用覆写入口（前置语义：用户宏先于默认值生效——默认值仅当未定义时设置
 * （#ifndef 惯例），无需 #undef 重定义。om_appcfg.h 非必需）。
 * 触发：构建注入 -DOM_USE_APPCFG（oh_my_robot.project_cfg 规则自动发现
 * <project>/cfg/om_appcfg.h），或在包含本头前定义。 */
#ifdef OM_USE_APPCFG
#include "om_appcfg.h"
#endif

/* 功能裁剪 */
#define OM_USE_ASSERT /* 框架断言（见 core/om_assert.h）；om_appcfg.h 中 #undef 可关闭 */

#ifndef OM_USE_LOG
#define OM_USE_LOG 1 /* services: log 服务（见 services/log/log.h）；值语义 =0 或 om_appcfg.h #undef（#if 未定义=0）可裁剪 */
#endif

#ifndef OM_LOG_MAX_BACKENDS
#define OM_LOG_MAX_BACKENDS 4 /* 后端注册表上限（定长数组） */
#endif

#ifndef OM_LOG_MAX_MODULES
#define OM_LOG_MAX_MODULES 16 /* 模块注册表上限（惰性登记；按名调节 om_log_module_set_level 用） */
#endif

#ifndef OM_LOG_SEGMENT_SIZE
#define OM_LOG_SEGMENT_SIZE 32 /* formatter 段缓冲（字节，栈占用 = 段 + 常量状态） */
#endif

/* log 服务异步能力（消费触发选择器）：1=日志线程抽环（门铃+线程，SERVICE init 建）；
 * 0=现场触发（生产入环 → 后端接受即同上下文抽环发射——零 OSAL，无线程无门铃，早期
 * 自然滞留环中）。生产侧形态两侧完全相同（消息恒入环——见服务 README 投递形态节）。 */
#ifndef OM_LOG_ASYNC
#define OM_LOG_ASYNC 1 /* 值语义（OM_SYNC_ACCEL 同款）：=0 或 appcfg #undef（#if 未定义=0）即裁剪 */
#endif

#ifndef OM_LOG_MAX_ARGS
#define OM_LOG_MAX_ARGS 8 /* 参数包上限（uintptr_t 宽度 ×8 = 64B）；取值 1..16（va 计数表上限）—— \
                            超限编译期报错（log.h ARG_LIMIT 负数组）+ 运行期丢弃计数 */
#endif
#if OM_LOG_MAX_ARGS > 16
#error "OM_LOG_MAX_ARGS 超出 va 计数表上限（1..16）——扩展 OM_LOG_VA_COUNT 表再调大"
#endif

/* log 消息环：生产侧唯一形态 = 消息恒入常驻环形（printk 形态——廉价/上下文安全/无消费者
 * 耦合；无消费者/无后端时消息自然滞留 = 早期日志场景）。容量 = 消息槽数（OmLogMsg 定长），
 * 须为 2 的幂；满 = 丢新 + 计数 + 后验告警（WRN 节流）。 */
#ifndef OM_LOG_RING_LEN
#define OM_LOG_RING_LEN 16 /* 消息槽数（≈1.2KB——8 槽队列 + 早期缓冲的合并预算） */
#endif
#if OM_LOG_RING_LEN < 4
#error "OM_LOG_RING_LEN 过小（下限 4 槽）"
#endif
#if (OM_LOG_RING_LEN & (OM_LOG_RING_LEN - 1)) != 0
#error "OM_LOG_RING_LEN 必须为 2 的幂（Ringbuf 掩码索引）"
#endif

/* log 丢弃后验告警：环满/参数超限的丢弃不再只进计数，由日志侧按节流补发 WRN 自证 */
#ifndef OM_LOG_DROP_WARN_INTERVAL_MS
#define OM_LOG_DROP_WARN_INTERVAL_MS 1000 /* 告警最小间隔（毫秒——防高频丢弃刷屏） */
#endif

/* RTT 内置默认后端（services/log/rtt_backend.h）：1=零接线隐藏注册（本组配置生效），
 * 0=仅显式 om_rtt_backend_register 注册（默认——现有行为不变）。 */
#ifndef OM_LOG_RTT
#define OM_LOG_RTT 0
#endif

#ifndef OM_LOG_RTT_NAME
#define OM_LOG_RTT_NAME "rtt" /* 默认后端名（om_log_backend_set_level 按名调节用；仅 OM_LOG_RTT=1 时生效） */
#endif

#ifndef OM_LOG_RTT_LEVEL
#define OM_LOG_RTT_LEVEL OM_LOG_LEVEL_INFO /* 默认后端初始级别（初始=宏参数；运行期 om_log_backend_set_level 调节）； \
                                            * 编译期越界守卫：rtt_backend.c _Static_assert（enum 常量 #if 不可见） */
#endif

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

#endif // __OM_CONFIG_H__

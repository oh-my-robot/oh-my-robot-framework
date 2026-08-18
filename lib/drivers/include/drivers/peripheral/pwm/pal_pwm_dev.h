/**
 * @file    pal_pwm_dev.h
 * @defgroup PWM PWM 子系统
 * @brief   PWM PAL 设备驱动接口
 * @details
 * PWM 子系统提供脉宽调制输出能力，支持周期/脉宽/极性配置和运行时占空比调整。
 *
 * 核心概念：
 * - **PwmChannelSpec** — 编译时通道描述符（控制器名 + 通道号），可声明为 static const
 * - **PwmChannel** — 运行时通道句柄，由 pwm_channel_get() 从 PwmChannelSpec 解析获得，
 *   后续操作直接通过已解析的控制器指针，无需字符串查找
 * - **PwmController** — PWM 控制器，内嵌 Device 参与设备链表管理
 * - **PwmCapability** — BSP 注册时声明的硬件能力（通道数/频率范围/分辨率/支持的特性）
 * - **PwmOps** — BSP 硬件操作函数表，BSP 层实现并注入控制器
 *
 * 线程安全：
 * - pwm_channel_config / pwm_channel_enable / pwm_channel_disable 仅在**线程**上下文调用
 * - pwm_channel_set_pulse 可在**线程或 ISR** 上下文调用，禁止阻塞
 *
 * 并发保护：
 * - ISR 安全：channelConfig（线程）和 channelSetPulse（ISR）并发安全——
 *   缓存的 ns/cycles 比例在 osal_irq_lock 保护下写入，ISR 不会读到不完整值
 * - **无保护**：多线程间并发——同时从不同线程调用 channelConfig、
 *   或线程同时调用 channelConfig 与 channelSetPulse，调用者自行串行化
 * - 多通道间（同一控制器不同 channel）并行操作安全，无需额外保护
 *
 * 硬件约定：
 * - **预装载机制（必须）**：BSP 必须启用硬件预装载，确保周期/脉宽在下一个周期边界同步生效
 * - **停止状态**：channel_disable 后引脚默认输出低电平
 *
 * @see     docs/03_pwm_pal_interface_design.md — 完整设计规格
 */

#ifndef __PAL_PWM_DEV_H__
#define __PAL_PWM_DEV_H__

#include "drivers/model/device.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 枚举类型
 * ================================================================ */

/** PWM 输出极性 */
typedef enum {
    PWM_POLARITY_NORMAL   = 0U, /**< 正常：高电平 = 有效 (active high) */
    PWM_POLARITY_INVERSED = 1U, /**< 反转：低电平 = 有效 (active low) */
} PwmPolarity;

/* ================================================================
 * 能力声明
 * ================================================================ */

/**
 * @name PWM 能力位图
 * @details BSP 注册时通过 PwmCapability.caps 声明控制器支持的特性。
 *          低 16 位为标准能力，高 16 位预留给平台专用扩展。
 * @{
 */

/** 支持正常极性 */
#define PWM_CAP_POLARITY_NORMAL (1U << 0)

/** 支持反转极性 */
#define PWM_CAP_POLARITY_INVERSED (1U << 1)

/** 支持互补输出（预留，当前未实现） */
#define PWM_CAP_COMPLEMENTARY (1U << 2)

/** 支持死区插入（预留，当前未实现） */
#define PWM_CAP_DEADTIME (1U << 3)

/** 支持输入捕获（预留，当前未实现） */
#define PWM_CAP_CAPTURE (1U << 4)

/** 支持故障保护（预留，当前未实现） */
#define PWM_CAP_FAULT_PROTECT (1U << 5)

/** @} */

/**
 * @brief  PWM 控制器能力声明
 * @details BSP 在注册时填充，框架据此校验应用层传入的参数。
 *          所有字段在注册后只读，线程安全。
 */
typedef struct {
    uint8_t numChannels;   /**< 可用通道数 (1~N) */
    uint32_t maxPeriodNs;  /**< 最大周期 (ns) → 最低频率 */
    uint32_t minPeriodNs;  /**< 最小周期 (ns) → 最高频率 */
    uint32_t resolutionHz; /**< 计数器时钟频率 (Hz)，用于 ns ↔ cycles 转换 */
    uint32_t caps;         /**< 能力位图，见 PWM_CAP_xxx */
    uint8_t counterWidth;  /**< 计数器位宽 (16 或 32) */
} PwmCapability;

/* ================================================================
 * 前向声明
 * ================================================================ */

typedef struct PwmController PwmController;
typedef struct PwmOps PwmOps;

/* ================================================================
 * 通道标识（双类型：Spec 编译时声明，Channel 运行时句柄）
 * ================================================================ */

/**
 * @brief  编译时通道描述符
 * @details 可声明为 static const，零运行时构建开销。
 *          通过 pwm_channel_get() 解析为运行时句柄 #PwmChannel。
 *
 * @code
 * static const PwmChannelSpec ledSpec = { "pwm1", 0 };
 * @endcode
 */
typedef struct {
    const char *controller; /**< 控制器名称（如 "pwm1"），由 BSP 定义 */
    uint8_t channel;        /**< 通道号（从 0 开始） */
} PwmChannelSpec;

/**
 * @brief  运行时通道句柄
 * @details 由 pwm_channel_get() 从 #PwmChannelSpec 解析获得，包含已解析的控制器指针。
 *          后续操作直接通过 ctrl 指针路由，零字符串查找开销。
 *
 * @code
 * PwmChannel ch;
 * OmRet ret = pwm_channel_get(&ledSpec, &ch);
 * if (ret != OM_OK) return ret;
 * pwm_channel_config(ch, &cfg);
 * pwm_channel_enable(ch);
 * @endcode
 */
typedef struct {
    PwmController *ctrl; /**< 已解析的控制器指针，NULL 表示无效 */
    uint8_t channel;     /**< 通道号 */
} PwmChannel;

/**
 * @brief  检查通道句柄是否有效
 * @details 等效于 `ch.ctrl != NULL`。
 * @param[in] ch  待检查的通道句柄
 * @return true 有效，false 无效（ctrl 为 NULL）
 */
static inline bool pwm_channel_valid(PwmChannel ch)
{
    return ch.ctrl != NULL;
}

/* ================================================================
 * 通道状态（per-channel，框架是唯一真相源）
 * ================================================================ */

/**
 * @brief  每个 PWM 通道一份状态
 * @details 存在 PwmController 中，BSP 提供存储。框架在 channelConfig/
 *          channelEnable/channelDisable/channelSetPulse 时更新对应字段。
 *          应用层可通过 pwm_channel_get_state() 读取当前状态（只读）。
 */
typedef struct {
    uint32_t periodNs;     /**< 已配置的周期 (ns)，0 = 从未 config */
    uint32_t periodCycles; /**< 硬件周期 ticks（ISR 快速路径使用） */
    uint32_t pulseNs;      /**< 最近一次 config 的脉宽 (ns) */
    PwmPolarity polarity;  /**< 当前输出极性 */
    bool enabled;          /**< 是否正在输出 */
} PwmChannelState;

/* ================================================================
 * 通道配置
 * ================================================================ */

/**
 * @brief  PWM 通道配置结构体
 * @details 通过 pwm_channel_config() 一次性应用周期、脉宽和极性。
 */
typedef struct {
    uint32_t periodNs;    /**< 周期 (ns) */
    uint32_t pulseNs;     /**< 脉宽/高电平时间 (ns)，必须 ≤ periodNs */
    PwmPolarity polarity; /**< 输出极性 */
} PwmChannelConfig;

/* ================================================================
 * BSP 硬件操作函数表 (PwmOps)
 * ================================================================ */

/**
 * @brief  BSP 硬件操作函数表
 * @details BSP 层实现此函数表并通过 pwm_controller_register() 注入到控制器。
 *
 * **通用约定**：
 * - `ctrl`   — 当前控制器指针，通过 `ctrl->priv` 获取 BSP 私有数据
 * - `channel` — 通道号，框架已校验范围 [0, numChannels)，BSP 无需再次校验
 * - 所有函数同步完成（非异步回调模式）
 *
 * **调用上下文**：
 * - channelConfig / channelEnable / channelDisable — 仅**线程**上下文调用
 * - channelSetPulse — 可能在**线程或 ISR** 中被调用，禁止阻塞
 */
struct PwmOps {

    /**
     * @brief  配置通道周期、脉宽和极性
     *
     * @details 将指定通道的硬件寄存器编程为给定的周期、脉宽和极性。
     *          此函数仅配置——不启动输出。启动由 channelEnable 单独执行。
     *
     *          **预装载机制（必须）**：
     *          BSP 必须启用硬件预装载/缓冲机制（MCU 定时器的自动重装载/输出比较预装载位），
     *          确保周期和脉宽的更新在下一个更新事件（UEV）时同步生效。
     *
     * @param[in] ctrl    当前控制器
     * @param[in] channel 通道号 [0, numChannels)
     * @param[in] period_cycles  周期（硬件计数 tick）
     * @param[in] pulse_cycles   脉宽（硬件计数 tick）
     * @param[in] polarity      输出极性
     * @return OM_OK 成功，OM_ERR_NOT_SUPPORTED 极性不支持
     *
     * @note 仅线程上下文调用。可在输出已启用时调用以更新配置。
     */
    OmRet (*channelConfig)(PwmController *ctrl, uint8_t channel,
                           uint32_t period_cycles, uint32_t pulse_cycles,
                           PwmPolarity polarity);

    /**
     * @brief  启动通道输出
     *
     * @details 使能 PWM 信号输出。调用前应先通过 channelConfig 配置参数。
     *          若已启动则为空操作（返回 OM_OK）。
     *
     * @param[in] ctrl    当前控制器
     * @param[in] channel 通道号 [0, numChannels)
     * @return OM_OK 成功
     *
     * @note 仅线程上下文调用
     */
    OmRet (*channelEnable)(PwmController *ctrl, uint8_t channel);

    /**
     * @brief  停止通道输出
     *
     * @details 禁用 PWM 信号输出。停止后引脚状态约定为**低电平**（物理 0）。
     *          若已停止则为空操作（返回 OM_OK）。
     *
     * @param[in] ctrl    当前控制器
     * @param[in] channel 通道号 [0, numChannels)
     * @return OM_OK 成功
     *
     * @note 仅线程上下文调用
     */
    OmRet (*channelDisable)(PwmController *ctrl, uint8_t channel);

    /**
     * @brief  运行时更新脉宽（不改变周期和极性）
     *
     * @details 仅更新占空比，通过预装载机制在下一个周期边界生效，实现无毛刺更新。
     *
     *          ISR 安全——可在中断上下文中调用，
     *          用于电机控制等需要高频更新占空比的场景。
     *
     * @param[in] ctrl        当前控制器
     * @param[in] channel     通道号 [0, numChannels)
     * @param[in] pulse_cycles 新脉宽（硬件计数 tick）
     * @return OM_OK 成功
     *
     * @note 可在 ISR 上下文调用，禁止阻塞
     * @note 调用者保证通道已配置并启用
     */
    OmRet (*channelSetPulse)(PwmController *ctrl, uint8_t channel,
                             uint32_t pulse_cycles);
};

/* ================================================================
 * PWM 控制器结构体
 * ================================================================ */

/**
 * @brief  PWM 控制器
 * @details 每个 PWM 外设实例注册为一个控制器。
 *          BSP 通过 pwm_controller_register() 注册，应用层通过 PwmChannelSpec 引用。
 *
 * **并发约定**：
 * - ISR 安全（框架保护）：缓存值在 osal_irq_lock 下写入，ISR 中的
 *   channelSetPulse 不会读到不完整的 (cachedPeriodNs, cachedPeriodCycles) 对。
 * - **无保护**（调用者负责）：
 *   - 多线程同时调用 pwm_channel_config 于同一控制器
 *   - 线程 A 调 pwm_channel_config，线程 B 同时调 pwm_channel_set_pulse
 *   以上场景需调用者以 mutex 或其他方式自行串行化。
 * - 多通道间（同一控制器不同 channel）的并行操作安全，无需额外保护。
 *
 * **通道状态**：
 * - chState 指向 BSP 提供的 PwmChannelState 数组，长度 = cap->numChannels。
 *   框架层在 channelConfig/enable/disable/setPulse 时更新对应字段。
 *   应用层通过 pwm_channel_get_state() 读取（只读）。
 *   BSP 不应直接读写 chState 内容。
 */
struct PwmController {
    Device parent;                                                       /**< 内嵌 Device，参与设备链表管理 */
    const PwmOps *ops;                                                   /**< BSP 注入的硬件操作函数表 */
    const PwmCapability *cap;                                            /**< BSP 注入的能力声明（只读） */
    PwmChannelState *chState; /**< per-channel 状态数组，BSP 提供存储 */ /**< per-channel 状态数组，BSP 提供存储 */
};

/* ================================================================
 * 公共 API — 通道解析
 * ================================================================ */

/**
 * @brief  从编译时描述符解析运行时通道句柄
 * @param[in]  spec  编译时通道描述符，不可为 NULL
 * @param[out] ch    解析结果句柄；失败时写入 ctrl=NULL 的无效句柄
 * @return OM_OK 成功，OM_ERR_INVALID_ARG 参数无效，
 *         OM_ERR_NOT_FOUND 控制器未找到或类型不匹配
 * @note   通常在初始化阶段调用一次，后续复用返回的 PwmChannel
 */
OmRet pwm_channel_get(const PwmChannelSpec *spec, PwmChannel *ch);

/* ================================================================
 * 公共 API — 配置和启停
 * ================================================================ */

/**
 * @brief  配置通道周期、脉宽和极性
 * @details 不启动输出——需单独调用 pwm_channel_enable() 启动。
 *          可在输出已启用时调用以更新配置（通过预装载机制无毛刺切换）。
 *
 * @param[in] ch    有效的通道句柄
 * @param[in] cfg   配置参数，不可为 NULL
 * @return OM_OK 成功，OM_ERR_INVALID_ARG 参数无效，
 *         OM_ERR_RANGE period/pulse 超出能力范围，
 *         OM_ERR_NOT_SUPPORTED 请求的极性不被该控制器支持
 * @note   仅线程上下文调用
 */
OmRet pwm_channel_config(PwmChannel ch, const PwmChannelConfig *cfg);

/**
 * @brief  启动通道输出
 * @param[in] ch  有效的通道句柄
 * @return OM_OK 成功
 * @note   仅线程上下文调用。若尚未配置，行为由 BSP 决定。
 */
OmRet pwm_channel_enable(PwmChannel ch);

/**
 * @brief  停止通道输出
 * @details 停止后引脚输出低电平。
 * @param[in] ch  有效的通道句柄
 * @return OM_OK 成功
 * @note   仅线程上下文调用
 */
OmRet pwm_channel_disable(PwmChannel ch);

/* ================================================================
 * 公共 API — 运行时脉宽调整
 * ================================================================ */

/**
 * @brief  运行时更新脉宽（不改变周期和极性）
 * @details ISR 安全——可在中断上下文中调用。
 *
 *          使用 chState 中缓存的 periodNs/periodCycles 比例转换，
 *          避免每次 ISR 重复 resolutionHz/1e9 的 64 位除法。
 *
 *          **并发**：与 channelConfig 并发安全——chState 在 osal_irq_lock
 *          保护下写入。线程间并发仍需调用者自行串行化。
 *
 * @param[in] ch       有效的通道句柄
 * @param[in] pulse_ns 新脉宽 (ns)
 * @return OM_OK 成功，OM_ERR_CONFLICT 通道未 config，
 *         OM_ERR_RANGE pulse_ns 超出 periodNs
 * @note   可在 ISR 上下文调用，禁止阻塞
 * @note   调用者保证通道已通过 channelConfig 配置并 channelEnable 启用
 */
OmRet pwm_channel_set_pulse(PwmChannel ch, uint32_t pulse_ns);

/* ================================================================
 * 公共 API — 能力查询
 * ================================================================ */

/**
 * @brief  获取控制器能力声明（只读）
 * @param[in] ch  有效的通道句柄
 * @return 能力结构体的只读指针；ch 无效时返回 NULL
 */
const PwmCapability *pwm_channel_get_capability(PwmChannel ch);

/* ================================================================
 * Device control 命令
 * ================================================================ */

#define PWM_CMD_GET_CAPABILITY (0x00U) /**< 查询控制器能力: *(const PwmCapability**)arg */
#define PWM_CMD_SUSPEND        (0x01U) /**< 暂停全部通道输出，保留配置和 enabled 标记 */
#define PWM_CMD_RESUME         (0x02U) /**< 恢复 suspend 的通道输出 */

/* ================================================================
 * BSP 注册接口
 * ================================================================ */

/**
 * @brief  注册 PWM 控制器到框架
 * @details BSP 层在初始化时调用此函数，将硬件 PWM 外设注册为框架控制器。
 *          注册后，应用层可通过 PwmChannelSpec + pwm_channel_get() 引用。
 *
 *          chState 数组存储 per-channel 状态（周期/脉宽/极性/启停），
 *          框架在 channelConfig/enable/disable/setPulse 时自动更新。
 *          pwm_channel_set_pulse 使用缓存的 ns/cycles 比例避免 ISR 中 64 位除法。
 *
 *          **硬件约束**：同一 TIM 的通道共享 ARR/PSC（时基）。
 *          同一控制器各通道必须使用相同周期，后配的覆盖先配的。
 *
 * @param[in] ctrl     控制器结构体指针（BSP 分配），不可为 NULL
 * @param[in] name     控制器名称（如 "pwm1"），不可为 NULL
 * @param[in] cap      能力声明，不可为 NULL
 * @param[in] ops      硬件操作函数表，不可为 NULL
 * @param[in] priv     BSP 私有数据指针，存入 parent.handle
 * @param[in] chState  per-channel 状态数组，大小 >= numChannels，不可为 NULL。
 *                     框架层读写内容，BSP 不应直接修改。
 * @return OM_OK 成功，OM_ERR_INVALID_ARG 参数无效
 * @note   仅在初始化阶段调用
 */
OmRet pwm_controller_register(PwmController *ctrl, const char *name,
                              const PwmCapability *cap,
                              const PwmOps *ops, void *priv,
                              PwmChannelState *chState);

/* ================================================================
 * 状态查询
 * ================================================================ */

/**
 * @brief  读取通道当前状态（只读）
 * @param[in] ch  有效的通道句柄
 * @return 状态结构体的只读指针；ch 无效时返回 NULL
 */
const PwmChannelState *pwm_channel_get_state(PwmChannel ch);

#ifdef __cplusplus
}
#endif

#endif /* __PAL_PWM_DEV_H__ */

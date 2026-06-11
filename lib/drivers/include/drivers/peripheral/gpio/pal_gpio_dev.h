/**
 * @file    pal_gpio_dev.h
 * @defgroup GPIO GPIO 子系统
 * @brief   GPIO PAL 设备驱动接口
 * @details
 * GPIO 子系统提供通用引脚 I/O、电气配置和中断管理能力。
 *
 * 核心概念：
 * - **GpioPinSpec** — 编译时引脚描述符（控制器名 + 偏移），可声明为 static const
 * - **GpioPin** — 运行时引脚句柄，由 gpio_pin_get() 从 GpioPinSpec 解析获得，
 *   后续操作直接通过已解析的控制器指针，无需字符串查找
 * - **GpioPort** — 端口句柄，由 gpio_port_get() 解析，用于端口级批量操作
 * - **GpioController** — GPIO 控制器，内嵌 Device 参与设备链表管理
 *
 * 线程安全：
 * - gpio_pin_attach_irq / gpio_pin_detach_irq 内部关中断保护回调表
 * - gpio_pin_write / read / toggle / configure / irq_enable 不加锁
 * - 调用者需保证同一引脚的 configure 不与 write/read 并发
 *
 * @see     lib/drivers/docs/gpio_design.md — 设计决策详述
 */

#ifndef __PAL_GPIO_DEV_H__
#define __PAL_GPIO_DEV_H__

#include "drivers/model/device.h"
#include "osal/osal_core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name 引脚标志
 * @{ */

/** 低电平有效标志。
 *  置位后 gpio_pin_write / gpio_pin_read 自动反转物理电平。 */
#define GPIO_FLAG_ACTIVE_LOW (0x01U)

/** @} */

/**
 * @name 枚举类型
 * @{ */

/** 引脚方向 */
typedef enum {
    GPIO_DIR_INPUT  = 0U,  /**< 输入模式 */
    GPIO_DIR_OUTPUT = 1U,  /**< 输出模式 */
} GpioDirection;

/** 上下拉配置 */
typedef enum {
    GPIO_PULL_NONE = 0U,  /**< 无上下拉 */
    GPIO_PULL_UP   = 1U,  /**< 上拉 */
    GPIO_PULL_DOWN = 2U,  /**< 下拉 */
} GpioPull;

/** 驱动模式 */
typedef enum {
    GPIO_DRIVE_PUSH_PULL  = 0U,  /**< 推挽输出 */
    GPIO_DRIVE_OPEN_DRAIN = 1U,  /**< 开漏输出 */
} GpioDrive;

/** 驱动强度（硬件相关，不支持时 BSP 返回 OM_ENOTSUP） */
typedef enum {
    GPIO_DRIVE_STRENGTH_LOW    = 0U,  /**< 低驱动强度 */
    GPIO_DRIVE_STRENGTH_MEDIUM = 1U,  /**< 中驱动强度 */
    GPIO_DRIVE_STRENGTH_HIGH   = 2U,  /**< 高驱动强度 */
} GpioDriveStrength;

/** 中断触发模式 */
typedef enum {
    GPIO_IRQ_EDGE_RISING  = 0U,  /**< 上升沿触发 */
    GPIO_IRQ_EDGE_FALLING = 1U,  /**< 下降沿触发 */
    GPIO_IRQ_EDGE_BOTH    = 2U,  /**< 双边沿触发 */
    GPIO_IRQ_LEVEL_HIGH   = 3U,  /**< 高电平触发 */
    GPIO_IRQ_LEVEL_LOW    = 4U,  /**< 低电平触发 */
} GpioIrqMode;

/** @} */

/**
 * @name IRQ 能力标志
 * @details 注册控制器时通过 caps 参数声明支持的 IRQ 触发模式。
 *          框架层 gpio_pin_attach_irq 会校验请求的模式是否在 caps 中声明。
 *          位号与 #GpioIrqMode 枚举值对齐，可直接用 `1U << mode` 查询。
 * @{ */

#define GPIO_CAP_IRQ_EDGE_RISING   (1U << GPIO_IRQ_EDGE_RISING)   /**< 支持上升沿触发 */
#define GPIO_CAP_IRQ_EDGE_FALLING  (1U << GPIO_IRQ_EDGE_FALLING)  /**< 支持下降沿触发 */
#define GPIO_CAP_IRQ_EDGE_BOTH     (1U << GPIO_IRQ_EDGE_BOTH)     /**< 支持双边沿触发 */
#define GPIO_CAP_IRQ_LEVEL_HIGH    (1U << GPIO_IRQ_LEVEL_HIGH)    /**< 支持高电平触发 */
#define GPIO_CAP_IRQ_LEVEL_LOW     (1U << GPIO_IRQ_LEVEL_LOW)     /**< 支持低电平触发 */

/** @} */

/* ===== 前向声明 ===== */

typedef struct GpioController GpioController;
typedef struct GpioOps GpioOps;

/**
 * @name 引脚标识（双类型：Spec 编译时声明，Pin 运行时句柄）
 * @{ */

/**
 * @brief  编译时引脚描述符
 * @details 可声明为 static const，零运行时构建开销。
 *          通过 gpio_pin_get() 解析为运行时句柄 #GpioPin。
 *
 * @code
 * static const GpioPinSpec led_spec = { "gpioa", 5, 0 };
 * @endcode
 */
typedef struct {
    const char *controller;  /**< 控制器名称，由 BSP 定义（如 "gpioa"） */
    uint8_t offset;          /**< 控制器内引脚偏移（从 0 开始） */
    uint32_t flags;          /**< 引脚属性标志，如 #GPIO_FLAG_ACTIVE_LOW */
} GpioPinSpec;

/**
 * @brief  运行时引脚句柄
 * @details 由 gpio_pin_get() 从 #GpioPinSpec 解析获得，包含已解析的控制器指针。
 *          后续操作直接通过 ctrl 指针路由，零字符串查找开销，ISR 安全。
 *
 * @code
 * GpioPin led = gpio_pin_get(&led_spec);
 * if (!gpio_pin_valid(led)) return OM_ENODEV;
 * gpio_pin_write(led, 1);
 * @endcode
 */
typedef struct {
    GpioController *ctrl;  /**< 已解析的控制器指针，NULL 表示无效 */
    uint8_t offset;        /**< 控制器内引脚偏移 */
    uint32_t flags;        /**< 引脚属性标志 */
} GpioPin;

/** @} */

/**
 * @name 端口句柄
 * @{ */

/**
 * @brief  端口句柄
 * @details 由 gpio_port_get() 解析获得，用于端口级批量操作。
 */
typedef struct {
    GpioController *ctrl;  /**< 已解析的控制器指针，NULL 表示无效 */
} GpioPort;

/** @} */

/**
 * @name 引脚配置
 * @{ */

/**
 * @brief  引脚配置结构体
 * @details 通过 gpio_pin_configure() 一次性应用所有电气属性。
 */
typedef struct {
    GpioDirection direction;      /**< 引脚方向：输入或输出 */
    GpioPull pull;                /**< 上下拉配置 */
    GpioDrive drive;              /**< 驱动模式：推挽或开漏 */
    GpioDriveStrength speed;      /**< 驱动强度 */
    bool init_high;               /**< 输出初始逻辑电平（仅输出模式有效）。
                                        true = 逻辑高（ACTIVE_LOW 引脚自动反转物理电平） */
} GpioPinConfig;

/** @} */

/**
 * @brief  中断回调描述（框架内部使用，用户无需直接操作）
 */
typedef struct {
    void (*callback)(void *arg);  /**< 用户回调函数 */
    void *arg;                    /**< 回调函数参数 */
    GpioIrqMode mode;             /**< 中断触发模式 */
} GpioIrqHdr;

/**
 * @brief  GPIO 控制器
 * @details 每个控制器（片上端口或外部扩展器）注册为独立 Device。
 *          BSP 层通过 container_of 或 priv 指针访问私有数据。
 */
struct GpioController {
    Device parent;                /**< 内嵌 Device，参与设备链表管理 */
    const GpioOps *ops;           /**< BSP 注入的硬件操作函数表 */
    uint8_t pin_count;            /**< 管理的引脚数量 */
    uint32_t caps;                /**< IRQ 能力位图，见 GPIO_CAP_IRQ_xxx */
    void *priv;                   /**< BSP 私有数据指针 */
    GpioIrqHdr *irq_hdrs;         /**< 中断回调表（框架层管理） */
};

/**
 * @brief  BSP 硬件操作函数表
 * @details BSP 层实现此函数表并通过 gpio_controller_register() 注入到控制器。
 *
 * **通用约定**：
 * - `ctrl`   — 当前控制器指针，通过 `ctrl->parent.handle` 或 `ctrl->priv` 获取 BSP 私有数据
 * - `offset` — 控制器内引脚偏移，框架已校验范围 [0, pin_count)，BSP 无需再次校验
 * - 引脚级 ops 为**必选**（注册时校验非 NULL），端口级 ops 为可选（NULL = 不支持）
 * - 所有回调需**同步完成**
 *
 * **物理电平约定**：
 * - 框架层在调用 ops 前已完成 ACTIVE_LOW 反转，BSP 收到的 value 均为**物理电平**
 * - BSP 返回给框架的 read 值也必须是**物理电平**，由框架负责向用户呈现逻辑电平
 *
 * **调用上下文**：
 * - pin_write / pin_read / pin_toggle — 可能在**线程或 ISR** 中被调用，禁止阻塞
 * - pin_configure / pin_attach_irq / pin_irq_enable — 仅在**线程**上下文调用
 * - 端口级 ops — 仅在**线程**上下文调用
 */
struct GpioOps {

    /**
     * @brief  配置引脚电气属性
     *
     * @details 一次性设置引脚方向、上下拉、驱动模式、驱动强度和初始电平。
     *          cfg 为栈上副本，BSP 可直接读取无需拷贝；函数返回后指针失效。
     *          对于输出模式，cfg->init_high 已由框架层完成 ACTIVE_LOW 反转，
     *          BSP 应将此值作为物理初始电平直接写入后切换模式，避免毛刺。
     *
     * @param[in] ctrl   当前控制器
     * @param[in] offset 引脚偏移 [0, pin_count)
     * @param[in] cfg    电气配置（物理电平语义）
     * @return OM_OK 成功，OM_ENOTSUP 不支持请求的配置（如 speed），
     *         其他负值表示硬件错误
     *
     * @note 调用后引脚应立即处于 cfg 所描述的状态，无中间态
     */
    OmRet (*pin_configure)(GpioController *ctrl, uint8_t offset,
                           const GpioPinConfig *cfg);

    /**
     * @brief  写引脚物理电平
     *
     * @details 将引脚驱动为指定物理电平。可能在 ISR 中被高频调用，
     *          实现应使用硬件原子操作，避免 read-modify-write。
     *
     * @param[in] ctrl   当前控制器
     * @param[in] offset 引脚偏移 [0, pin_count)
     * @param[in] value  物理电平：0 = 低，非 0 = 高
     *
     * @note 可在 ISR 上下文调用，禁止阻塞
     */
    void (*pin_write)(GpioController *ctrl, uint8_t offset, uint8_t value);

    /**
     * @brief  读引脚物理电平
     *
     * @details 读取引脚当前物理电平。对输出引脚应读实际引脚状态，
     *          而非输出寄存器，以反映外部电路的影响。
     *
     * @param[in] ctrl   当前控制器
     * @param[in] offset 引脚偏移 [0, pin_count)
     * @return 0 物理低电平，1 物理高电平
     *
     * @note 可在 ISR 上下文调用，禁止阻塞
     */
    uint8_t (*pin_read)(GpioController *ctrl, uint8_t offset);

    /**
     * @brief  翻转引脚物理电平
     *
     * @details 将引脚物理电平取反。若硬件支持原子翻转应直接使用，
     *          否则等效于 read → write 反值。
     *
     * @param[in] ctrl   当前控制器
     * @param[in] offset 引脚偏移 [0, pin_count)
     *
     * @note 可在 ISR 上下文调用，禁止阻塞
     */
    void (*pin_toggle)(GpioController *ctrl, uint8_t offset);

    /**
     * @brief  配置中断路由和触发方式
     *
     * @details 将指定引脚的中断线路由到当前控制器，配置边沿/电平触发方式，
     *          并设置中断优先级。**不应修改引脚的电气配置**（方向、上下拉等）。
     *          **不应使能中断**，由 pin_irq_enable 控制。
     *
     * @param[in] ctrl   当前控制器
     * @param[in] offset 引脚偏移 [0, pin_count)
     * @param[in] mode   触发模式（框架已校验 caps，保证此模式受支持）
     * @param[in] cb     框架层 ISR 入口（BSP 可在硬件 ISR 中直接调用 hal_gpio_isr，
     *                    无需使用此参数；保留供非 EXTI 架构使用）
     * @param[in] arg    传递给 hal_gpio_isr 的参数（同 ctrl）
     * @return OM_OK 成功，其他值表示硬件配置失败
     *
     * @note 仅线程上下文调用
     * @note 调用前用户应已通过 pin_configure 将引脚设为输入模式
     */
    OmRet (*pin_attach_irq)(GpioController *ctrl, uint8_t offset,
                            GpioIrqMode mode, void (*cb)(void *), void *arg);

    /**
     * @brief  使能或禁用引脚硬件中断
     *
     * @details 控制 NVIC 中对应中断通道的使能状态。
     *          enable=true 前应已完成 pin_attach_irq 配置。
     *
     * @param[in] ctrl   当前控制器
     * @param[in] offset 引脚偏移 [0, pin_count)
     * @param[in] enable true 使能 NVIC 中断，false 禁用
     * @return OM_OK 成功
     *
     * @note 仅线程上下文调用
     */
    OmRet (*pin_irq_enable)(GpioController *ctrl, uint8_t offset, bool enable);

    /* ===== 端口级（可选，NULL 表示不支持） ===== */

    /**
     * @brief  掩码写入端口
     *
     * @details 仅更新 mask 中为 1 的位：对应位 value=1 置高，value=0 置低。
     *          mask 为 0 的位不受影响。
     *
     * @param[in] ctrl   当前控制器
     * @param[in] mask   写入掩码（bit n=1 表示更新第 n 个引脚）
     * @param[in] value  物理电平值（bit n 对应第 n 个引脚的目标电平）
     * @return OM_OK 成功
     */
    OmRet (*port_write_masked)(GpioController *ctrl, uint32_t mask, uint32_t value);

    /**
     * @brief  原子置位指定引脚
     * @details 将 pins 中为 1 的位对应的引脚物理电平拉高，其他引脚不变。
     * @param[in] ctrl  当前控制器
     * @param[in] pins  目标引脚位掩码
     * @return OM_OK 成功
     */
    OmRet (*port_set_bits)(GpioController *ctrl, uint32_t pins);

    /**
     * @brief  原子清零指定引脚
     * @details 将 pins 中为 1 的位对应的引脚物理电平拉低，其他引脚不变。
     * @param[in] ctrl  当前控制器
     * @param[in] pins  目标引脚位掩码
     * @return OM_OK 成功
     */
    OmRet (*port_clear_bits)(GpioController *ctrl, uint32_t pins);

    /**
     * @brief  翻转指定引脚电平
     * @details 将 pins 中为 1 的位对应的引脚物理电平取反。
     * @warning 若硬件不支持原子翻转（如 STM32F4 的 ODR RMW），
     *          调用者需保证不在多上下文中对同一控制器并发此操作。
     * @param[in] ctrl  当前控制器
     * @param[in] pins  目标引脚位掩码
     * @return OM_OK 成功
     */
    OmRet (*port_toggle_bits)(GpioController *ctrl, uint32_t pins);

    /**
     * @brief  读取整个端口物理电平
     * @param[in] ctrl  当前控制器
     * @return 端口物理电平值（bit n = 第 n 个引脚的电平，0=低 1=高）
     */
    uint32_t (*port_read)(GpioController *ctrl);
};

/**
 * @name 引脚解析与校验
 * @{ */

/**
 * @brief  从编译时描述符解析运行时引脚句柄
 * @param[in] spec  编译时引脚描述符，不可为 NULL
 * @return 运行时引脚句柄；解析失败时 ctrl 为 NULL
 * @note   通常在初始化阶段调用一次，后续复用返回的 #GpioPin
 */
GpioPin gpio_pin_get(const GpioPinSpec *spec);

/**
 * @brief  检查引脚句柄是否有效
 * @param[in] pin  待检查的引脚句柄
 * @return true 有效，false 无效（ctrl 为 NULL）
 */
bool gpio_pin_valid(GpioPin pin);

/** @} */

/**
 * @name 引脚级操作
 * @{ */

/**
 * @brief  配置引脚电气属性
 * @param[in] pin   有效的引脚句柄
 * @param[in] cfg   配置参数，不可为 NULL
 * @return OM_OK 成功，其他值见错误码
 * @note   调用者需保证与同一引脚的 write/read 不并发
 */
OmRet gpio_pin_configure(GpioPin pin, const GpioPinConfig *cfg);

/**
 * @brief  写引脚电平
 * @param[in] pin    有效的引脚句柄
 * @param[in] value  0 为低电平，非 0 为高电平
 * @note   若引脚设置了 #GPIO_FLAG_ACTIVE_LOW，物理电平自动反转
 * @note   无锁操作，单寄存器写入在硬件层保证原子性
 */
void gpio_pin_write(GpioPin pin, uint8_t value);

/**
 * @brief  读引脚电平
 * @param[in] pin  有效的引脚句柄
 * @return 0 低电平，1 高电平
 * @note   若引脚设置了 #GPIO_FLAG_ACTIVE_LOW，返回值自动反转
 */
uint8_t gpio_pin_read(GpioPin pin);

/**
 * @brief  翻转引脚电平
 * @param[in] pin  有效的引脚句柄
 * @note   无锁操作，BSP 层利用硬件原子翻转能力
 */
void gpio_pin_toggle(GpioPin pin);

/** @} */

/**
 * @name 中断管理
 * @{ */

/**
 * @brief  注册中断回调
 * @param[in] pin       有效的引脚句柄
 * @param[in] mode      中断触发模式
 * @param[in] callback  回调函数，不可为 NULL；在 ISR 上下文中执行，
 *                      必须遵守 ISR 约束（不可阻塞、不可耗时）
 * @param[in] arg       回调函数参数
 * @return OM_OK 成功，其他值见错误码
 * @note   此函数仅注册回调，不使能硬件中断。
 *         需随后调用 gpio_pin_irq_enable() 使能中断。
 * @note   内部通过关中断保护回调表，防止与 ISR 并发修改
 */
OmRet gpio_pin_attach_irq(GpioPin pin, GpioIrqMode mode,
                           void (*callback)(void *arg), void *arg);

/**
 * @brief  注销中断回调
 * @param[in] pin  有效的引脚句柄
 * @return OM_OK 成功
 * @note   内部通过关中断保护回调表。
 *         注销后应同步调用 gpio_pin_irq_enable(pin, false) 禁用硬件中断。
 */
OmRet gpio_pin_detach_irq(GpioPin pin);

/**
 * @brief  使能或禁用引脚硬件中断
 * @param[in] pin     有效的引脚句柄
 * @param[in] enable  true 使能，false 禁用
 * @return OM_OK 成功，其他值见错误码
 */
OmRet gpio_pin_irq_enable(GpioPin pin, bool enable);

/** @} */

/**
 * @name 端口级批量操作
 * @details 端口级 API 直接操作硬件寄存器，使用**物理电平**语义：
 *          value 的每一位直接对应引脚的物理电平，不经过 ACTIVE_LOW 反转。
 *          如需逻辑电平操作，请使用引脚级 API（gpio_pin_write 等）。
 *
 *          Zephyr 提供了 gpio_port_set_masked（逻辑）和 _raw 变体（物理），
 *          当前设计出于简洁性考虑仅提供物理接口，后续可按需扩展。
 * @{ */

/**
 * @brief  从控制器名称解析端口句柄
 * @param[in] controller  控制器名称，如 "gpio0"
 * @return 端口句柄；解析失败时 ctrl 为 NULL
 */
GpioPort gpio_port_get(const char *controller);

/**
 * @brief  检查端口句柄是否有效
 * @param[in] port  待检查的端口句柄
 * @return true 有效，false 无效
 */
bool gpio_port_valid(GpioPort port);

/**
 * @brief  掩码写入端口
 * @param[in] port   有效的端口句柄
 * @param[in] mask   写入掩码，仅 mask 为 1 的位被更新
 * @param[in] value  待写入的值
 * @return OM_OK 成功，OM_ERROR_PARAM 控制器不支持此操作或参数无效
 */
OmRet gpio_port_write_masked(GpioPort port, uint32_t mask, uint32_t value);

/**
 * @brief  原子置位指定引脚
 * @param[in] port  有效的端口句柄
 * @param[in] pins  目标引脚位掩码
 * @return OM_OK 成功，OM_ERROR_PARAM 控制器不支持此操作或参数无效
 */
OmRet gpio_port_set_bits(GpioPort port, uint32_t pins);

/**
 * @brief  原子清零指定引脚
 * @param[in] port  有效的端口句柄
 * @param[in] pins  目标引脚位掩码
 * @return OM_OK 成功，OM_ERROR_PARAM 控制器不支持此操作或参数无效
 */
OmRet gpio_port_clear_bits(GpioPort port, uint32_t pins);

/**
 * @brief  原子翻转指定引脚
 * @param[in] port  有效的端口句柄
 * @param[in] pins  目标引脚位掩码
 * @return OM_OK 成功，OM_ERROR_PARAM 控制器不支持此操作或参数无效
 */
OmRet gpio_port_toggle_bits(GpioPort port, uint32_t pins);

/**
 * @brief  读取整个端口电平
 * @param[in] port  有效的端口句柄
 * @return 端口电平值，每位对应一个引脚；控制器不支持时返回 0
 */
uint32_t gpio_port_read(GpioPort port);

/** @} */

/**
 * @name BSP 侧接口
 * @{ */

/**
 * @brief  注册 GPIO 控制器
 * @param[in] ctrl       控制器结构体指针（BSP 层分配），不可为 NULL
 * @param[in] name       控制器名称（如 "gpioa"），不可为 NULL
 * @param[in] pin_count  管理的引脚数量，不可为 0
 * @param[in] caps       IRQ 能力位图，见 GPIO_CAP_IRQ_xxx
 * @param[in] ops        硬件操作函数表，不可为 NULL
 * @param[in] priv       BSP 私有数据指针，可为 NULL
 * @return OM_OK 成功，OM_ERROR_PARAM 参数无效，OM_ERROR_MEMORY 内存不足
 * @note   仅在初始化阶段调用
 */
OmRet gpio_controller_register(GpioController *ctrl, const char *name,
                                uint8_t pin_count,
                                uint32_t caps, const GpioOps *ops, void *priv);

/**
 * @brief  GPIO ISR 框架入口
 * @details BSP 层的 EXTI ISR 中调用此函数，框架负责查回调表并调用用户回调。
 * @param[in] ctrl    发生中断的控制器
 * @param[in] offset  控制器内引脚偏移
 */
void hal_gpio_isr(GpioController *ctrl, uint8_t offset);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* __PAL_GPIO_DEV_H__ */

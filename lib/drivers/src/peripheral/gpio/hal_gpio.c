/**
 * @file    hal_gpio.c
 * @ingroup GPIO
 * @brief   GPIO 框架层实现
 * @details
 * 实现 GPIO PAL 定义的公共 API，包括：
 * - 控制器注册与 pin 号空间管理
 * - 引脚句柄解析（GpioPinSpec → GpioPin）
 * - 引脚级读写/配置/翻转（无锁）
 * - 中断回调表管理（关中断保护）
 * - 端口级批量操作
 *
 * 设计决策详见 lib/drivers/docs/gpio_design.md。
 */

#include "drivers/peripheral/gpio/pal_gpio_dev.h"
#include "osal/osal_core.h"
#include <string.h>

/**
 * @brief  GPIO Device 接口占位
 * @details GPIO 不通过 Device 的 read/write/control 操作，
 *          所有接口函数均为 NULL，仅用于满足 Device 模型注册要求。
 */
static DevInterface gpio_dev_interface = {
    .init    = NULL,
    .open    = NULL,
    .close   = NULL,
    .read    = NULL,
    .write   = NULL,
    .control = NULL,
};

/* ===== 控制器注册 ===== */

/**
 * @brief  注册 GPIO 控制器到设备链表
 * @details 先完整构造 Device（interface/handle/type），再调 device_register。
 *          分配中断回调表、设置操作函数表和能力位图。
 *          成功后控制器可通过 device_find() 查找。
 */
OmRet gpio_controller_register(GpioController *ctrl, const char *name,
                                uint8_t pin_count,
                                uint32_t caps, const GpioOps *ops, void *priv)
{
    if (!ctrl || !name || !ops || pin_count == 0)
        return OM_ERROR_PARAM;

    /* 校验必选 ops（端口级可选，不校验） */
    if (!ops->pin_configure || !ops->pin_write || !ops->pin_read ||
        !ops->pin_toggle || !ops->pin_attach_irq || !ops->pin_irq_enable)
        return OM_ERROR_PARAM;

    GpioIrqHdr *hdrs = (GpioIrqHdr *)osal_malloc(pin_count * sizeof(GpioIrqHdr));
    if (!hdrs)
        return OM_ERROR_MEMORY;
    memset(hdrs, 0, pin_count * sizeof(GpioIrqHdr));

    ctrl->parent.type      = DEVICE_TYPE_GPIO;
    ctrl->parent.handle    = priv;
    ctrl->parent.interface = &gpio_dev_interface;
    ctrl->ops              = ops;
    ctrl->pin_count        = pin_count;
    ctrl->caps             = caps;
    ctrl->priv             = priv;
    ctrl->irq_hdrs         = hdrs;

    OmRet ret = device_register(&ctrl->parent, (char *)name, 0);
    if (ret != OM_OK) {
        ctrl->irq_hdrs = NULL;
        osal_free(hdrs);
        return ret;
    }

    return OM_OK;
}

/* ===== Pin 句柄解析 ===== */

/**
 * @brief  通过 device_find() 查找控制器、校验 offset、写入输出参数
 */
OmRet gpio_pin_get(const GpioPinSpec *spec, GpioPin *pin)
{
    if (!pin)
        return OM_ERROR_PARAM;

    GpioPin invalid = {NULL, 0, 0};
    *pin = invalid;

    if (!spec || !spec->controller)
        return OM_ERROR_PARAM;

    Device *dev = device_find((char *)spec->controller);
    if (!dev || dev->type != DEVICE_TYPE_GPIO)
        return OM_ERROR;

    GpioController *ctrl = (GpioController *)dev;
    if (spec->offset >= ctrl->pin_count)
        return OM_ERROR;

    pin->ctrl   = ctrl;
    pin->offset = spec->offset;
    pin->flags  = spec->flags;
    return OM_OK;
}

/* ===== 引脚级操作（无锁） ===== */

/**
 * @brief  配置引脚电气属性（逻辑电平语义）
 * @details init_high 采用逻辑电平（与 gpio_pin_write 一致），
 *          ACTIVE_LOW 引脚在传递给 BSP 前自动反转物理电平。
 */
OmRet gpio_pin_configure(GpioPin pin, const GpioPinConfig *cfg)
{
    if (!pin.ctrl || !cfg)
        return OM_ERROR_PARAM;

    GpioPinConfig actual = *cfg;
    if ((pin.flags & GPIO_FLAG_ACTIVE_LOW) && cfg->direction == GPIO_DIR_OUTPUT)
        actual.init_high = !cfg->init_high;

    return pin.ctrl->ops->pin_configure(pin.ctrl, pin.offset, &actual);
}

/**
 * @brief  写引脚电平，处理 ACTIVE_LOW 反转
 */
void gpio_pin_write(GpioPin pin, uint8_t value)
{
    if (!pin.ctrl)
        return;
    if (pin.flags & GPIO_FLAG_ACTIVE_LOW)
        value = !value;
    pin.ctrl->ops->pin_write(pin.ctrl, pin.offset, value);
}

/**
 * @brief  读引脚电平，处理 ACTIVE_LOW 反转
 */
uint8_t gpio_pin_read(GpioPin pin)
{
    if (!pin.ctrl)
        return 0;
    uint8_t value = pin.ctrl->ops->pin_read(pin.ctrl, pin.offset);
    if (pin.flags & GPIO_FLAG_ACTIVE_LOW)
        value = !value;
    return value;
}

/**
 * @brief  委托 BSP ops 翻转引脚电平
 */
void gpio_pin_toggle(GpioPin pin)
{
    if (!pin.ctrl)
        return;
    pin.ctrl->ops->pin_toggle(pin.ctrl, pin.offset);
}

/* ===== 中断管理（仅 IRQ 回调表保护） ===== */

/**
 * @brief  校验 caps → 关中断保护回调表 → 委托 BSP ops 注册中断
 * @details 先检查控制器是否支持请求的触发模式（caps 位图），
 *          再关中断保护回调表（赋值仅几条指令，窗口极短）。
 */
OmRet gpio_pin_attach_irq(GpioPin pin, GpioIrqMode mode,
                           void (*callback)(void *arg), void *arg)
{
    if (!pin.ctrl || !callback)
        return OM_ERROR_PARAM;

    if (!(pin.ctrl->caps & (1U << mode)))
        return OM_ERROR_NOT_SUPPORT;

    OsalIrqIsrState key;
    osal_irq_lock(&key);
    pin.ctrl->irq_hdrs[pin.offset].callback = callback;
    pin.ctrl->irq_hdrs[pin.offset].arg      = arg;
    pin.ctrl->irq_hdrs[pin.offset].mode     = mode;
    osal_irq_unlock(key);

    return pin.ctrl->ops->pin_attach_irq(pin.ctrl, pin.offset, mode,
                                          callback, arg);
}

/**
 * @brief  关中断清空回调表项（不操作硬件中断）
 */
OmRet gpio_pin_detach_irq(GpioPin pin)
{
    if (!pin.ctrl)
        return OM_ERROR_PARAM;

    OsalIrqIsrState key;
    osal_irq_lock(&key);
    pin.ctrl->irq_hdrs[pin.offset].callback = NULL;
    pin.ctrl->irq_hdrs[pin.offset].arg      = NULL;
    pin.ctrl->irq_hdrs[pin.offset].mode     = 0;
    osal_irq_unlock(key);

    return OM_OK;
}

/**
 * @brief  委托 BSP ops 使能/禁用硬件中断
 */
OmRet gpio_pin_irq_enable(GpioPin pin, bool enable)
{
    if (!pin.ctrl)
        return OM_ERROR_PARAM;
    return pin.ctrl->ops->pin_irq_enable(pin.ctrl, pin.offset, enable);
}

/* ===== ISR 框架入口（BSP ISR 中调用） ===== */

/**
 * @brief  查 irq_hdrs 表调用用户回调
 * @details BSP 层 EXTI ISR 中调用 hal_gpio_isr()，框架负责查表分发。
 */
void hal_gpio_isr(GpioController *ctrl, uint8_t offset)
{
    if (!ctrl || offset >= ctrl->pin_count)
        return;
    if (ctrl->irq_hdrs[offset].callback)
        ctrl->irq_hdrs[offset].callback(ctrl->irq_hdrs[offset].arg);
}

/* ===== 端口句柄解析 ===== */

/**
 * @brief  通过 device_find() 查找控制器，返回 GpioPort
 */
GpioPort gpio_port_get(const char *controller)
{
    GpioPort invalid = {NULL};
    if (!controller)
        return invalid;

    Device *dev = device_find((char *)controller);
    if (!dev || dev->type != DEVICE_TYPE_GPIO)
        return invalid;

    GpioPort port = {(GpioController *)dev};
    return port;
}

/* ===== 端口级批量操作 ===== */

/**
 * @brief  委托 BSP ops 掩码写入端口
 * @details mask 中超出 pin_count 的位在框架层被截断，BSP 收到的 mask 仅含有效位。
 */
OmRet gpio_port_write_masked(GpioPort port, uint32_t mask, uint32_t value)
{
    if (!port.ctrl || !port.ctrl->ops->port_write_masked)
        return OM_ERROR_PARAM;
    uint32_t valid_mask = (1U << port.ctrl->pin_count) - 1;
    return port.ctrl->ops->port_write_masked(port.ctrl, mask & valid_mask,
                                             value);
}

/**
 * @brief  委托 BSP ops 原子置位引脚
 * @details pins 中超出 pin_count 的位在框架层被截断，BSP 收到的 pins 仅含有效位。
 */
OmRet gpio_port_set_bits(GpioPort port, uint32_t pins)
{
    if (!port.ctrl || !port.ctrl->ops->port_set_bits)
        return OM_ERROR_PARAM;
    uint32_t valid_mask = (1U << port.ctrl->pin_count) - 1;
    return port.ctrl->ops->port_set_bits(port.ctrl, pins & valid_mask);
}

/**
 * @brief  委托 BSP ops 原子清零引脚
 * @details pins 中超出 pin_count 的位在框架层被截断，BSP 收到的 pins 仅含有效位。
 */
OmRet gpio_port_clear_bits(GpioPort port, uint32_t pins)
{
    if (!port.ctrl || !port.ctrl->ops->port_clear_bits)
        return OM_ERROR_PARAM;
    uint32_t valid_mask = (1U << port.ctrl->pin_count) - 1;
    return port.ctrl->ops->port_clear_bits(port.ctrl, pins & valid_mask);
}

/**
 * @brief  委托 BSP ops 原子翻转引脚
 * @details pins 中超出 pin_count 的位在框架层被截断，BSP 收到的 pins 仅含有效位。
 */
OmRet gpio_port_toggle_bits(GpioPort port, uint32_t pins)
{
    if (!port.ctrl || !port.ctrl->ops->port_toggle_bits)
        return OM_ERROR_PARAM;
    uint32_t valid_mask = (1U << port.ctrl->pin_count) - 1;
    return port.ctrl->ops->port_toggle_bits(port.ctrl, pins & valid_mask);
}

/**
 * @brief  委托 BSP ops 读取端口电平
 * @details 返回值为 BSP 返回值经 pin_count 掩码截断，高位噪声已清除。
 */
uint32_t gpio_port_read(GpioPort port)
{
    if (!port.ctrl || !port.ctrl->ops->port_read)
        return 0;
    uint32_t valid_mask = (1U << port.ctrl->pin_count) - 1;
    return port.ctrl->ops->port_read(port.ctrl) & valid_mask;
}

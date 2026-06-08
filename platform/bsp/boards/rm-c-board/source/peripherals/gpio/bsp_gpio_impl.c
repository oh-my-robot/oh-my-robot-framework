/**
 * @file    bsp_gpio_impl.c
 * @brief   rm-c-board GPIO BSP 实现（STM32F407）
 */

#include "bsp_gpio.h"

/* ===== STM32 EXTI IRQ 通道映射 ===== */

static IRQn_Type bsp_gpio_exti_irqn(uint8_t pin)
{
    if (pin <= 4)
        return (IRQn_Type)(EXTI0_IRQn + pin);
    if (pin <= 9)
        return EXTI9_5_IRQn;
    return EXTI15_10_IRQn;
}

/** 计算 GPIO 端口在 EXTICR 中的索引（GPIOA=0, GPIOB=1, ...） */
static uint8_t bsp_gpio_port_index(GPIO_TypeDef *port)
{
    return (uint8_t)(((uint32_t)port - (uint32_t)GPIOA) / 0x0400U);
}

/* ===== GpioOps 实现 ===== */

static OmRet bsp_gpio_pin_configure(GpioController *ctrl, uint8_t offset,
                                     const GpioPinConfig *cfg)
{
    BspGpio *bsp = (BspGpio *)ctrl->parent.handle;
    GPIO_InitTypeDef init = {0};

    init.Pin = (uint16_t)(1U << offset);

    /* 方向 + 驱动模式 */
    if (cfg->direction == GPIO_DIR_OUTPUT) {
        init.Mode = (cfg->drive == GPIO_DRIVE_OPEN_DRAIN)
                        ? GPIO_MODE_OUTPUT_OD
                        : GPIO_MODE_OUTPUT_PP;
        init.Speed = (cfg->speed == GPIO_SPEED_HIGH)    ? GPIO_SPEED_FREQ_VERY_HIGH
                   : (cfg->speed == GPIO_SPEED_MEDIUM) ? GPIO_SPEED_FREQ_MEDIUM
                                                       : GPIO_SPEED_FREQ_LOW;
        if (cfg->init_high)
            HAL_GPIO_WritePin(bsp->port, init.Pin, GPIO_PIN_SET);
        else
            HAL_GPIO_WritePin(bsp->port, init.Pin, GPIO_PIN_RESET);
    } else {
        init.Mode = GPIO_MODE_INPUT;
    }

    /* 上下拉 */
    init.Pull = (cfg->pull == GPIO_PULL_UP)  ? GPIO_PULLUP
              : (cfg->pull == GPIO_PULL_DOWN) ? GPIO_PULLDOWN
                                              : GPIO_NOPULL;

    HAL_GPIO_Init(bsp->port, &init);
    return OM_OK;
}

static void bsp_gpio_pin_write(GpioController *ctrl, uint8_t offset,
                                uint8_t value)
{
    BspGpio *bsp = (BspGpio *)ctrl->parent.handle;
    HAL_GPIO_WritePin(bsp->port, (uint16_t)(1U << offset),
                      value ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint8_t bsp_gpio_pin_read(GpioController *ctrl, uint8_t offset)
{
    BspGpio *bsp = (BspGpio *)ctrl->parent.handle;
    return (HAL_GPIO_ReadPin(bsp->port, (uint16_t)(1U << offset)) == GPIO_PIN_SET)
               ? 1 : 0;
}

static void bsp_gpio_pin_toggle(GpioController *ctrl, uint8_t offset)
{
    BspGpio *bsp = (BspGpio *)ctrl->parent.handle;
    HAL_GPIO_TogglePin(bsp->port, (uint16_t)(1U << offset));
}

/**
 * @brief  配置 EXTI 路由和 NVIC（不调用 HAL_GPIO_Init，不修改电气配置）
 * @details 仅操作 EXTI 相关寄存器（EXTICR/RTSR/FTSR/IMR），
 *          保留用户通过 gpio_pin_configure 设置的 pull 和 mode。
 */
static OmRet bsp_gpio_pin_attach_irq(GpioController *ctrl, uint8_t offset,
                                      GpioIrqMode mode,
                                      void (*cb)(void *), void *arg)
{
    (void)cb;
    (void)arg;
    BspGpio *bsp = (BspGpio *)ctrl->parent.handle;

    /* 配置 SYSCFG EXTICR：将 EXTI 线路由到当前端口 */
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    volatile uint32_t *exticr = &SYSCFG->EXTICR[offset / 4];
    uint32_t shift = (offset % 4) * 4;
    uint32_t port_val = bsp_gpio_port_index(bsp->port);
    *exticr = (*exticr & ~(0xFU << shift)) | (port_val << shift);

    /* 配置 EXTI 边沿触发（不触碰 PUPDR/MODER） */
    uint32_t bitmask = 1U << offset;
    switch (mode) {
    case GPIO_IRQ_EDGE_RISING:
        EXTI->RTSR |= bitmask;
        EXTI->FTSR &= ~bitmask;
        break;
    case GPIO_IRQ_EDGE_FALLING:
        EXTI->FTSR |= bitmask;
        EXTI->RTSR &= ~bitmask;
        break;
    case GPIO_IRQ_EDGE_BOTH:
        EXTI->RTSR |= bitmask;
        EXTI->FTSR |= bitmask;
        break;
    default:
        return OM_ERROR_NOT_SUPPORT;
    }

    /* 使能 EXTI 中断掩码 */
    EXTI->IMR |= bitmask;

    /* 配置 NVIC 优先级（不使能，由 irq_enable 控制） */
    IRQn_Type irqn = bsp_gpio_exti_irqn(offset);
    HAL_NVIC_SetPriority(irqn, bsp->irq_priority, 0);

    return OM_OK;
}

static OmRet bsp_gpio_pin_irq_enable(GpioController *ctrl, uint8_t offset,
                                      bool enable)
{
    (void)ctrl;
    IRQn_Type irqn = bsp_gpio_exti_irqn(offset);
    if (enable)
        HAL_NVIC_EnableIRQ(irqn);
    else
        HAL_NVIC_DisableIRQ(irqn);
    return OM_OK;
}

/* ===== 端口级操作（直接寄存器） ===== */

static OmRet bsp_gpio_port_write_masked(GpioController *ctrl, uint32_t mask,
                                         uint32_t value)
{
    BspGpio *bsp = (BspGpio *)ctrl->parent.handle;
    bsp->port->BSRR = ((~value & mask) << 16) | (value & mask);
    return OM_OK;
}

static OmRet bsp_gpio_port_set_bits(GpioController *ctrl, uint32_t pins)
{
    BspGpio *bsp = (BspGpio *)ctrl->parent.handle;
    bsp->port->BSRR = pins;
    return OM_OK;
}

static OmRet bsp_gpio_port_clear_bits(GpioController *ctrl, uint32_t pins)
{
    BspGpio *bsp = (BspGpio *)ctrl->parent.handle;
    bsp->port->BSRR = pins << 16;
    return OM_OK;
}

static OmRet bsp_gpio_port_toggle_bits(GpioController *ctrl, uint32_t pins)
{
    BspGpio *bsp = (BspGpio *)ctrl->parent.handle;
    bsp->port->ODR ^= pins;
    return OM_OK;
}

static uint32_t bsp_gpio_port_read(GpioController *ctrl)
{
    BspGpio *bsp = (BspGpio *)ctrl->parent.handle;
    return bsp->port->IDR;
}

/* ===== GpioOps 实例 ===== */

static const GpioOps gGpioOps = {
    .pin_configure  = bsp_gpio_pin_configure,
    .pin_write      = bsp_gpio_pin_write,
    .pin_read       = bsp_gpio_pin_read,
    .pin_toggle     = bsp_gpio_pin_toggle,
    .pin_attach_irq = bsp_gpio_pin_attach_irq,
    .pin_irq_enable = bsp_gpio_pin_irq_enable,
    .port_write_masked = bsp_gpio_port_write_masked,
    .port_set_bits     = bsp_gpio_port_set_bits,
    .port_clear_bits   = bsp_gpio_port_clear_bits,
    .port_toggle_bits  = bsp_gpio_port_toggle_bits,
    .port_read         = bsp_gpio_port_read,
};

/* ===== BSP 实例数组 ===== */

BspGpio gBspGpio[BSP_GPIO_PORT_COUNT] = {
    {GPIOA, {0}, "gpioa", BSP_GPIO_IRQ_PRIORITY},
    {GPIOB, {0}, "gpiob", BSP_GPIO_IRQ_PRIORITY},
    {GPIOC, {0}, "gpioc", BSP_GPIO_IRQ_PRIORITY},
    {GPIOD, {0}, "gpiod", BSP_GPIO_IRQ_PRIORITY},
    {GPIOE, {0}, "gpioe", BSP_GPIO_IRQ_PRIORITY},
    {GPIOF, {0}, "gpiof", BSP_GPIO_IRQ_PRIORITY},
    {GPIOG, {0}, "gpiog", BSP_GPIO_IRQ_PRIORITY},
    {GPIOH, {0}, "gpioh", BSP_GPIO_IRQ_PRIORITY},
    {GPIOI, {0}, "gpioi", BSP_GPIO_IRQ_PRIORITY},
};

/* ===== 注册入口 ===== */

void bsp_gpio_register(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    for (uint8_t i = 0; i < BSP_GPIO_PORT_COUNT; i++) {
        gpio_controller_register(
            &gBspGpio[i].parent,
            gBspGpio[i].name,
            BSP_GPIO_PINS_PER_PORT,
            BSP_GPIO_IRQ_CAPS,
            &gGpioOps,
            &gBspGpio[i]);
    }
}

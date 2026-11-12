/**
 * @file bsp_gpio_impl.c
 * @brief MSPM0G3507 GpioOps 实现 + 注册 (DL_GPIO)
 */

#include "bsp_gpio.h"
#include "ti/driverlib/dl_gpio.h"

/*---------------------------------------------------------------------------*/
/* pin_configure                                                             */
/*---------------------------------------------------------------------------*/

static OmRet bsp_pin_configure(GpioController *ctrl, uint8_t offset,
                                const GpioPinConfig *cfg)
{
    BspGpio *g = (BspGpio *)ctrl->parent.handle;
    uint32_t pin = (1UL << offset);

    /* 先写初始电平再切方向，避免输出毛刺 */
    if (cfg->direction == GPIO_DIR_OUTPUT) {
        if (cfg->init_high)
            DL_GPIO_setPins(g->port, pin);
        else
            DL_GPIO_clearPins(g->port, pin);
        DL_GPIO_enableOutput(g->port, pin);
    } else {
        DL_GPIO_disableOutput(g->port, pin);
    }

    /* 上下拉由 SysConfig 预配，运行时不变更 */
    return OM_OK;
}

/*---------------------------------------------------------------------------*/
/* pin_write / pin_read / pin_toggle (ISR 安全)                               */
/*---------------------------------------------------------------------------*/

static void bsp_pin_write(GpioController *ctrl, uint8_t offset, uint8_t value)
{
    uint32_t pin = (1UL << offset);
    BspGpio *g = (BspGpio *)ctrl->parent.handle;
    if (value)
        DL_GPIO_setPins(g->port, pin);
    else
        DL_GPIO_clearPins(g->port, pin);
}

static uint8_t bsp_pin_read(GpioController *ctrl, uint8_t offset)
{
    uint32_t pin = (1UL << offset);
    BspGpio *g = (BspGpio *)ctrl->parent.handle;
    return DL_GPIO_readPins(g->port, pin) ? 1U : 0U;
}

static void bsp_pin_toggle(GpioController *ctrl, uint8_t offset)
{
    uint32_t pin = (1UL << offset);
    BspGpio *g = (BspGpio *)ctrl->parent.handle;
    DL_GPIO_togglePins(g->port, pin);
}

/*---------------------------------------------------------------------------*/
/* pin_attach_irq / pin_irq_enable (暂桩，无实际 IRQ 功能)                     */
/*---------------------------------------------------------------------------*/

static OmRet bsp_pin_attach_irq(GpioController *ctrl, uint8_t offset,
                                 GpioIrqMode mode, void (*cb)(void *), void *arg)
{
    (void)ctrl; (void)offset; (void)mode; (void)cb; (void)arg;
    return OM_ERR_NOT_SUPPORTED;
}

static OmRet bsp_pin_irq_enable(GpioController *ctrl, uint8_t offset, bool en)
{
    (void)ctrl; (void)offset; (void)en;
    return OM_ERR_NOT_SUPPORTED;
}

/*---------------------------------------------------------------------------*/
/* GpioOps 函数表                                                             */
/*---------------------------------------------------------------------------*/

static const GpioOps gGpioOps = {
    .pin_configure   = bsp_pin_configure,
    .pin_write       = bsp_pin_write,
    .pin_read        = bsp_pin_read,
    .pin_toggle      = bsp_pin_toggle,
    .pin_attach_irq  = bsp_pin_attach_irq,
    .pin_irq_enable  = bsp_pin_irq_enable,
};

/*---------------------------------------------------------------------------*/
/* 实例 + 注册                                                                */
/*---------------------------------------------------------------------------*/

static BspGpio gBspGpio[BSP_GPIO_PORT_COUNT] = {
    { .port = GPIOA, .name = "gpioa", .pin_count = 32 },  /* PA0-31 */
    { .port = GPIOB, .name = "gpiob", .pin_count = 28 },  /* PB0-27 */
};

void bsp_gpio_register(void)
{
    for (uint8_t i = 0; i < BSP_GPIO_PORT_COUNT; i++) {
        gpio_controller_register(&gBspGpio[i].parent,
                                  gBspGpio[i].name,
                                  gBspGpio[i].pin_count,
                                  0,
                                  &gGpioOps,
                                  &gBspGpio[i]);
    }
}

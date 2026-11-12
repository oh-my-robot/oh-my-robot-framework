/**
 * @file   host_gpio_fake.c
 * @brief  可控 GPIO 控制器实现
 */

#include "host_gpio_fake.h"

#include <string.h>

static HostGpioFake *fake_of(GpioController *ctrl)
{
    return (HostGpioFake *)ctrl->priv;
}

static OmRet fake_pin_configure(GpioController *ctrl, uint8_t offset,
                                const GpioPinConfig *cfg)
{
    HostGpioFake *f = fake_of(ctrl);
    if (offset >= HOST_GPIO_FAKE_PINS || !cfg)
    {
        return OM_ERR_INVALID_ARG;
    }
    f->configureCalls++;
    f->lastDirection = cfg->direction;
    f->lastInitHigh = cfg->init_high;
    f->lastPushPull = (cfg->drive == GPIO_DRIVE_PUSH_PULL);
    f->levels[offset] = cfg->init_high ? 1u : 0u;
    return OM_OK;
}

static void fake_pin_write(GpioController *ctrl, uint8_t offset, uint8_t value)
{
    HostGpioFake *f = fake_of(ctrl);
    if (offset >= HOST_GPIO_FAKE_PINS)
    {
        return;
    }
    f->writeCalls++;
    f->levels[offset] = value ? 1u : 0u;
    if (f->csSim && offset == f->csOffset)
    {
        /* 引脚物理低 = 片选断言（低有效由片选时序承担，此处只见物理电平） */
        spi_nor_sim_cs_edge(f->csSim, value == 0u);
    }
}

static uint8_t fake_pin_read(GpioController *ctrl, uint8_t offset)
{
    HostGpioFake *f = fake_of(ctrl);
    return (offset < HOST_GPIO_FAKE_PINS) ? f->levels[offset] : 0u;
}

static void fake_pin_toggle(GpioController *ctrl, uint8_t offset)
{
    HostGpioFake *f = fake_of(ctrl);
    if (offset < HOST_GPIO_FAKE_PINS)
    {
        fake_pin_write(ctrl, offset, (uint8_t)(f->levels[offset] ? 0u : 1u));
    }
}

static OmRet fake_pin_attach_irq(GpioController *ctrl, uint8_t offset, GpioIrqMode mode,
                                 void (*cb)(void *), void *arg)
{
    (void)ctrl;
    (void)offset;
    (void)mode;
    (void)cb;
    (void)arg;
    return OM_ERR_NOT_SUPPORTED;
}

static OmRet fake_pin_irq_enable(GpioController *ctrl, uint8_t offset, bool enable)
{
    (void)ctrl;
    (void)offset;
    (void)enable;
    return OM_ERR_NOT_SUPPORTED;
}

OmRet host_gpio_fake_register(HostGpioFake *fake, const char *name, SpiNorSim *cs_sim,
                              uint8_t cs_offset)
{
    if (!fake || !name)
    {
        return OM_ERR_INVALID_ARG;
    }
    memset(fake, 0, sizeof(*fake));
    fake->csSim = cs_sim;
    fake->csOffset = cs_offset;

    fake->ops.pin_configure = fake_pin_configure;
    fake->ops.pin_write = fake_pin_write;
    fake->ops.pin_read = fake_pin_read;
    fake->ops.pin_toggle = fake_pin_toggle;
    fake->ops.pin_attach_irq = fake_pin_attach_irq;
    fake->ops.pin_irq_enable = fake_pin_irq_enable;

    return gpio_controller_register(&fake->ctrl, name, HOST_GPIO_FAKE_PINS, 0u, &fake->ops,
                                    fake);
}

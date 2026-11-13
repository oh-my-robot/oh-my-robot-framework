#ifndef __PAL_DEV_H__
#define __PAL_DEV_H__

#include "core/om_config.h"
#include "drivers/model/device.h"

#ifdef OM_USE_HAL_SERIALS
#include "drivers/peripheral/serial/pal_serial_dev.h"
#endif

#ifdef OM_USE_HAL_CAN
#include "drivers/peripheral/can/pal_can_dev.h"
#endif

#include "drivers/peripheral/gpio/pal_gpio_dev.h"

#ifdef OM_USE_HAL_SPI
#include "drivers/peripheral/spi/pal_spi_dev.h"
#endif

#ifdef OM_USE_HAL_PWM
#include "drivers/peripheral/pwm/pal_pwm_dev.h"
#endif

#include "drivers/peripheral/flash/pal_flash_dev.h"

#endif // __PAL_DEV_H__

#ifndef __PAL_DEV_H__
#define __PAL_DEV_H__

#include "core/om_config.h"
#include "drivers/model/device.h"

#ifdef __OM_USE_HAL_SERIALS
#include "serial/pal_serial_dev.h"
#endif

#ifdef __OM_USE_HAL_CAN
#include "can/pal_can_dev.h"
#endif

#include "gpio/pal_gpio_dev.h"

#ifdef OM_USE_HAL_SPI
#include "spi/pal_spi_dev.h"
#endif

#ifdef OM_USE_HAL_PWM
#include "pwm/pal_pwm_dev.h"
#endif

#endif // __PAL_DEV_H__

#ifndef __BSP_H__
#define __BSP_H__
#ifdef OM_USE_BOARDCFG
#include "om_boardcfg.h" /* 工程板级覆写（boardcfg 契约见 ADR-0017）——须在默认值定义之前 */
#endif

#include "bsp_can.h"
#include "bsp_dwt.h"
#include "bsp_gpio.h"
#include "bsp_serial.h"
#include "bsp_pwm.h"
#include "bsp_spi.h"

#include "core/om_cpu.h"
#include "stm32f4xx_hal.h"

extern uint32_t SystemCoreClock;

// TODO: 板级版本管理
#define __OM_BOARD_VERSION "1.0.0"
#define __OM_CPU_FREQ_MHZ (SystemCoreClock / 1000000U)

#endif

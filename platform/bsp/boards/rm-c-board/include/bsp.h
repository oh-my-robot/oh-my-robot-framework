#ifndef __BSP_H__
#define __BSP_H__

#include "bsp_can.h"
#include "bsp_dwt.h"
#include "bsp_serial.h"

#include "core/om_cpu.h"
#include "stm32f4xx_hal.h"

extern uint32_t SystemCoreClock;

// TODO: 板级版本管理
#define __OM_BOARD_VERSION "1.0.0"
#define __OM_CPU_FREQ_MHZ (SystemCoreClock / 1000000U)

#endif

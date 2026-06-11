#ifndef __DM_MC_BOARD_BSP_H__
#define __DM_MC_BOARD_BSP_H__

#include "bsp_can.h"
#include "bsp_dwt.h"
#include "bsp_serial.h"

#include "core/om_cpu.h"
#include "stm32h7xx_hal.h"

extern uint32_t SystemCoreClock;

#define __OM_BOARD_VERSION "0.1.0"
#define __OM_CPU_FREQ_MHZ (SystemCoreClock / 1000000U)

#endif

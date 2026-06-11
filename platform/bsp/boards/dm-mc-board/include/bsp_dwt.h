#ifndef __DM_MC_BOARD_DWT_H__
#define __DM_MC_BOARD_DWT_H__

#include "stm32h7xx_hal.h"
#include <stdint.h>

typedef struct DwtTime
{
    uint32_t s;
    uint16_t ms;
    uint16_t us;
} DwtTime_s;

void DWT_Init(uint32_t CPU_Freq_mHz);
float DWT_GetDeltaT(uint32_t* cnt_last);
double DWT_GetDeltaT64(uint32_t* cnt_last);
float DWT_GetTimeline_s(void);
float DWT_GetTimeline_ms(void);
uint64_t DWT_GetTimeline_us(void);
void DWT_Delay(float Delay);
void DWT_SysTimeUpdate(void);

#endif

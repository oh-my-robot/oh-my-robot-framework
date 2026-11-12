/**
 ******************************************************************************
 * @file	bsp_dwt.c
 * @author  Wang Hongxi
 * @author  modified by NeoZng 2022/3/8
 * @modifier:  Yuhao  2025/12/1
 * @date    2025/12/1
 * @brief   Cortex-M 周期计数器（DWT CYCCNT）时间服务实现（arch/cortex-m 共享）
 * @details 纯 ARMv7-M 内核寄存器操作，M3/M4/M7 家族通用。
 *          零设备头依赖：DWT（base 0xE0001000）与 DEMCR（0xE000EDFC）是 ARM 架构
 *          规范（ARM ARM DDI 0403）固定的内核寄存器地址，本文件自管寄存器映射
 *          （core_cm4.h 非自包含——需设备头先定义 IRQn_Type/__FPU_PRESENT，故不采用）。
 *          接入：板 lua sources 引用本文件（板瘦身 opt-in 铁律）。
 ******************************************************************************
 */

#include "bsp_dwt.h"

/*===========================================================================
 * Cortex-M 内核寄存器映射（ARMv7-M 架构规范，非厂商 HAL；仅本 TU 使用）
 *===========================================================================*/

/** @brief DWT 数据观察点与跟踪单元寄存器（DWT base = 0xE0001000） */
typedef struct
{
    volatile uint32_t CTRL;      /* 0x00 控制寄存器（CYCCNTENA=bit0） */
    volatile uint32_t CYCCNT;    /* 0x04 周期计数器 */
    volatile uint32_t CPICNT;    /* 0x08 */
    volatile uint32_t EXCCNT;    /* 0x0C */
    volatile uint32_t SLEEPCNT;  /* 0x10 */
    volatile uint32_t LSUCNT;    /* 0x14 */
    volatile uint32_t FOLDCNT;   /* 0x18 */
    volatile uint32_t PCSR;      /* 0x1C */
} OmDwtRegs;

#define OM_DWT                  (*(volatile OmDwtRegs *)0xE0001000UL)
#define OM_DWT_CTRL_CYCCNTENA   (1UL << 0)

/** @brief 调试异常与监控控制寄存器（DEMCR，base = 0xE000EDFC；TRCENA=bit24） */
#define OM_DEMCR                (*(volatile uint32_t *)0xE000EDFCUL)
#define OM_DEMCR_TRCENA         (1UL << 24)

static DwtTime_s SysTime;
static uint32_t CPU_FREQ_Hz, CPU_FREQ_Hz_ms, CPU_FREQ_Hz_us;
static uint32_t CYCCNT_RountCount;
static uint32_t CYCCNT_LAST;
static uint64_t CYCCNT64;

/**
 * @brief 私有函数,用于检查DWT CYCCNT寄存器是否溢出,并更新CYCCNT_RountCount
 * @attention 此函数假设两次调用之间的时间间隔不超过一次溢出
 *
 * @todo 更好的方案是为dwt的时间更新单独设置一个任务?
 *       不过,使用dwt的初衷是定时不被中断/任务等因素影响,因此该实现仍然有其存在的意义
 *
 */
static void DWT_CNT_Update(void)
{
    static volatile uint8_t bit_locker = 0;
    if (!bit_locker)
    {
        bit_locker = 1;
        volatile uint32_t cnt_now = OM_DWT.CYCCNT;
        if (cnt_now < CYCCNT_LAST)
            CYCCNT_RountCount++;

        CYCCNT_LAST = OM_DWT.CYCCNT;
        bit_locker = 0;
    }
}

void DWT_Init(uint32_t CPU_Freq_mHz)
{
    /* 使能DWT外设 */
    OM_DEMCR |= OM_DEMCR_TRCENA;

    /* DWT CYCCNT寄存器计数清0 */
    OM_DWT.CYCCNT = (uint32_t)0u;

    /* 使能Cortex-M DWT CYCCNT寄存器 */
    OM_DWT.CTRL |= OM_DWT_CTRL_CYCCNTENA;

    CPU_FREQ_Hz = CPU_Freq_mHz * 1000000;
    CPU_FREQ_Hz_ms = CPU_FREQ_Hz / 1000;
    CPU_FREQ_Hz_us = CPU_FREQ_Hz / 1000000;
    CYCCNT_RountCount = 0;

    DWT_CNT_Update();
}

float DWT_GetDeltaT(uint32_t* cnt_last)
{
    volatile uint32_t cnt_now = OM_DWT.CYCCNT;
    float dt = ((uint32_t)(cnt_now - *cnt_last)) / ((float)(CPU_FREQ_Hz));
    *cnt_last = cnt_now;

    DWT_CNT_Update();

    return dt;
}

double DWT_GetDeltaT64(uint32_t* cnt_last)
{
    volatile uint32_t cnt_now = OM_DWT.CYCCNT;
    double dt = ((uint32_t)(cnt_now - *cnt_last)) / ((double)(CPU_FREQ_Hz));
    *cnt_last = cnt_now;

    DWT_CNT_Update();

    return dt;
}

void DWT_SysTimeUpdate(void)
{
    volatile uint32_t cnt_now = OM_DWT.CYCCNT;
    static uint64_t CNT_TEMP1, CNT_TEMP2, CNT_TEMP3;

    DWT_CNT_Update();

    CYCCNT64 = (uint64_t)CYCCNT_RountCount * (uint64_t)UINT32_MAX + (uint64_t)cnt_now;
    CNT_TEMP1 = CYCCNT64 / CPU_FREQ_Hz;
    CNT_TEMP2 = CYCCNT64 - CNT_TEMP1 * CPU_FREQ_Hz;
    SysTime.s = CNT_TEMP1;
    SysTime.ms = CNT_TEMP2 / CPU_FREQ_Hz_ms;
    CNT_TEMP3 = CNT_TEMP2 - SysTime.ms * CPU_FREQ_Hz_ms;
    SysTime.us = CNT_TEMP3 / CPU_FREQ_Hz_us;
}

float DWT_GetTimeline_s(void)
{
    DWT_SysTimeUpdate();

    float DWT_Timelinef32 = SysTime.s + SysTime.ms * 0.001f + SysTime.us * 0.000001f;

    return DWT_Timelinef32;
}

float DWT_GetTimeline_ms(void)
{
    DWT_SysTimeUpdate();

    float DWT_Timelinef32 = SysTime.s * 1000 + SysTime.ms + SysTime.us * 0.001f;

    return DWT_Timelinef32;
}

uint64_t DWT_GetTimeline_us(void)
{
    DWT_SysTimeUpdate();

    uint64_t DWT_Timelinef32 = SysTime.s * 1000000 + SysTime.ms * 1000 + SysTime.us;

    return DWT_Timelinef32;
}

void DWT_Delay(float Delay)
{
    uint32_t tickstart = OM_DWT.CYCCNT;
    float wait = Delay;

    while ((OM_DWT.CYCCNT - tickstart) < wait * (float)CPU_FREQ_Hz)
        ;
}

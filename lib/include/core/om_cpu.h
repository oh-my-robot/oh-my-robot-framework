/**
 * @file  om_board.h
 * @brief 板级函数接口
 * @note  1、本文件规范了不同开发板的行为，以便于工程迁移
 *        2、本文件作为一个规范，仅包含函数声明，具体实现请浏览platform/board/om_board.c
 */

#ifndef __OM_BOARD_H__
#define OM_BOARD_H

#include "core/om_config.h"
#include "core/om_def.h"
#include <stdint.h>

/* CPU 配置 */
#define OM_CPU_NAME "DJI_C" // CPU名称，如"DJI_C"
typedef uint32_t CputimeCnt;   // CPU时间计数器类型，由此计数类型，通过换算关系计算得到以下时间类型
typedef float Cputime;        // CPU时间类型，单位：s
typedef float CputimeMs;       // CPU时间类型，单位：ms
typedef uint64_t CputimeUs;    // CPU时间类型，单位：us

typedef struct OmCpu OmCpu;

typedef struct OmBoardInterface OmBoardInterface;
typedef struct OmBoardInterface
{
    void (*errhandler)(void);
    void (*reset)(void);
    Cputime (*getCpuTimeS)(void);                           // 获取CPU时间，单位：s
    CputimeMs (*getCpuTimeMs)(void);                         // 获取CPU时间，单位：ms
    CputimeUs (*getCpuTimeUs)(void);                         // 获取CPU时间，单位：us
    Cputime (*getDeltaCpuTimeS)(CputimeCnt* last_time_cnt); // 获取CPU时间差，单位：s
    void (*delayMs)(float ms);                                  // 延时ms
} OmBoardInterface;

typedef struct OmCpu
{
    char* cpuName;           // CPU名称，如"DJI_C"
    char* omVersion;       // OM版本，目前采用时间制，如"2025-12-1"
    CputimeCnt lastTimeCnt; // 上一次获取的CPU时间计数器值
    uint32_t cpuFreqMHz;     // CPU频率，单位：MHz
    uint32_t cpuFreqHz;      // CPU频率，单位：Hz
    OmBoardInterface* interface;
} OmCpu;

/**
 * @brief 获取CPU名称
 *
 * @return char* CPU名称
 */
char* om_get_cpu_name(void);

/**
 * @brief 获取固件版本
 *
 * @return char* 固件版本
 */
char* om_get_firmware_version(void);

/**
 * @brief 获取CPU时间
 *
 * @return cputime_t CPU时间，单位：s
 */
Cputime om_cpu_get_time_s(void);

/**
 * @brief 获取CPU时间
 *
 * @return CputimeMs CPU时间，单位：ms
 */
CputimeMs om_cpu_get_time_ms(void);

/**
 * @brief 获取CPU时间
 *
 * @return CputimeUs CPU时间，单位：us
 */
CputimeUs om_cpu_get_time_us(void);

/**
 * @brief 获取CPU时间差
 *
 * @return Cputime CPU时间差，单位：s
 */
Cputime om_cpu_get_delta_time_s(CputimeCnt* last_time_cnt);

/**
 * @brief 处理CPU错误
 *
 * @param file 错误发生的文件名
 * @param line 错误发生的行号
 * @param msg 错误信息
 */
void om_cpu_errhandler(char* file, uint32_t line, uint8_t level, char* msg);

#define OM_CPU_ERRHANDLER(msg, level) om_cpu_errhandler(__FILE__, __LINE__, level, msg)

/**
 * @brief CPU复位
 *
 */
void om_cpu_reset(void);

/**
 * @brief 延时ms
 *
 * @param ms 延时时间，单位：ms
 */
void om_cpu_delay_ms(float ms);

/**
 * @brief 注册CPU
 *
 * @param cpuFreqMHz CPU频率，单位：MHz
 * @param interface 板级接口
 */
void om_cpu_register(uint32_t cpu_freq_m_hz, OmBoardInterface* interface);

/**
 * @brief 初始化CPU
 *
 */
void om_core_init(void);

/**
 * @brief 初始化板级函数
 *
 * @note  1、本函数用于初始化板级函数，用于板级初始化，以及注册cpu
 *        2、本函数必须在底层实现
 */
void om_board_init(void);

#endif

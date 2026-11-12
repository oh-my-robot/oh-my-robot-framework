/**
 * @file  bsp_serial_data.c
 * @brief rm-c-board Serial 板数据（板瘦身：板=数据，供共享适配层 bsp_serial_f4*.c 消费）
 * @details 契约见 serial/bsp_serial_f4.h；实例启用与 DMA 参数宏在板 shim bsp_serial.h。
 */
#include "bsp_serial.h"

/* 实例表（顺序 = SerialIdx_e 枚举） */
bsp_serial_s g_bsp_serial[BSP_SERIAL_COUNT] = {
#ifdef USE_SERIAL_1
    BSP_SERIAL_STATIC_INIT(USART1, "usart1", SERIAL_1_REG_PARAMS),
#endif
#ifdef USE_SERIAL_3
    BSP_SERIAL_STATIC_INIT(USART3, "usart3", SERIAL_3_REG_PARAMS),
#endif
#ifdef USE_SERIAL_6
    BSP_SERIAL_STATIC_INIT(USART6, "usart6", SERIAL_6_REG_PARAMS),
#endif
};

/* 一致性兜底：BSP_SERIAL_COUNT 必须等于 g_bsp_serial 条目数（C99 技巧，编译期校验） */
typedef char bsp_serial_count_ok[(BSP_SERIAL_COUNT == sizeof(g_bsp_serial) / sizeof(g_bsp_serial[0])) ? 1 : -1];

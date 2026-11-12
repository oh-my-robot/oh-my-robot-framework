/**
 * @file  bsp_serial_data.c
 * @brief rm-a-board Serial 板数据（板瘦身：板=数据，供共享适配层 bsp_serial_f4*.c 消费）
 * @details 契约见 serial/bsp_serial_f4.h；实例启用与 DMA profile 宏在板 shim bsp_serial.h。
 */
#include "bsp_serial.h"

/* 实例表（显式索引初始化——索引 = SerialIdx_e 枚举值，与枚举强绑定；
 * 顺序初始化依赖枚举顺序，增删实例会静默错位（2026-08-20 实测根因）） */
bsp_serial_s g_bsp_serial[BSP_SERIAL_COUNT] = {
#ifdef USE_SERIAL_3
    [SERIAL3_IDX] = BSP_SERIAL_STATIC_INIT(USART3, "usart3", SERIAL_3_REG_PARAMS),
#endif
#ifdef USE_SERIAL_6
    [SERIAL6_IDX] = BSP_SERIAL_STATIC_INIT(USART6, "usart6", SERIAL_6_REG_PARAMS),
#endif
#ifdef USE_SERIAL_7
    [SERIAL7_IDX] = BSP_SERIAL_STATIC_INIT(UART7, "uart7", SERIAL_7_REG_PARAMS),
#endif
#ifdef USE_SERIAL_8
    [SERIAL8_IDX] = BSP_SERIAL_STATIC_INIT(UART8, "uart8", SERIAL_8_REG_PARAMS),
#endif
};

/* 一致性兜底：BSP_SERIAL_COUNT 必须等于 g_bsp_serial 条目数（C99 技巧，编译期校验） */
typedef char bsp_serial_count_ok[(BSP_SERIAL_COUNT == sizeof(g_bsp_serial) / sizeof(g_bsp_serial[0])) ? 1 : -1];

/* 枚举顶索引 + 1 必须等于表大小（枚举与表错位时编译报错——根治索引漂移） */
_Static_assert(SERIAL8_IDX + 1 == BSP_SERIAL_COUNT,
               "SerialIdx_e 枚举与 g_bsp_serial 数据表大小不一致（检查 USE_SERIAL_* 宏）");

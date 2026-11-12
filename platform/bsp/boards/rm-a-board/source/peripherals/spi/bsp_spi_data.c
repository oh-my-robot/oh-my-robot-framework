/**
 * @file  bsp_spi_data.c
 * @brief rm-a-board SPI 板数据（板瘦身：板=数据，供共享适配层 bsp_spi_f4*.c 消费）
 * @details 契约见 spi/bsp_spi_f4.h；实例裁剪与 DMA 映射宏在板 shim bsp_spi.h。
 */
#include "bsp_spi.h"

/* 实例表（顺序 = BspSpiIdx_e 枚举） */
BspSpi_s gBspSpi[BSP_SPI_COUNT] = {
#ifdef USE_SPI4
    BSP_SPI_STATIC_INIT(SPI4),
#endif
};

/* 一致性兜底：BSP_SPI_COUNT 必须等于 gBspSpi 条目数（C99 技巧，编译期校验） */
typedef char bsp_spi_count_ok[(BSP_SPI_COUNT == sizeof(gBspSpi) / sizeof(gBspSpi[0])) ? 1 : -1];

/**
 * @file bsp_spi.h
 * @brief MSPM0G3507 SPI BSP 层 — 基于 DL_SPI + DL_DMA
 *
 * SPI1: PB7=MISO  PB8=MOSI  PB9=SCLK  PB6=CS0 (硬件 CS, 不用)
 * GPIO CS: PB5 (由框架 spi_device_attach 自动控制)
 * DMA CH4: SPI1 TX    DMA CH3: SPI1 RX (传输完成信号)
 */

#ifndef __OM_BSP_SPI_H__
#define __OM_BSP_SPI_H__

#include "drivers/peripheral/spi/pal_spi_dev.h"
#include "ti/devices/msp/msp.h"
#include "ti/driverlib/m0p/dl_core.h"

/*---------------------------------------------------------------------------*/
/* 使能裁剪                                                                  */
/*---------------------------------------------------------------------------*/

#define BSP_USE_SPI1

/*---------------------------------------------------------------------------*/
/* DMA 资源                                                                  */
/*---------------------------------------------------------------------------*/

#define BSP_SPI1_TX_DMA_CH   6
#define BSP_SPI1_RX_DMA_CH   5

/*---------------------------------------------------------------------------*/
/* BspSpi 结构                                                               */
/*---------------------------------------------------------------------------*/

typedef struct BspSpi {
    SpiBus       parent;         /* 必须第一：框架 SpiBus */
    SPI_Regs    *handle;         /* SPI 外设基地址 */
    /* DMA 状态 */
    size_t       pendingLen;     /* 当前传输总长度 (abort 时反算) */
    uint16_t     dummyTx;        /* tx==NULL 时 DMA 源 (0xFFFF, MINC=0 固定地址) */
    uint16_t     dummyRx;        /* rx==NULL 时 DMA 目标 (丢弃) */
} BspSpi_s, *BspSpi_t;

#define BSP_SPI_STATIC_INIT()                                           \
    (BspSpi_s) {                                                        \
        .handle  = SPI1,                                                \
        .dummyTx = 0xFFFFU,                                             \
    }

extern BspSpi_s gBspSpi[];

void bsp_spi_pre_init(BspSpi_t s);
void bsp_spi_register(void);
void bsp_spi_dma_isr(void);  /* 由 DMA_IRQHandler 统一分发 */

#endif /* __OM_BSP_SPI_H__ */

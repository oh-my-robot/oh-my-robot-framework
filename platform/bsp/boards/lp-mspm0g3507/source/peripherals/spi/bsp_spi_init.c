/**
 * @file      bsp_spi_init.c
 * @brief     MSPM0G3507 SPI1 硬件初始化 — GPIO/DMA/NVIC
 *
 * SPI1: PB7=MISO  PB8=MOSI  PB9=SCLK  PB6=CS0
 * SYSCFG_DL_init() 已完成 pinmux + 时钟。
 */

#include "bsp_spi.h"
#include "ti/driverlib/dl_spi.h"
#include "ti/driverlib/dl_dma.h"
#include "ti/driverlib/m0p/dl_core.h"

void bsp_spi_pre_init(BspSpi_t s)
{
    (void)s;

    /* 初始化为 Master, Motorola 3-pin 模式 */
    DL_SPI_Config cfg = {
        .mode          = DL_SPI_MODE_CONTROLLER,
        .frameFormat   = DL_SPI_FRAME_FORMAT_MOTO3_POL0_PHA0,
        .dataSize      = DL_SPI_DATA_SIZE_8,
        .bitOrder      = DL_SPI_BIT_ORDER_MSB_FIRST,
        .chipSelectPin = DL_SPI_CHIP_SELECT_NONE,  /* CS 由 GPIO 控制 */
    };

    DL_SPI_init(SPI1, &cfg);
    DL_SPI_enablePower(SPI1);
    /* FIFO 阈值 1 帧，确保单字节 DMA 不阻塞 */
    DL_SPI_setFIFOThreshold(SPI1,
        DL_SPI_RX_FIFO_LEVEL_ONE_FRAME,
        DL_SPI_TX_FIFO_LEVEL_ONE_FRAME);
    DL_SPI_enable(SPI1);

    /* NVIC: DMA 中断（共享 DMA_INT_IRQn，已在串口 BSP 中使能）*/
    NVIC_EnableIRQ(DMA_INT_IRQn);

    /* 使能 SPI 侧的 DMA 事件（transferOne 中也会调用，此处预置）*/
    DL_SPI_enableDMAReceiveEvent(SPI1, DL_SPI_DMA_INTERRUPT_RX);
    DL_SPI_enableDMATransmitEvent(SPI1);
}

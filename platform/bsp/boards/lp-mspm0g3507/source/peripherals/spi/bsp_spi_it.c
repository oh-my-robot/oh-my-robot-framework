/**
 * @file      bsp_spi_it.c
 * @brief     MSPM0G3507 SPI DMA 中断处理
 *
 * RX DMA 完成 → hal_spi_isr(bus, OM_OK, transferred)
 * TX DMA 完成 → 仅清理标志（RX 是最终完成信号）
 */

#include "bsp_spi.h"
#include "ti/driverlib/dl_dma.h"

/*---------------------------------------------------------------------------*/
/* SPI DMA ISR — DMA_IRQHandler 的 SPI 部分                                   */
/*---------------------------------------------------------------------------*/

void bsp_spi_dma_isr(void)
{
    uint32_t dma_ris = DMA->CPU_INT.RIS;

    for (uint8_t i = 0; i < 1U /* BSP_SPI_COUNT */; i++)
    {
        /* TX DMA 完成 — 仅清理 */
        if (dma_ris & (1UL << BSP_SPI1_TX_DMA_CH))
            DL_DMA_clearInterruptStatus(DMA, 1UL << BSP_SPI1_TX_DMA_CH);

        /* RX DMA 完成 — 传输完成信号 */
        if (dma_ris & (1UL << BSP_SPI1_RX_DMA_CH))
        {
            DL_DMA_clearInterruptStatus(DMA, 1UL << BSP_SPI1_RX_DMA_CH);
            DL_DMA_disableChannel(DMA, BSP_SPI1_RX_DMA_CH);
            DL_DMA_disableInterrupt(DMA, 1UL << BSP_SPI1_RX_DMA_CH);
            hal_spi_isr(&gBspSpi[i].parent, OM_OK,
                        gBspSpi[i].pendingLen);
        }
    }
}

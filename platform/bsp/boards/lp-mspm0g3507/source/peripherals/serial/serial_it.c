/**
 * @file      serial_it.c
 * @brief     MSPM0G3507 串口中断处理 — UART ISR + DMA ISR
 */

#include "bsp_serial.h"
#include "bsp_spi.h"
#include "ti/driverlib/dl_uart_main.h"
#include "ti/driverlib/dl_dma.h"
#include "drivers/peripheral/serial/pal_serial_dev.h"

/*---------------------------------------------------------------------------*/
/* 辅助：从 DMA 通道号查 bsp_serial、寄存器快捷访问                           */
/*---------------------------------------------------------------------------*/

static bsp_serial_t _find_by_dma_ch(uint8_t dma_ch)
{
    for (uint8_t i = 0; i < SERIAL_COUNT; i++) {
        if (g_bsp_serial[i].dma_rx_ch == dma_ch ||
            g_bsp_serial[i].dma_tx_ch == dma_ch)
            return &g_bsp_serial[i];
    }
    return NULL;
}

/* DMA 寄存器直接访问 — DriverLib 未封装的字段 */
#define DMA_RIS       (DMA->CPU_INT.RIS)
#define DMA_CH_SRC(a) (DMA->DMACHAN[(a)].DMASA)
#define DMA_CH_DST(a) (DMA->DMACHAN[(a)].DMADA)

/*---------------------------------------------------------------------------*/
/* DMA RX 中断处理                                                            */
/*---------------------------------------------------------------------------*/

void bsp_serial_dmarx_isr(bsp_serial_t s, uint8_t dma_ch)
{
    if (!s || !s->rx_multibuf)
        return;
    if (!(DMA_RIS & (1UL << dma_ch)))
        return;

    DL_DMA_clearInterruptStatus(DMA, 1UL << dma_ch);

    uint8_t *buf = s->rx_multibuf->container0;
    size_t   len = s->rx_multibuf->container_len;

    serial_hw_isr(&s->parent, SERIAL_EVENT_DMA_RXDONE, buf, len);

    /* 重启 DMA 接收 */
    DL_DMA_setTransferSize(DMA, dma_ch, (uint32_t)len);
    DMA_CH_DST(dma_ch) = (uint32_t)buf;
    DL_DMA_startTransfer(DMA, dma_ch);
}

/*---------------------------------------------------------------------------*/
/* DMA TX 中断处理                                                            */
/*---------------------------------------------------------------------------*/

void bsp_serial_dmatx_isr(bsp_serial_t s, uint8_t dma_ch)
{
    if (!s)
        return;
    if (!(DMA_RIS & (1UL << dma_ch)))
        return;

    /* 仅清理 DMA 中断标志，不触发框架 completion。
     * DMA 完成仅表示数据已写入 UART FIFO，UART 可能仍在移位输出。
     * completion 由 UART DMA_DONE_TX 中断（bsp_serial_uart_isr）触发，
     * 该中断在 FIFO + 移位寄存器全空后才置位。 */
    DL_DMA_clearInterruptStatus(DMA, 1UL << dma_ch);
}

/*---------------------------------------------------------------------------*/
/* UART 中断 — DMA_DONE_RX / DMA_DONE_TX                                      */
/*---------------------------------------------------------------------------*/

void bsp_serial_uart_isr(bsp_serial_t s)
{
    if (!s)
        return;

    uint32_t int_status = DL_UART_Main_getRawInterruptStatus(s->handle, 0xFFFFFFFFu);

    if (int_status & DL_UART_MAIN_INTERRUPT_DMA_DONE_RX) {
        DL_UART_Main_clearInterruptStatus(s->handle,
            DL_UART_MAIN_INTERRUPT_DMA_DONE_RX);
        bsp_serial_dmarx_isr(s, s->dma_rx_ch);
    }

    if (int_status & DL_UART_MAIN_INTERRUPT_DMA_DONE_TX) {
        DL_UART_Main_clearInterruptStatus(s->handle,
            DL_UART_MAIN_INTERRUPT_DMA_DONE_TX);
        /* DMA_DONE_TX 在 FIFO+移位寄存器全空时置位，是真正的发送完成 */
        serial_hw_isr(&s->parent, SERIAL_EVENT_DMA_TXDONE,
                      NULL, s->tx_xfer_size);
    }

    /* INT TX：仅当软件发起了 INT TX（tx_remaining > 0）才处理；
     * DMA 模式下 TX FIFO 空中断也会置位 RIS，但 tx_remaining == 0 */
    if ((int_status & DL_UART_MAIN_INTERRUPT_TX) && s->tx_remaining > 0) {
        DL_UART_Main_clearInterruptStatus(s->handle,
            DL_UART_MAIN_INTERRUPT_TX);
        while (s->tx_remaining > 0 && !DL_UART_Main_isTXFIFOFull(s->handle)) {
            DL_UART_Main_transmitData(s->handle, *s->tx_data++);
            s->tx_remaining--;
        }
        if (s->tx_remaining == 0) {
            DL_UART_Main_disableInterrupt(s->handle,
                DL_UART_MAIN_INTERRUPT_TX);
            serial_hw_isr(&s->parent, SERIAL_EVENT_INT_TXDONE,
                          NULL, s->tx_xfer_size);
        }
    }

    if (int_status & DL_UART_MAIN_INTERRUPT_RX) {
        DL_UART_Main_clearInterruptStatus(s->handle,
            DL_UART_MAIN_INTERRUPT_RX);
        uint8_t data = DL_UART_Main_receiveData(s->handle);
        serial_hw_isr(&s->parent, SERIAL_EVENT_INT_RXDONE, &data, 1);
    }
}

/*===========================================================================*/
/* NVIC ISR 入口                                                             */
/*===========================================================================*/

void DMA_IRQHandler(void)
{
    for (uint8_t i = 0; i < SERIAL_COUNT; i++) {
        bsp_serial_t s = &g_bsp_serial[i];
        uint8_t tx_ch = s->dma_tx_ch;
        uint8_t rx_ch = s->dma_rx_ch;

        if (DMA_RIS & (1UL << rx_ch))
            bsp_serial_dmarx_isr(s, rx_ch);
        if (DMA_RIS & (1UL << tx_ch))
            bsp_serial_dmatx_isr(s, tx_ch);
    }

    bsp_spi_dma_isr();
}

#ifdef USE_SERIAL_1
void UART1_IRQHandler(void)
{
    bsp_serial_uart_isr(&g_bsp_serial[SERIAL1_IDX]);
}
#endif

#ifdef USE_SERIAL_2
void UART2_IRQHandler(void)
{
    bsp_serial_uart_isr(&g_bsp_serial[SERIAL2_IDX]);
}
#endif

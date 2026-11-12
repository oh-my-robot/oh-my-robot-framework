/**
 * @file      bsp_serial_init.c
 * @brief     MSPM0G3507 串口硬件初始化 + DMA 配置
 *
 * UART1: PA8(TX) PA9(RX)  2Mbps
 * UART2: PA21(TX) PA22(RX) 115200
 * SYSCFG_DL_init() 已完成 pinmux + 时钟，本文件负责 NVIC/DMA/格式配置。
 */

#include "bsp_serial.h"
#include "ti/driverlib/dl_uart_main.h"
#include "ti/driverlib/dl_dma.h"
#include "ti/driverlib/m0p/dl_core.h"

/* UART 模块时钟 = 32MHz (SYSOSC) */
#define UART_CLK_HZ  32000000u

/*---------------------------------------------------------------------------*/
/* DMA RX 配置（循环模式，持续接收）                                          */
/*---------------------------------------------------------------------------*/

void bsp_serial_dma_cfg(bsp_serial_t s, uint32_t dma_regparams)
{
    if (dma_regparams == SERIAL_REG_DMA_RX && s->rx_multibuf) {
        uint8_t  ch      = s->dma_rx_ch;
        uint8_t  trigger = (ch == 2) ? DMA_UART1_RX_TRIG : DMA_UART2_RX_TRIG;
        uint8_t *buf     = s->rx_multibuf->container0;
        size_t   len     = s->rx_multibuf->container_len;

        DL_DMA_Config dma_rx_cfg = {
            .transferMode  = DL_DMA_FULL_CH_REPEAT_SINGLE_TRANSFER_MODE,
            .extendedMode  = DL_DMA_NORMAL_MODE,
            .destIncrement = DL_DMA_ADDR_INCREMENT,
            .srcIncrement  = DL_DMA_ADDR_UNCHANGED,
            .destWidth     = DL_DMA_WIDTH_BYTE,
            .srcWidth      = DL_DMA_WIDTH_BYTE,
            .trigger       = trigger,
            .triggerType   = DL_DMA_TRIGGER_TYPE_EXTERNAL,
        };

        DL_DMA_initChannel(DMA, ch, &dma_rx_cfg);
        DL_DMA_setSrcAddr(DMA, ch, (uint32_t)&s->handle->RXDATA);
        DMA->DMACHAN[ch].DMADA = (uint32_t)buf;
        DL_DMA_setTransferSize(DMA, ch, (uint32_t)len);
        DL_DMA_clearInterruptStatus(DMA, 1UL << ch);
        DL_DMA_enableInterrupt(DMA, 1UL << ch);
        DL_DMA_enableChannel(DMA, ch);
        DL_DMA_startTransfer(DMA, ch);

        /* 使能 UART → DMA 的接收触发 */
        DL_UART_Main_enableDMAReceiveEvent(s->handle, DL_UART_DMA_INTERRUPT_RX);

        s->rx_multibuf->last_rx_cnt = 0;
    }

    if (dma_regparams == SERIAL_REG_DMA_TX) {
        uint8_t ch = s->dma_tx_ch;
        DL_DMA_enableInterrupt(DMA, 1UL << ch);
        DL_UART_Main_enableDMATransmitEvent(s->handle);
    }
}

/*---------------------------------------------------------------------------*/
/* NVIC 初始化                                                               */
/*---------------------------------------------------------------------------*/

static void bsp_serial_nvic_init(bsp_serial_t s)
{
    IRQn_Type uart_irq;
    if (s->handle == UART1)
        uart_irq = UART1_INT_IRQn;
    else if (s->handle == UART2)
        uart_irq = UART2_INT_IRQn;
    else
        return;

    NVIC_SetPriority(uart_irq, 6);
    NVIC_EnableIRQ(uart_irq);

    NVIC_SetPriority(DMA_INT_IRQn, 5);
    NVIC_EnableIRQ(DMA_INT_IRQn);
}

/*---------------------------------------------------------------------------*/
/* 预初始化 — 默认配置 + 启动硬件                                             */
/*---------------------------------------------------------------------------*/

void bsp_serial_pre_init(bsp_serial_t s)
{
    DL_UART_Main_Config default_cfg = {
        .mode        = DL_UART_MAIN_MODE_NORMAL,
        .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
        .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
        .parity      = DL_UART_MAIN_PARITY_NONE,
        .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
        .stopBits    = DL_UART_MAIN_STOP_BITS_ONE,
    };

    uint32_t baud = (s->handle == UART1) ? 2000000u : 115200u;

    /* 同步框架 cfg，防止 serial_init → configure 用默认值覆盖 */
    s->parent.cfg.baudrate  = baud;
    s->parent.cfg.txBufSize = 256U;  /* 避免环缓冲区回绕导致消息分裂 */
    s->parent.cfg.rxBufSize = 256U;

    DL_UART_Main_disable(s->handle);
    DL_UART_Main_init(s->handle, &default_cfg);
    DL_UART_Main_configBaudRate(s->handle, UART_CLK_HZ, baud);
    DL_UART_Main_setOversampling(s->handle, DL_UART_OVERSAMPLING_RATE_16X);

    DL_UART_Main_enableFIFOs(s->handle);
    DL_UART_Main_enablePower(s->handle);
    DL_UART_Main_enable(s->handle);

    /* 使能 UART 侧的 DMA_DONE 中断 */
    DL_UART_Main_enableInterrupt(s->handle,
        DL_UART_MAIN_INTERRUPT_DMA_DONE_RX |
        DL_UART_MAIN_INTERRUPT_DMA_DONE_TX);

    bsp_serial_nvic_init(s);

    /* 如果注册参数含 DMA_RX，立即启动循环接收 */
    uint32_t regparams = device_get_regparams((Device *)&s->parent);
    if ((regparams & DEVICE_REG_RXTYPE_MASK) == SERIAL_REG_DMA_RX)
        bsp_serial_dma_cfg(s, SERIAL_REG_DMA_RX);
}

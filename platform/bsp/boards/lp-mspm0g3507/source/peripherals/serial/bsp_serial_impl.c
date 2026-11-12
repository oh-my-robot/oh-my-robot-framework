/**
 * @file      bsp_serial_impl.c
 * @brief     MSPM0G3507 串口 BSP — SerialInterface 5 函数实现（DL_UART + DL_DMA）
 */

#include "bsp_serial.h"
#include "ti/driverlib/dl_uart_main.h"
#include "ti/driverlib/dl_dma.h"

/*---------------------------------------------------------------------------*/
/* 静态辅助                                                                  */
/*---------------------------------------------------------------------------*/

static inline bsp_serial_t to_bsp(HalSerial *serial)
{
    return (bsp_serial_t)serial->parent.handle;
}

/* UART 模块时钟 = 32MHz (SYSOSC) */
#define UART_CLK_HZ  32000000u

/*---------------------------------------------------------------------------*/
/* configure — 从 SerialCfg 映射到 DL_UART                                    */
/*---------------------------------------------------------------------------*/

static OmRet bsp_serial_configure(HalSerial *serial, SerialCfg *cfg)
{
    bsp_serial_t s = to_bsp(serial);

    DL_UART_Main_Config config = {
        .mode        = DL_UART_MAIN_MODE_NORMAL,
        .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
        .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
        .parity      = DL_UART_MAIN_PARITY_NONE,
        .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
        .stopBits    = DL_UART_MAIN_STOP_BITS_ONE,
    };

    /* 数据位 */
    switch (cfg->dataBits) {
    case DATA_BITS_5:  config.wordLength = DL_UART_MAIN_WORD_LENGTH_5_BITS; break;
    case DATA_BITS_6:  config.wordLength = DL_UART_MAIN_WORD_LENGTH_6_BITS; break;
    case DATA_BITS_7:  config.wordLength = DL_UART_MAIN_WORD_LENGTH_7_BITS; break;
    default: break;
    }

    /* 停止位 */
    if (cfg->stopBits == STOP_BITS_2)
        config.stopBits = DL_UART_MAIN_STOP_BITS_TWO;

    /* 校验 */
    switch (cfg->parity) {
    case PARITY_ODD:  config.parity = DL_UART_MAIN_PARITY_ODD;  break;
    case PARITY_EVEN: config.parity = DL_UART_MAIN_PARITY_EVEN; break;
    default: break;
    }

    /* 过采样 */
    DL_UART_OVERSAMPLING_RATE oversampling =
        (cfg->overSampling == OVERSAMPLING_8)
            ? DL_UART_OVERSAMPLING_RATE_8X
            : DL_UART_OVERSAMPLING_RATE_16X;

    /* 重新初始化 */
    DL_UART_Main_disable(s->handle);
    DL_UART_Main_init(s->handle, &config);
    DL_UART_Main_configBaudRate(s->handle, UART_CLK_HZ, cfg->baudrate);
    DL_UART_Main_setOversampling(s->handle, oversampling);
    DL_UART_Main_enable(s->handle);

    return OM_OK;
}

/*---------------------------------------------------------------------------*/
/* putByte — 阻塞单字节发送                                                   */
/*---------------------------------------------------------------------------*/

static OmRet bsp_serial_putByte(HalSerial *serial, uint8_t data)
{
    DL_UART_Main_transmitDataBlocking(to_bsp(serial)->handle, data);
    return OM_OK;
}

/*---------------------------------------------------------------------------*/
/* getByte — 阻塞单字节接收                                                   */
/*---------------------------------------------------------------------------*/

static OmRet bsp_serial_getByte(HalSerial *serial, uint8_t *buf)
{
    bsp_serial_t s = to_bsp(serial);
    if (DL_UART_Main_isRXFIFOEmpty(s->handle))
        return OM_ERROR_TIMEOUT;
    *buf = DL_UART_Main_receiveDataBlocking(s->handle);
    return OM_OK;
}

/*---------------------------------------------------------------------------*/
/* transmit — DMA 发送 / INT 发送                                             */
/*---------------------------------------------------------------------------*/

static size_t bsp_serial_transmit(HalSerial *serial, const uint8_t *data, size_t len)
{
    bsp_serial_t s = to_bsp(serial);
    uint32_t regparams = device_get_regparams(&serial->parent);
    uint32_t tx_type   = regparams & DEVICE_REG_TXTYPE_MASK;

    if (len == 0)
        return 0;

    s->tx_xfer_size = len;

    if (tx_type == SERIAL_REG_DMA_TX) {

        /* 确保通道空闲：禁止 → 清中断 → 重新初始化 */
        DL_DMA_disableChannel(DMA, s->dma_tx_ch);
        DL_DMA_clearInterruptStatus(DMA, 1UL << s->dma_tx_ch);

        DL_DMA_Config dma_cfg = {
            .transferMode  = DL_DMA_SINGLE_TRANSFER_MODE,
            .extendedMode  = DL_DMA_NORMAL_MODE,
            .destIncrement = DL_DMA_ADDR_UNCHANGED,
            .srcIncrement  = DL_DMA_ADDR_INCREMENT,
            .destWidth     = DL_DMA_WIDTH_BYTE,
            .srcWidth      = DL_DMA_WIDTH_BYTE,
            .trigger       = (s->dma_tx_ch == 1) ? DMA_UART1_TX_TRIG
                           : (s->dma_tx_ch == 3) ? DMA_UART2_TX_TRIG : 0,
            .triggerType   = DL_DMA_TRIGGER_TYPE_EXTERNAL,
        };

        DL_DMA_initChannel(DMA, s->dma_tx_ch, &dma_cfg);
        DL_DMA_setTransferSize(DMA, s->dma_tx_ch, (uint32_t)len);
        DL_DMA_setSrcAddr(DMA, s->dma_tx_ch, (uint32_t)data);
        DMA->DMACHAN[s->dma_tx_ch].DMADA = (uint32_t)&s->handle->TXDATA;
        DL_DMA_enableChannel(DMA, s->dma_tx_ch);
        DL_DMA_startTransfer(DMA, s->dma_tx_ch);

    } else if (tx_type == SERIAL_REG_INT_TX) {

        s->tx_data      = data;
        s->tx_remaining = len;

        /* 使能 TX 中断，并立即填满 FIFO 产生首次边沿 */
        DL_UART_Main_enableInterrupt(s->handle, DL_UART_MAIN_INTERRUPT_TX);
        while (s->tx_remaining > 0 && !DL_UART_Main_isTXFIFOFull(s->handle)) {
            DL_UART_Main_transmitData(s->handle, *s->tx_data++);
            s->tx_remaining--;
        }

    } else {
        return 0;
    }

    return len;
}

/*---------------------------------------------------------------------------*/
/* control — 多路复用命令                                                     */
/*---------------------------------------------------------------------------*/

static OmRet bsp_serial_control(HalSerial *serial, uint32_t cmd, void *arg)
{
    bsp_serial_t s = to_bsp(serial);
    uint32_t regparams = device_get_regparams(&serial->parent);

    switch (cmd) {

    case SERIAL_CMD_SET_IOTPYE: {
        uint32_t iotype = (uint32_t)(uintptr_t)arg;
        if (iotype == SERIAL_REG_DMA_TX || iotype == SERIAL_REG_DMA_RX)
            bsp_serial_dma_cfg(s, iotype);
        else if (iotype == SERIAL_REG_INT_RX)
            DL_UART_Main_enableInterrupt(s->handle, DL_UART_MAIN_INTERRUPT_RX);
        break;
    }

    case SERIAL_CMD_SUSPEND:
        DL_UART_Main_disable(s->handle);
        break;

    case SERIAL_CMD_RESUME:
        if (arg != NULL) {
            if (bsp_serial_configure(serial, (SerialCfg *)arg) != OM_OK)
                return OM_ERROR;
        } else {
            DL_UART_Main_enable(s->handle);
        }
        if ((regparams & DEVICE_REG_RXTYPE_MASK) == SERIAL_REG_DMA_RX)
            bsp_serial_dma_cfg(s, SERIAL_REG_DMA_RX);
        break;

    default:
        break;
    }

    return OM_OK;
}

/*---------------------------------------------------------------------------*/
/* SerialInterface 函数表                                                     */
/*---------------------------------------------------------------------------*/

static SerialInterface bsp_serial_interface = {
    .configure = bsp_serial_configure,
    .control   = bsp_serial_control,
    .getByte   = bsp_serial_getByte,
    .putByte   = bsp_serial_putByte,
    .transmit  = bsp_serial_transmit,
};

/*---------------------------------------------------------------------------*/
/* 资源表                                                                    */
/*---------------------------------------------------------------------------*/

#ifdef USE_SERIAL_1
#ifdef USE_SERIAL1_CONTAINER1
static uint8_t _ser1_rx0[SERIAL_1_RX_MULTIBUF_SIZE];
static uint8_t _ser1_rx1[SERIAL_1_RX_MULTIBUF_SIZE];
static BSP_SERIAL_MULTIBUF_DEF(_ser1_mb, _ser1_rx0, _ser1_rx1, SERIAL_1_RX_MULTIBUF_SIZE);
#endif
#endif

#ifdef USE_SERIAL_2
#ifdef USE_SERIAL2_CONTAINER1
static uint8_t _ser2_rx0[SERIAL_2_RX_MULTIBUF_SIZE];
static uint8_t _ser2_rx1[SERIAL_2_RX_MULTIBUF_SIZE];
static BSP_SERIAL_MULTIBUF_DEF(_ser2_mb, _ser2_rx0, _ser2_rx1, SERIAL_2_RX_MULTIBUF_SIZE);
#endif
#endif

bsp_serial_s g_bsp_serial[] = {
#ifdef USE_SERIAL_1
    BSP_SERIAL_STATIC_INIT(UART1_BASE, "usart1", SERIAL_1_REG_PARAMS,
                           SERIAL_1_DMA_TX_CH, SERIAL_1_DMA_RX_CH),
#endif
#ifdef USE_SERIAL_2
    BSP_SERIAL_STATIC_INIT(UART2_BASE, "usart2", SERIAL_2_REG_PARAMS,
                           SERIAL_2_DMA_TX_CH, SERIAL_2_DMA_RX_CH),
#endif
};

/*---------------------------------------------------------------------------*/
/* 注册入口                                                                  */
/*---------------------------------------------------------------------------*/

void bsp_serial_register(void)
{
    for (uint8_t i = 0; i < SERIAL_COUNT; i++) {
        g_bsp_serial[i].parent.interface = &bsp_serial_interface;
#ifdef USE_SERIAL_1
        if (g_bsp_serial[i].handle == UART1)
            g_bsp_serial[i].rx_multibuf = &_ser1_mb;
#endif
#ifdef USE_SERIAL_2
        if (g_bsp_serial[i].handle == UART2)
            g_bsp_serial[i].rx_multibuf = &_ser2_mb;
#endif
        serial_register(&g_bsp_serial[i].parent,
                        g_bsp_serial[i].name,
                        &g_bsp_serial[i],
                        g_bsp_serial[i].regparams);
        bsp_serial_pre_init(&g_bsp_serial[i]);
    }
}

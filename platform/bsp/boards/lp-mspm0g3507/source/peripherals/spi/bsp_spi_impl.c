/**
 * @file      bsp_spi_impl.c
 * @brief     MSPM0G3507 SPI BSP — SpiControllerOps 4 函数实现 (DL_SPI + DL_DMA)
 */

#include "bsp_spi.h"
#include "ti/driverlib/dl_spi.h"
#include "ti/driverlib/dl_dma.h"

/* --- 内部辅助 ------------------------------------------------------------ */

static inline BspSpi_t self_from_bus(SpiBus *bus)
{
    return (BspSpi_t)bus;
}

/* SPI 时钟 = BUSCLK 32MHz */
#define BUS_CLK_HZ  32000000u

/*---------------------------------------------------------------------------*/
/* configure — SpiDeviceCfg → DL_SPI 寄存器                                  */
/*---------------------------------------------------------------------------*/

static OmRet bsp_spi_configure(SpiBus *bus, const SpiDeviceCfg *cfg)
{
    BspSpi_t s = self_from_bus(bus);

    /* 帧格式: SPI_MODE_n → Motorola 3-pin CPOL/CPHA */
    static const DL_SPI_FRAME_FORMAT mode_map[] = {
        DL_SPI_FRAME_FORMAT_MOTO3_POL0_PHA0,  /* SPI_MODE_0 */
        DL_SPI_FRAME_FORMAT_MOTO3_POL0_PHA1,  /* SPI_MODE_1 */
        DL_SPI_FRAME_FORMAT_MOTO3_POL1_PHA0,  /* SPI_MODE_2 */
        DL_SPI_FRAME_FORMAT_MOTO3_POL1_PHA1,  /* SPI_MODE_3 */
    };

    /* 波特率分频 */
    uint32_t desired_hz = cfg->maxHz;
    uint32_t divider = BUS_CLK_HZ / desired_hz;
    if (divider < 2U)  divider = 2U;
    if (divider > 256U) divider = 256U;

    DL_SPI_CLOCK_DIVIDE_RATIO ratio;
    if      (divider <= 2)  ratio = DL_SPI_CLOCK_DIVIDE_RATIO_2;
    else if (divider <= 3)  ratio = DL_SPI_CLOCK_DIVIDE_RATIO_3;
    else if (divider <= 4)  ratio = DL_SPI_CLOCK_DIVIDE_RATIO_4;
    else if (divider <= 5)  ratio = DL_SPI_CLOCK_DIVIDE_RATIO_5;
    else if (divider <= 6)  ratio = DL_SPI_CLOCK_DIVIDE_RATIO_6;
    else if (divider <= 7)  ratio = DL_SPI_CLOCK_DIVIDE_RATIO_7;
    else                    ratio = DL_SPI_CLOCK_DIVIDE_RATIO_8;

    bus->actualHz = BUS_CLK_HZ / ((uint32_t)ratio + 1U);

    DL_SPI_disable(s->handle);

    /* 设置帧格式 (CPOL/CPHA) */
    DL_SPI_setFrameFormat(s->handle, mode_map[cfg->mode & 0x3U]);

    /* 分频 */
    DL_SPI_ClockConfig clk_cfg = {
        .clockSel    = DL_SPI_CLOCK_BUSCLK,
        .divideRatio = ratio,
    };
    DL_SPI_setClockConfig(s->handle, &clk_cfg);

    /* 位序 */
    DL_SPI_setBitOrder(s->handle,
        (cfg->bitOrder == SPI_LSB_FIRST)
            ? DL_SPI_BIT_ORDER_LSB_FIRST
            : DL_SPI_BIT_ORDER_MSB_FIRST);

    /* 数据宽度 */
    DL_SPI_setDataSize(s->handle,
        (cfg->dataWidth == 16U) ? DL_SPI_DATA_SIZE_16
                                : DL_SPI_DATA_SIZE_8);

    DL_SPI_enable(s->handle);
    return OM_OK;
}

/*---------------------------------------------------------------------------*/
/* transferOne — 启动全双工 DMA 传输（非阻塞）                                 */
/*---------------------------------------------------------------------------*/

static OmRet bsp_spi_transferOne(SpiBus *bus, const uint8_t *tx,
                                  uint8_t *rx, size_t len)
{
    BspSpi_t s = self_from_bus(bus);
    if (len == 0U) return OM_OK;

    s->pendingLen = len;

    /* --- TX DMA (CH4): 内存 → SPI TXDATA --- */
    {
        uint32_t src_addr;
        bool     src_inc;

        if (tx) {
            src_addr = (uint32_t)tx;
            src_inc  = true;
        } else {
            src_addr = (uint32_t)&s->dummyTx;
            src_inc  = false;   /* 固定地址，重复发送 0xFFFF */
        }

        DL_DMA_Config tx_cfg = {
            .transferMode  = DL_DMA_SINGLE_TRANSFER_MODE,
            .extendedMode  = DL_DMA_NORMAL_MODE,
            .destIncrement = DL_DMA_ADDR_UNCHANGED,
            .srcIncrement  = src_inc ? DL_DMA_ADDR_INCREMENT
                                     : DL_DMA_ADDR_UNCHANGED,
            .destWidth     = DL_DMA_WIDTH_BYTE,
            .srcWidth      = DL_DMA_WIDTH_BYTE,
            .trigger       = DMA_SPI1_TX_TRIG,
            .triggerType   = DL_DMA_TRIGGER_TYPE_EXTERNAL,
        };

        DL_DMA_initChannel(DMA, BSP_SPI1_TX_DMA_CH, &tx_cfg);
        DL_DMA_setTransferSize(DMA, BSP_SPI1_TX_DMA_CH, (uint32_t)len);
        DL_DMA_setSrcAddr(DMA, BSP_SPI1_TX_DMA_CH, src_addr);
        DMA->DMACHAN[BSP_SPI1_TX_DMA_CH].DMADA =
            (uint32_t)&s->handle->TXDATA;
        DL_DMA_clearInterruptStatus(DMA, 1UL << BSP_SPI1_TX_DMA_CH);
        DL_DMA_disableInterrupt(DMA, 1UL << BSP_SPI1_TX_DMA_CH);
        DL_DMA_enableChannel(DMA, BSP_SPI1_TX_DMA_CH);
        DL_DMA_startTransfer(DMA, BSP_SPI1_TX_DMA_CH);
    }

    /* --- RX DMA (CH3): SPI RXDATA → 内存 --- */
    {
        uint32_t dst_addr;
        bool     dst_inc;

        if (rx) {
            dst_addr = (uint32_t)rx;
            dst_inc  = true;
        } else {
            dst_addr = (uint32_t)&s->dummyRx;
            dst_inc  = false;
        }

        DL_DMA_Config rx_cfg = {
            .transferMode  = DL_DMA_SINGLE_TRANSFER_MODE,
            .extendedMode  = DL_DMA_NORMAL_MODE,
            .destIncrement = dst_inc ? DL_DMA_ADDR_INCREMENT
                                     : DL_DMA_ADDR_UNCHANGED,
            .srcIncrement  = DL_DMA_ADDR_UNCHANGED,
            .destWidth     = DL_DMA_WIDTH_BYTE,
            .srcWidth      = DL_DMA_WIDTH_BYTE,
            .trigger       = DMA_SPI1_RX_TRIG,
            .triggerType   = DL_DMA_TRIGGER_TYPE_EXTERNAL,
        };

        DL_DMA_initChannel(DMA, BSP_SPI1_RX_DMA_CH, &rx_cfg);
        DL_DMA_setTransferSize(DMA, BSP_SPI1_RX_DMA_CH, (uint32_t)len);
        DL_DMA_setSrcAddr(DMA, BSP_SPI1_RX_DMA_CH,
                          (uint32_t)&s->handle->RXDATA);
        DMA->DMACHAN[BSP_SPI1_RX_DMA_CH].DMADA = dst_addr;
        DL_DMA_clearInterruptStatus(DMA, 1UL << BSP_SPI1_RX_DMA_CH);
        DL_DMA_enableInterrupt(DMA, 1UL << BSP_SPI1_RX_DMA_CH);
        DL_DMA_enableChannel(DMA, BSP_SPI1_RX_DMA_CH);
        DL_DMA_startTransfer(DMA, BSP_SPI1_RX_DMA_CH);
    }

    /* 使能 SPI 侧 DMA 触发 */
    DL_SPI_enableDMAReceiveEvent(s->handle, DL_SPI_DMA_INTERRUPT_RX);
    DL_SPI_enableDMATransmitEvent(s->handle);

    return OM_OK;
}

/*---------------------------------------------------------------------------*/
/* control — ABORT / SUSPEND / RESUME                                        */
/*---------------------------------------------------------------------------*/

static OmRet bsp_spi_control(SpiBus *bus, uint32_t cmd, void *arg)
{
    BspSpi_t s = self_from_bus(bus);
    (void)arg;

    switch (cmd) {
    case SPI_CMD_ABORT:
        DL_DMA_disableChannel(DMA, BSP_SPI1_TX_DMA_CH);
        DL_DMA_disableChannel(DMA, BSP_SPI1_RX_DMA_CH);
        DL_SPI_disable(s->handle);
        /* 反算已传输字节 */
        {
            size_t remaining =
                (size_t)DL_DMA_getTransferSize(DMA, BSP_SPI1_RX_DMA_CH);
            size_t transferred = s->pendingLen - remaining;
            hal_spi_isr(bus, OM_OK, transferred);
        }
        break;

    case SPI_CMD_SUSPEND:
        DL_SPI_disable(s->handle);
        break;

    case SPI_CMD_RESUME:
        DL_SPI_enable(s->handle);
        break;

    default:
        break;
    }
    return OM_OK;
}

/*---------------------------------------------------------------------------*/
/* SpiControllerOps 函数表                                                    */
/*---------------------------------------------------------------------------*/

static SpiControllerOps bsp_spi_ops = {
    .configure   = bsp_spi_configure,
    .transferOne = bsp_spi_transferOne,
    .control     = bsp_spi_control,
    .setCs       = NULL,  /* 通过 csSpec GPIO 框架控制 */
};

/*---------------------------------------------------------------------------*/
/* 资源表 + 注册                                                             */
/*---------------------------------------------------------------------------*/

BspSpi_s gBspSpi[] = {
#ifdef BSP_USE_SPI1
    BSP_SPI_STATIC_INIT(),
#endif
};

void bsp_spi_register(void)
{
    for (uint8_t i = 0; i < 1U /* BSP_SPI_COUNT */; i++)
    {
        bsp_spi_pre_init(&gBspSpi[i]);
        spi_bus_register(&gBspSpi[i].parent, &gBspSpi[i], &bsp_spi_ops);
    }
}

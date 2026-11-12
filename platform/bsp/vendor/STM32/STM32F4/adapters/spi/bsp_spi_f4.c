/**
 * @file    bsp_spi_f4.c
 * @brief   STM32F4 家族 SPI BSP 共享适配层（板瘦身：由 rm-a 唯一实现上移；
 *          SpiControllerOps 4 回调 + 注册入口，实例表外置板数据）
 * @details 实现 SpiControllerOps 的 configure / transferOne / control / setCs，
 *          并提供 bsp_spi_register() 注册入口（实例表 gBspSpi[] 由板数据提供）。
 *
 *          关键约束（bsp_implementer_checklist.md §1）：
 *          - configure ：持 bus->lock，线程上下文，按 cfg 字段重配 SPI 寄存器
 *          - transferOne：持 bus->lock，线程上下文，**异步**——启动 DMA 后立即
 *                        返回 OM_OK，DMA 完成 IRQ 触发 hal_spi_isr
 *          - control   ：持 bus->lock，线程上下文，处理 ABORT/SUSPEND/RESUME
 *          - setCs     ：A 板 CS 默认走 GPIO 路径，故置 NULL
 *
 * @note    不写 poll 路径（任务约束 #1）。
 */

#include "bsp_spi.h"
#include "core/om_def.h"
#include "core/om_init.h"

/* 分散加载自注册：把 bsp_spi_register 挂到 .om_init 段（BOARD 级，由 om_do_initcalls 自动调用） */
static OmRet bsp_spi_self_init(void)
{
    bsp_spi_register();
    return OM_OK;
}
OM_INIT_BOARD(bsp_spi_self_init);

/*===========================================================================
 * 内部辅助：根据 maxHz 计算 BR 分频（取 <= maxHz 的最大分频）
 *===========================================================================*/

/**
 * @brief 由期望 SCLK 频率反算 SPI_CR1.BR 分频位
 * @param  spi_clk_hz  SPI 外设输入时钟（SPI4 在 APB2，90MHz）
 * @param  max_hz      从设备期望最大 SCLK 频率（来自 SpiDeviceCfg.maxHz）
 * @retval SPI_BAUDRATEPRESCALER_xxx
 *
 * STM32F4 SPI BR 分频序列：fpclk / {2,4,8,16,32,64,128,256}，
 * 对应 BAUDRATEPRESCALER_2.._256。
 * 策略：选使实际频率 <= max_hz 的最小分频（即最大实际频率）。
 *
 * @note  HAL 的 BAUDRATEPRESCALER_xxx 是 CR1.BR 位段掩码（非 0,1,2... 线性序号），
 *        必须用 lookup table 映射，不能直接左移。
 */
static const uint32_t gSpiBaudrateTable[8] = {
    SPI_BAUDRATEPRESCALER_2,   /* fpclk/2   */
    SPI_BAUDRATEPRESCALER_4,   /* fpclk/4   */
    SPI_BAUDRATEPRESCALER_8,   /* fpclk/8   */
    SPI_BAUDRATEPRESCALER_16,  /* fpclk/16  */
    SPI_BAUDRATEPRESCALER_32,  /* fpclk/32  */
    SPI_BAUDRATEPRESCALER_64,  /* fpclk/64  */
    SPI_BAUDRATEPRESCALER_128, /* fpclk/128 */
    SPI_BAUDRATEPRESCALER_256, /* fpclk/256 */
};

static uint32_t bsp_spi_compute_baudrate(uint32_t spi_clk_hz, uint32_t max_hz,
                                          uint32_t *actual_hz_out)
{
    if (max_hz == 0U) {
        if (actual_hz_out)
            *actual_hz_out = spi_clk_hz / 2U;
        return SPI_BAUDRATEPRESCALER_2;
    }

    /* 从最小分频（fpclk/2，索引0）枚举到最大分频（fpclk/256，索引7），
     * 取首个使 actual <= max_hz 的项。 */
    for (uint32_t i = 0U; i < 8U; i++)
    {
        uint32_t div    = 2UL << i; /* 分频系数 2,4,8,...,256 */
        uint32_t actual = spi_clk_hz / div;
        if (actual <= max_hz) {
            if (actual_hz_out)
                *actual_hz_out = actual;
            return gSpiBaudrateTable[i];
        }
    }
    if (actual_hz_out)
        *actual_hz_out = spi_clk_hz / 256U;
    return SPI_BAUDRATEPRESCALER_256;
}

/*===========================================================================
 * SpiControllerOps 4 回调
 *===========================================================================*/

/**
 * @brief 配置 SPI 控制寄存器（CPOL/CPHA/BR/DFF/LSBFIRST）
 * @note  必须先 __HAL_SPI_DISABLE，否则 CR1/CR2 写入无效。
 *        SPI_Mode / NSS / FirstBit 等基础字段在首次 HAL_SPI_Init 时固定，
 *        此处仅重配 dev-specific 字段（bsp_implementer_checklist.md §1.1）。
 */
static OmRet bsp_spi_configure(SpiBus *bus, const SpiDeviceCfg *cfg)
{
    if (!bus || !cfg)
        return OM_ERR_INVALID_ARG;

    BspSpi_t          bsp_spi = (BspSpi_t)bus->hwPrivate;
    SPI_HandleTypeDef *hspi   = &bsp_spi->handle;

    /* 正常 DMA 完成后 BSY 硬件收尾可能滞后于 ISR 回调。
     * 必须在关 SPE 前等 BSY=0（SPE=0 后 SCLK 停振，在途帧永不完成）。
     * 仅在 SPE=1 时等待：abort 恢复路径 SPE 已为 0，跳过。 */
    if (hspi->Instance->CR1 & SPI_CR1_SPE) {
        uint32_t bsy_timeout = 100000U;
        while (__HAL_SPI_GET_FLAG(hspi, SPI_FLAG_BSY) && --bsy_timeout) {}
        if (!bsy_timeout)
            return OM_ERR_IO;
    }

    /* 关 SPI（CR1.SPE=0）才能写 BR/CPOL/CPHA/LSBFIRST/DFF */
    __HAL_SPI_DISABLE(hspi);

    /* 固定字段（Mode/Direction/NSS/TIMode/CRCCalculation/CRCPolynomial）
     * 已在 BSP_SPI_STATIC_INIT 中静态赋值，此处无需重复设置。 */

    /* CPOL/CPHA：SPI_MODE_0..3 直接映射到 CR1.CPOL | CR1.CPHA 位序 */
    hspi->Init.CLKPhase  = (cfg->mode & 0x1U) ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
    hspi->Init.CLKPolarity = (cfg->mode & 0x2U) ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;

    /* BR 分频，同时回写实际频率供框架超时计算 */
    uint32_t actual_hz;
    hspi->Init.BaudRatePrescaler =
        bsp_spi_compute_baudrate(SPI4_INPUT_CLOCK_HZ, cfg->maxHz, &actual_hz);
    bus->actualHz = actual_hz;

    /* 数据宽度 DFF：8/16 */
    hspi->Init.DataSize = (cfg->dataWidth == SPI_DATA_WIDTH_16)
                              ? SPI_DATASIZE_16BIT
                              : SPI_DATASIZE_8BIT;

    /* 位序 LSBFIRST */
    hspi->Init.FirstBit = (cfg->bitOrder == SPI_LSB_FIRST)
                              ? SPI_FIRSTBIT_LSB
                              : SPI_FIRSTBIT_MSB;

    /* 应用配置（HAL 内部会重新计算 CR1/CR2） */
    if (HAL_SPI_Init(hspi) != HAL_OK)
        return OM_ERR_IO;

    __HAL_SPI_ENABLE(hspi);

    /* 上一条传输（尤其是被 ABORT 的异步传输）在 SPE 禁止后 RXNE 不清零，
     * 且 SPE=0 时读 DR 也无法清 RXNE（外设立于禁用态，内部状态机冻结）。
     * 必须在 __HAL_SPI_ENABLE 后读 DR，此时外设已活跃，读 DR 能可靠清
     * RXNE。否则 HAL_SPI_TransmitReceive_DMA 的 RXDMAEN 一开，残留 RXNE
     * 抢在本次 TX 移位完成前触发虚假 DMA 读，NDTR 提前耗尽，rxb 收到旧数据。 */
    if (__HAL_SPI_GET_FLAG(hspi, SPI_FLAG_RXNE))
        (void)READ_REG(hspi->Instance->DR);

    return OM_OK;
}

/**
 * @brief 启动全双工 DMA 传输（异步）
 * @note  tx==NULL → DMA 源指向 &bsp_spi->dummyTx（MINC=0 循环读）
 *        rx==NULL → DMA 目标指向 &bsp_spi->dummyRx（MINC=0 循环写）
 *        DMA MSIZE/PSIZE 在 8/16-bit 模式间动态切换，NDTR 同步调整。
 *        返回 OM_OK 表示 DMA 已启动，真正的完成信号由 DMA RX IRQ 触发。
 *
 *        HAL_SPI_TransmitReceive_DMA 内部会配置两个 DMA stream 的方向/
 *        对齐/长度并启动，最终 RX stream TC 触发 HAL_SPI_TxRxCpltCallback。
 *
 *        DMA_SxCR.MINC/MSIZE/PSIZE 切换契约（RM0090 §9.5.5）：
 *          - CR 配置位写入要求 EN=0
 *          - Normal 模式下，前次传输 NDTR=0 时硬件自动清 EN
 *          - HAL_DMA_Start_IT / DMA_SetConfig 全程不写 CR.MINC/MSIZE/PSIZE
 *          - 故 transferOne 入口时 EN=0 恒成立，可自由切 MINC/MSIZE/PSIZE，len 无上限
 */
static OmRet bsp_spi_transfer_one(SpiBus *bus, const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (!bus)
        return OM_ERR_INVALID_ARG;

    BspSpi_t          bsp_spi = (BspSpi_t)bus->hwPrivate;
    SPI_HandleTypeDef *hspi   = &bsp_spi->handle;
    bool              is16bit = (hspi->Init.DataSize == SPI_DATASIZE_16BIT);

    /* 记录 pendingLen（字节数）供 abort 反算与 ISR 上报 */
    bsp_spi->pendingLen = len;

    /* DMA 数据宽度：8-bit 模式用 BYTE，16-bit 模式用 HALFWORD。
     * NDTR 在 16-bit 时为 len/2（半字数），8-bit 时为 len（字节数）。
     * MSIZE/PSIZE 切换要求 EN=0，Normal 模式前次传输完成后硬件已自动清 EN。 */
    uint32_t dma_width_mask = DMA_SxCR_MSIZE | DMA_SxCR_PSIZE;
    uint32_t dma_width_val;
    uint16_t dma_size;

    DMA_Stream_TypeDef *stream_tx = hspi->hdmatx->Instance;
    DMA_Stream_TypeDef *stream_rx = hspi->hdmarx->Instance;

    if (is16bit) {
        dma_width_val = DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0; /* HALFWORD */
        dma_size      = (uint16_t)(len / 2U);
    } else {
        dma_width_val = 0U; /* BYTE */
        dma_size      = (uint16_t)len;
    }

    MODIFY_REG(stream_tx->CR, dma_width_mask, dma_width_val);
    MODIFY_REG(stream_rx->CR, dma_width_mask, dma_width_val);

    /* tx/rx NULL 处理：DMA_SxCR.MINC 按 NULL 切换。
     * 非 NULL → MINC=1（连续 buffer），NULL → MINC=0（单元素循环）。 */
    const uint8_t *tx_src;
    if (tx) {
        SET_BIT(stream_tx->CR, DMA_SxCR_MINC);
        tx_src = tx;
    } else {
        CLEAR_BIT(stream_tx->CR, DMA_SxCR_MINC);
        tx_src = (const uint8_t *)&bsp_spi->dummyTx;
    }

    uint8_t *rx_dst;
    if (rx) {
        SET_BIT(stream_rx->CR, DMA_SxCR_MINC);
        rx_dst = rx;
    } else {
        CLEAR_BIT(stream_rx->CR, DMA_SxCR_MINC);
        rx_dst = (uint8_t *)&bsp_spi->dummyRx;
    }

    /* SPE=1 后 SPI 可能产生幽灵 RXNE（第二个或更多），出现在 configure 的
     * flush 之后、HAL_SPI_TransmitReceive_DMA 开 RXDMAEN 之前。不在此处清掉，
     * RXDMAEN 一开 DMA 就会预读旧 DR，rxb 收到错误数据。
     * 必须在调用 HAL 前紧邻刷新（bsp_spi_configure 里已做过第一次）。 */
    if (__HAL_SPI_GET_FLAG(hspi, SPI_FLAG_RXNE))
        (void)READ_REG(hspi->Instance->DR);

    /* 启动全双工 DMA。HAL_StatusTypeDef != HAL_OK 时返回错误码，
     * 此时**不得**调 hal_spi_isr（bsp_implementer_checklist.md §3.2）。 */
    HAL_StatusTypeDef hal_ret =
        HAL_SPI_TransmitReceive_DMA(hspi, (uint8_t *)tx_src, rx_dst, dma_size);

    if (hal_ret != HAL_OK)
        return OM_ERR_IO;

    return OM_OK;
}

/**
 * @brief 控制：ABORT / SUSPEND / RESUME
 * @note  ABORT 必须立即停 DMA+SPI 并同步触发一次 hal_spi_isr，
 *        否则框架同步路径会等到超时（bsp_implementer_checklist.md §1.3）。
 */
static OmRet bsp_spi_control(SpiBus *bus, uint32_t cmd, void *arg)
{
    (void)arg;
    if (!bus)
        return OM_ERR_INVALID_ARG;

    BspSpi_t          bsp_spi = (BspSpi_t)bus->hwPrivate;
    SPI_HandleTypeDef *hspi   = &bsp_spi->handle;

    switch (cmd)
    {
    case SPI_CMD_ABORT:
    {
        /* 1. 关闭 DMA（防止继续传输），HAL 内部会 abort 两个 stream */
        if (hspi->hdmatx)
            HAL_DMA_Abort(hspi->hdmatx);
        if (hspi->hdmarx)
            HAL_DMA_Abort(hspi->hdmarx);
        /* 2. 关 SPI */
        __HAL_SPI_DISABLE(hspi);
        /* 3. 反算已传输字节数：pendingLen - RX DMA 剩余计数。
         *    16-bit 模式下 NDTR 是半字数，需 ×2 转为字节数。 */
        size_t transferred = 0U;
        if (hspi->hdmarx)
        {
            size_t remain = __HAL_DMA_GET_COUNTER(hspi->hdmarx);
            if (hspi->Init.DataSize == SPI_DATASIZE_16BIT)
                remain *= 2U;
            transferred = (bsp_spi->pendingLen >= remain)
                              ? (bsp_spi->pendingLen - remain)
                              : 0U;
        }
        /* 6. 触发一次 hal_spi_isr，status=OM_ERR_IO（无专用 ABORTED 错误码），
         *    transferred=已传字节数。框架 hal_spi_isr 会原样传给调用者。 */
        hal_spi_isr(bus, OM_ERR_IO, transferred);
        return OM_OK;
    }

    case SPI_CMD_SUSPEND:
    {
        /* 关 SPI 时钟：所有设备都 suspended 时由框架触发（hal_spi.c:839-841） */
        __HAL_RCC_SPI4_CLK_DISABLE();
        return OM_OK;
    }

    case SPI_CMD_RESUME:
    {
        /* 开 SPI 时钟：第一个设备 resume 时由框架触发（hal_spi.c:864-866） */
        __HAL_RCC_SPI4_CLK_ENABLE();
        /* resume 后配置缓存失效（框架自己在 hal_spi.c:872 已清 cachedDevice） */
        return OM_OK;
    }

    default:
        /* SET_CFG / GET_CFG 由框架自己处理，不会转给 BSP */
        return OM_ERR_INVALID_ARG;
    }
}

/* A 板 CS 默认走 GPIO 路径（cfg->csSpec.controller 非 NULL），
 * 硬件 CS 模式不实现，setCs 置 NULL。
 * 框架在 spi_cs_assert_dev/deassert_dev 中检查 setCs != NULL 才调用。 */

/*===========================================================================
 * SpiControllerOps 实例
 *===========================================================================*/

static SpiControllerOps gSpiOps = {
    .configure   = bsp_spi_configure,
    .transferOne = bsp_spi_transfer_one,
    .control     = bsp_spi_control,
    .setCs       = NULL,   /* 硬件 CS 不实现，A 板 CS 走 GPIO 路径 */
};

/* 契约守卫：板 shim bsp_spi.h 必须定义实例数 */
#ifndef BSP_SPI_COUNT
#error "bsp_spi_f4.c requires BSP_SPI_COUNT (define in board bsp_spi.h shim)"
#endif

/*===========================================================================
 * 注册入口（实例表 gBspSpi[] 由板数据提供）
 *===========================================================================*/

void bsp_spi_register(void)
{
    for (uint8_t i = 0U; i < BSP_SPI_COUNT; i++)
    {
        /* 1. 硬件预初始化：GPIO/AF/DMA link/NVIC（必须先于 bus_register，
         *    因为 spi_bus_register 不做任何硬件工作，只分配 lock/completion） */
        bsp_spi_pre_init(&gBspSpi[i]);

        /* 2. 注册 SpiBus 到框架 */
        OmRet ret = spi_bus_register(&gBspSpi[i].parent,
                                         &gBspSpi[i],
                                         &gSpiOps);
        (void)ret; /* TODO: 失败处理策略待框架层统一 */
    }
}

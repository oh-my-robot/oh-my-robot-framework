/**
 * @file    bsp_spi_f4_init.c
 * @brief   STM32F4 家族 SPI BSP 硬件资源初始化（板瘦身共享适配层；由 rm-a 唯一实现上移，
 *          逐实例块由板 shim 的 USE_SPI4 宏与 DMA 映射宏驱动）
 * @details 职责：
 *          1. 静态 DMA_HandleTypeDef 定义（每个 SPI 实例一对 TX/RX）
 *          2. _bsp_spiN_pre_init：开外设/AF/DMA 时钟、配 GPIO AF、
 *             __HAL_LINKDMA 链接、DMA stream/channel 配置、NVIC 优先级
 *          3. bsp_spi_pre_init：按 SPI 实例分派
 *
 *          不在此文件做：(a) SPI 控制寄存器配置（CPOL/CPHA/BR/DFF，由
 *          bsp_spi_impl.c 的 configure 按 SpiDeviceCfg 实时配置）；
 *          (b) CS GPIO 配置（CS 走框架 GPIO 路径，BSP 不参与）。
 *
 *          SPI4 引脚（AF5）：
 *            PE12 — SCLK
 *            PE5  — MISO
 *            PE6  — MOSI
 *            PE4  — NSS（GPIO CS，框架直控）
 *
 * @note    NVIC 优先级约定（任务约束 #4）：
 *          - TX DMA IRQ  = preempt 6, sub 0
 *          - RX DMA IRQ  = preempt 5, sub 0（RX 作为最终完成信号，略高）
 *          - SPI 外设 IRQ= preempt 6, sub 0（错误中断）
 *          全部 preempt 数值 >= FreeRTOS configMAX_SYSCALL_INTERRUPT_PRIORITY
 *          阈值（默认 5），保证内核临界区安全。
 */

#include "bsp_spi.h"

/*===========================================================================
 * 静态 DMA 资源定义
 *===========================================================================*/

#ifdef USE_SPI4
static DMA_HandleTypeDef hdma_spi4_tx;
static DMA_HandleTypeDef hdma_spi4_rx;
#endif /* USE_SPI4 */

/*===========================================================================
 * 单实例预初始化
 *===========================================================================*/

#ifdef USE_SPI4
/**
 * @brief SPI4 预初始化
 * @param bsp_spi        SPI4 实例
 * @param is_enable_int  非 0 = 使能 SPI 外设自身 NVIC（错误中断）
 *
 * 引脚映射（A 板手册）：
 *   PE12 ------> SPI4_SCK  (AF5)
 *   PE5  ------> SPI4_MISO (AF5)
 *   PE6  ------> SPI4_MOSI (AF5)
 *   PE4         GPIO CS（框架直控，不在此处配置）
 */
static void _bsp_spi4_pre_init(BspSpi_t bsp_spi, uint8_t is_enable_int)
{
    SPI_HandleTypeDef *hspi = &bsp_spi->handle;

    /* 1. 开时钟：SPI4 + GPIOE + DMA2 */
    __HAL_RCC_SPI4_CLK_ENABLE();
    if (__HAL_RCC_GPIOE_IS_CLK_DISABLED())
        __HAL_RCC_GPIOE_CLK_ENABLE();
    if (__HAL_RCC_DMA2_IS_CLK_DISABLED())
        __HAL_RCC_DMA2_CLK_ENABLE();

    /* 2. GPIO AF5_SPI4：PE12=SCK, PE5=MISO, PE6=MOSI */
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin       = GPIO_PIN_12 | GPIO_PIN_5 | GPIO_PIN_6;
    gpio_init.Mode      = GPIO_MODE_AF_PP;
    gpio_init.Pull      = GPIO_NOPULL;
    gpio_init.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF5_SPI4;
    HAL_GPIO_Init(GPIOE, &gpio_init);

    /* 3. DMA TX 链接 + stream/channel 配置 + HAL_DMA_Init
     * 必须调 HAL_DMA_Init：(a) 把 Init 字段写进 stream 的 CR/FCR 寄存器；
     * (b) 把 hdma->State 从 RESET 改为 READY，否则 HAL_DMA_Start_IT 会
     * 直接返回 HAL_BUSY，HAL_SPI_TransmitReceive_DMA 包成 HAL_ERROR 返回。 */
    __HAL_LINKDMA(hspi, hdmatx, hdma_spi4_tx);
    hdma_spi4_tx.Instance                 = SPI4_DMA_TX_DMA_STREAM;
    hdma_spi4_tx.Init.Channel             = SPI4_DMA_TX_DMA_CHANNEL;
    hdma_spi4_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_spi4_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_spi4_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_spi4_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi4_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_spi4_tx.Init.Mode                = DMA_NORMAL;
    hdma_spi4_tx.Init.Priority            = DMA_PRIORITY_LOW;
    hdma_spi4_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_spi4_tx);
    HAL_NVIC_SetPriority(SPI4_DMA_TX_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(SPI4_DMA_TX_IRQn);

    /* 4. DMA RX 链接 + stream/channel 配置（作为最终完成信号，优先级略高） */
    __HAL_LINKDMA(hspi, hdmarx, hdma_spi4_rx);
    hdma_spi4_rx.Instance                 = SPI4_DMA_RX_DMA_STREAM;
    hdma_spi4_rx.Init.Channel             = SPI4_DMA_RX_DMA_CHANNEL;
    hdma_spi4_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_spi4_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_spi4_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_spi4_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi4_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_spi4_rx.Init.Mode                = DMA_NORMAL;
    hdma_spi4_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_spi4_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_spi4_rx);
    HAL_NVIC_SetPriority(SPI4_DMA_RX_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(SPI4_DMA_RX_IRQn);

    /* 5. SPI 外设自身 NVIC（MODF/OVR/CRC 错误中断） */
    if (is_enable_int)
    {
        HAL_NVIC_SetPriority(SPI4_IRQn, 6U, 0U);
        HAL_NVIC_EnableIRQ(SPI4_IRQn);
    }
}
#endif /* USE_SPI4 */

/*===========================================================================
 * 分派入口
 *===========================================================================*/

void bsp_spi_pre_init(BspSpi_t bsp_spi)
{
    if (!bsp_spi || !bsp_spi->handle.Instance)
        return;

    uint8_t is_enable_int = 1U;
    switch ((uint32_t)bsp_spi->handle.Instance)
    {
#ifdef USE_SPI4
    case SPI4_BASE:
        _bsp_spi4_pre_init(bsp_spi, is_enable_int);
        break;
#endif
    default:
        /* 未支持的实例，忽略 */
        break;
    }
}

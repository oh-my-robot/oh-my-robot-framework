/**
 * @file    bsp_spi_f4_it.c
 * @brief   STM32F4 家族 SPI BSP 中断处理（板瘦身共享适配层；DMA TX/RX IRQ + SPI 错误 IRQ
 *          + HAL 回调，由 rm-a 唯一实现上移）
 * @details 本文件必须进 override_sources（板 lua），否则 IRQ 强符号
 *          被链接器 GC，vector table 仍指向启动文件中的 weak 默认（死循环）。
 *
 *          三层调用栈（bsp_peripheral_pattern.md §4.1）：
 *            NVIC vector
 *              └── 强符号 IRQHandler（本文件）
 *                    └── HAL_DMA_IRQHandler / HAL_SPI_IRQHandler
 *                          └── HAL_SPI_TxRxCpltCallback / ErrorCallback
 *                                └── hal_spi_isr(parent, status, transferred)
 *
 *          完成信号路径：DMA RX TC（最终完成）→ HAL_SPI_TxRxCpltCallback
 *          错误信号路径：DMA FE/TE/DME 或 SPI MODF/OVR/CRC
 *                     → HAL_SPI_ErrorCallback → hal_spi_isr(OM_ERR_IO, ...)
 */

#include "bsp_spi.h"

/*===========================================================================
 * HAL 回调 → 框架 hal_spi_isr
 *===========================================================================*/

/**
 * @brief SPI 全双工 DMA 传输完成回调
 * @note  HAL 在 RX DMA TC 后调用此回调。此时 TX 必然也已完成（全双工等长）。
 *        hspi 指向 BspSpi_s.handle 字段，用 BSP_SPI_FROM_HANDLE 反查 bsp_spi。
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    BspSpi_t bsp_spi = BSP_SPI_FROM_HANDLE(hspi);
    hal_spi_isr(&bsp_spi->parent, OM_OK, bsp_spi->pendingLen);
}

/**
 * @brief SPI 错误回调（MODF / OVR / CRC / DMA FE-TE-DME）
 * @note  反算已传输字节数：pendingLen - RX DMA 剩余计数。
 *        若 hdmarx 为空（异常路径），上报 transferred=0。
 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    BspSpi_t bsp_spi = BSP_SPI_FROM_HANDLE(hspi);
    size_t   transferred = 0U;

    if (hspi->hdmarx != NULL)
    {
        size_t remain = __HAL_DMA_GET_COUNTER(hspi->hdmarx);
        transferred = (bsp_spi->pendingLen >= remain)
                          ? (bsp_spi->pendingLen - remain)
                          : 0U;
    }

    hal_spi_isr(&bsp_spi->parent, OM_ERR_IO, transferred);
}

/*===========================================================================
 * IRQ handler 强符号（覆盖启动文件 weak symbol）
 *
 * 必须加 __attribute__((used))：
 *   armclang LTO 做全程序 dead-code-elimination 时，看不到启动文件（汇编）
 *   中向量表对 IRQ handler 的引用，会把"没被 C 代码显式调用"的 IRQ handler
 *   当作死代码丢弃，导致向量表条目最终指向启动文件末尾的 weak 死循环。
 *   `used` attribute 在 LLVM LTO 下生成 llvm.compiler.used intrinsic，
 *   阻止优化器消除代码段。
 *===========================================================================*/

#ifdef USE_SPI4
/**
 * @brief SPI4 DMA TX 完成中断
 * @note  TX TC 仅表示发送 buffer 已被 DMA 读完，不代表 SPI 移位完成。
 *        最终完成信号以 RX DMA TC 为准。
 */
__attribute__((used))
void SPI4_DMA_TX_IRQ_Handler(void)
{
    BspSpi_t bsp_spi = &gBspSpi[BSP_SPI4_IDX];
    HAL_DMA_IRQHandler(bsp_spi->handle.hdmatx);
}

/**
 * @brief SPI4 DMA RX 完成中断（最终完成信号）
 * @note  RX TC 表示接收 buffer 已满，即 SPI 也完成了最后一位的移位。
 *        HAL 内部会调 HAL_SPI_TxRxCpltCallback → hal_spi_isr。
 */
__attribute__((used))
void SPI4_DMA_RX_IRQ_Handler(void)
{
    BspSpi_t bsp_spi = &gBspSpi[BSP_SPI4_IDX];
    HAL_DMA_IRQHandler(bsp_spi->handle.hdmarx);
}

/**
 * @brief SPI4 外设中断（MODF / OVR / CRCERR / FRE）
 * @note  HAL 内部会清标志并调 HAL_SPI_ErrorCallback。
 */
__attribute__((used))
void SPI4_IRQHandler(void)
{
    BspSpi_t bsp_spi = &gBspSpi[BSP_SPI4_IDX];
    HAL_SPI_IRQHandler(&bsp_spi->handle);
}
#endif /* USE_SPI4 */

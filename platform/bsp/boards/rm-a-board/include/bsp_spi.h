/**
 * @file    bsp_spi.h
 * @brief   rm-a-board SPI 板配置 shim（板瘦身：类型/契约已上移 spi/bsp_spi_f4.h）
 * @details 本文件只留板配置：实例裁剪 + DMA stream/channel/NVIC 映射宏（含实测教训注释）。
 *
 *          SPI4 引脚（AF5）：
 *            PE12 — SCLK
 *            PE5  — MISO
 *            PE6  — MOSI
 *            PE4  — NSS（GPIO CS，由框架直控）
 */

#ifndef BSP_SPI_H
#define BSP_SPI_H
#ifdef OM_USE_BOARDCFG
#include "om_boardcfg.h" /* 工程板级覆写（boardcfg 契约见 ADR-0017）——须在默认值定义之前 */
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "drivers/peripheral/spi/pal_spi_dev.h"

/*===========================================================================
 * 实例裁剪与配置宏
 *===========================================================================*/

#define USE_SPI4
#ifdef USE_SPI4
/* SPI4 在 APB2（90MHz），DMA 必须走 DMA2。
 *
 * STM32F427 DMA2 request mapping（RM0090 §9.3.3 Table 43，硬件布线，非 LL_DMA_REQUEST）：
 *   SPI4_TX = DMA2 Stream4 Channel5（F427 唯一可用映射，Table 43 footnote 标 F42x/F43x-only）
 *   SPI4_RX = DMA2 Stream3 Channel5
 *
 * 实测教训（任务 #54）：
 *   (1) Stream5/Ch4 — F427 硬件不路由 SPI4_TX 请求到 Stream5，NDTR 永不递减、ISR 不触发
 *   (2) Stream1/Ch4 — 硬件可路由，但 IRQ 与 serial6_rx 占用的 DMA2_Stream1_IRQHandler
 *       符号冲突（STM32 IRQ 按 stream 共享，不能按 channel 复用），链接器报 L6123E
 *       symbol multiply defined
 *   (3) Stream4/Ch5 — RM0090 Table 43 中 F427 唯一合法的 SPI4_TX 映射，IRQ（DMA2_Stream4_IRQn
 *       =60）空闲，符号无冲突
 *
 * 已知被 Serial 占用的 DMA2 stream（见 bsp_serial.h）：
 *   Stream7/Ch4 = serial1_tx,   Stream2/Ch4 = serial1_rx
 *   Stream6/Ch5 = serial6_tx,   Stream1/Ch5 = serial6_rx
 *   （Stream3/4 不被任何 serial 占用，可放心给 SPI4 用）
 */
#define SPI4_DMA_TX_DMA_STREAM   (DMA2_Stream4)
#define SPI4_DMA_TX_DMA_CHANNEL  (DMA_CHANNEL_5)
#define SPI4_DMA_TX_IRQn         (DMA2_Stream4_IRQn)
#define SPI4_DMA_TX_IRQ_Handler  (DMA2_Stream4_IRQHandler)

#define SPI4_DMA_RX_DMA_STREAM   (DMA2_Stream3)
#define SPI4_DMA_RX_DMA_CHANNEL  (DMA_CHANNEL_5)
#define SPI4_DMA_RX_IRQn         (DMA2_Stream3_IRQn)
#define SPI4_DMA_RX_IRQ_Handler  (DMA2_Stream3_IRQHandler)

/* SPI4 输入时钟（APB2 = HCLK/2 = 180/2 = 90MHz），用于 maxHz 分频计算 */
#define SPI4_INPUT_CLOCK_HZ      (90000000UL)
#endif /* USE_SPI4 */

/* 实例数：必须等于 bsp_spi_data.c 中 gBspSpi 条目数（数据文件内编译期校验） */
#define BSP_SPI_COUNT (1U)

#include "spi/bsp_spi_f4.h"

#ifdef __cplusplus
}
#endif

#endif /* BSP_SPI_H */

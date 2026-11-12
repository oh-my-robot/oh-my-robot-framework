/**
 * @file  bsp_spi_f4.h
 * @brief STM32F4 家族 SPI BSP 共享适配层：类型 + 板数据契约
 * @details 板瘦身（见 ADR-0011）：F4 家族一份共享实现（impl/init/it），板只提供
 *          实例表（gBspSpi[]）与板配置宏（USE_SPIx、DMA 映射留板 shim）。
 *
 *          ── 板数据契约（板 opt-in 本适配层后必须提供）────────────────────
 *          板侧 include/bsp_spi.h（shim）：
 *            USE_SPI4 / SPI4_DMA_{TX,RX}_DMA_STREAM/CHANNEL/IRQn / SPI4_INPUT_CLOCK_HZ
 *            #define BSP_SPI_COUNT   实例数（= gBspSpi 条目数）
 *            #include "spi/bsp_spi_f4.h"
 *          板侧 source/peripherals/spi/bsp_spi_data.c：
 *            BspSpi_s gBspSpi[BSP_SPI_COUNT]   实例表（USE_SPIx 守卫）
 *          适配层文件由板 lua 显式引用（opt-in 铁律，永不进 vendor/chip sources）：
 *            selfreg_sources  += .../adapters/spi/bsp_spi_f4.c
 *            override_sources += .../adapters/spi/bsp_spi_f4_it.c
 *            （bsp_spi_f4_init.c 随 impl 一起进 selfreg_sources）
 */

#ifndef BSP_SPI_F4_H
#define BSP_SPI_F4_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "drivers/peripheral/spi/pal_spi_dev.h"

/*===========================================================================
 * 实例索引枚举（实例数由板 shim 的 BSP_SPI_COUNT 宏定义）
 *===========================================================================*/

typedef enum
{
#ifdef USE_SPI4
    BSP_SPI4_IDX,
#endif
} BspSpiIdx_e;

/*===========================================================================
 * BspSpi_s —— BSP 实例结构（含 SpiBus 父类 + HAL 句柄）
 *===========================================================================*/

typedef struct BspSpi *BspSpi_t;

/**
 * @brief SPI BSP 实例
 * @note  第 1 字段为 SpiBus parent（向上注册可首字段强转）；handle 放第 2 字段，
 *        HAL 回调中反查须用 BSP_SPI_FROM_HANDLE(hspi)（基于 offsetof 反查）。
 */
typedef struct BspSpi
{
    SpiBus             parent;       /* 第 1 字段：框架 SpiBus */
    SPI_HandleTypeDef  handle;       /* 第 2 字段：HAL 句柄（含 hdmatx/hdmarx 链接） */

    /* DMA dummy 缓冲：tx/rx==NULL 时 DMA 源/目标。uint16_t 兼容 8/16-bit
     * 两种数据宽度（16-bit 模式下 DMA HALFWORD 访问需 2 字节对齐）。 */
    uint16_t           dummyTx;      /* tx==NULL 时 DMA 源，固定 0xFFFF */
    uint16_t           dummyRx;      /* rx==NULL 时 DMA 目标，丢弃 */
    size_t             pendingLen;   /* 当前传输总长，abort 反算用 */
} BspSpi_s;

#define BSP_SPI_FROM_HANDLE(hspi) \
    ((BspSpi_t)((char *)(hspi) - offsetof(BspSpi_s, handle)))

/**
 * @brief 静态初始化宏（用于全局 gBspSpi[] 数组）
 * @note  dummyTx = 0xFFFF（SPI 约定 dummy），dummyRx = 0（丢弃）。
 *        固定 Init 字段（Mode/Direction/NSS/TIMode/CRCCalculation/CRCPolynomial）
 *        在此静态赋初值，configure 中不再重复设置。
 */
#define BSP_SPI_STATIC_INIT(INSTANCE)                            \
    (BspSpi_s)                                                     \
    {                                                              \
        .handle.Instance = (INSTANCE),                             \
        .handle.Init     = {                                       \
            .Mode           = SPI_MODE_MASTER,                     \
            .Direction      = SPI_DIRECTION_2LINES,                \
            .NSS            = SPI_NSS_SOFT,                        \
            .TIMode         = SPI_TIMODE_DISABLE,                  \
            .CRCCalculation = SPI_CRCCALCULATION_DISABLE,          \
            .CRCPolynomial  = 7U,                                  \
        },                                                         \
        .dummyTx         = 0xFFFFU,                                \
        .dummyRx         = 0x0000U,                                \
        .pendingLen      = 0U,                                     \
    }

/*===========================================================================
 * 板数据契约声明
 *===========================================================================*/

extern BspSpi_s gBspSpi[BSP_SPI_COUNT]; /* BSP_SPI_COUNT 由板 shim 定义 */

void bsp_spi_register(void);
void bsp_spi_pre_init(BspSpi_t bsp_spi);

#ifdef __cplusplus
}
#endif

#endif /* BSP_SPI_F4_H */

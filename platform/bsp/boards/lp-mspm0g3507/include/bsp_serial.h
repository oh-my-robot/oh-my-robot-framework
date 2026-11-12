/**
 * @file bsp_serial.h
 * @brief MSPM0G3507 串口 BSP 层 — 基于 DL_UART + DL_DMA
 *
 * 串口资源：
 *   UART1 (PA8=TX, PA9=RX)  2Mbps  VOFA 调试 + printf
 *   UART2 (PA21=TX, PA22=RX) 115200 TJC HMI 串口屏
 *
 * DMA 通道分配：
 *   DMA CH1 → UART1 TX     DMA CH2 → UART1 RX
 *   DMA CH3 → UART2 TX     DMA CH4 → UART2 RX
 */

#ifndef __OM_BSP_SERIAL_H__
#define __OM_BSP_SERIAL_H__

#include "drivers/peripheral/serial/pal_serial_dev.h"
#include "ti/devices/msp/msp.h"
#include "ti/driverlib/m0p/dl_core.h"

/*---------------------------------------------------------------------------*/
/* 串口使能裁剪                                                              */
/*---------------------------------------------------------------------------*/

#define USE_SERIAL_1
#define USE_SERIAL_2

/*---------------------------------------------------------------------------*/
/* UART1 — VOFA 调试 (2Mbps, DMA)                                           */
/*---------------------------------------------------------------------------*/

#ifdef USE_SERIAL_1
#define SERIAL_1_REG_PARAMS  (SERIAL_REG_DMA_RX | SERIAL_REG_DMA_TX)
#define USE_SERIAL1_DMA_TX
#define USE_SERIAL1_DMA_RX
#ifdef USE_SERIAL1_DMA_TX
#define SERIAL_1_DMA_TX_CH        1
#define SERIAL_1_DMA_TX_TRIGSRC   DMA_UART1_TX_TRIG
#define SERIAL_1_DMA_TX_IRQn      DMA_INT_IRQn
#endif
#ifdef USE_SERIAL1_DMA_RX
#define SERIAL_1_RX_MULTIBUF_SIZE (256U)
#define USE_SERIAL1_CONTAINER1
#define SERIAL_1_DMA_RX_CH        2
#define SERIAL_1_DMA_RX_TRIGSRC   DMA_UART1_RX_TRIG
#define SERIAL_1_DMA_RX_IRQn      DMA_INT_IRQn
#endif
#endif

/*---------------------------------------------------------------------------*/
/* UART2 — TJC HMI 串口屏 (115200, DMA)                                      */
/*---------------------------------------------------------------------------*/

#ifdef USE_SERIAL_2
#define SERIAL_2_REG_PARAMS  (SERIAL_REG_DMA_RX | SERIAL_REG_DMA_TX)
#define USE_SERIAL2_DMA_TX
#define USE_SERIAL2_DMA_RX
#ifdef USE_SERIAL2_DMA_TX
#define SERIAL_2_DMA_TX_CH        3
#define SERIAL_2_DMA_TX_TRIGSRC   DMA_UART2_TX_TRIG
#define SERIAL_2_DMA_TX_IRQn      DMA_INT_IRQn
#endif
#ifdef USE_SERIAL2_DMA_RX
#define SERIAL_2_RX_MULTIBUF_SIZE (256U)
#define USE_SERIAL2_CONTAINER1
#define SERIAL_2_DMA_RX_CH        4
#define SERIAL_2_DMA_RX_TRIGSRC   DMA_UART2_RX_TRIG
#define SERIAL_2_DMA_RX_IRQn      DMA_INT_IRQn
#endif
#endif

/*---------------------------------------------------------------------------*/
/* 枚举 / 结构体                                                             */
/*---------------------------------------------------------------------------*/

typedef enum {
#ifdef USE_SERIAL_1
    SERIAL1_IDX,
#endif
#ifdef USE_SERIAL_2
    SERIAL2_IDX,
#endif
    SERIAL_COUNT
} SerialIdx_e;

typedef struct bsp_serial_mutibuf {
    uint8_t  *container0;
    uint8_t  *container1;
    size_t    container_len;
    size_t    last_rx_cnt;
} bsp_serial_mutibuf_s, *bsp_serial_mutibuf_t;

/**
 * @brief MSPM0 串口实例
 * @note  handle 必须是第一个成员 —— bsp_serial_impl.c 中通过
 *        &handle 做指针转换得到 bsp_serial_t
 */
typedef struct bsp_serial {
    UART_Regs *          handle;       /* UART 外设基地址（必须第一） */
    HalSerial             parent;       /* 框架串口实例 */
    const char *          name;
    uint32_t              regparams;    /* REG_DMA_TX | REG_DMA_RX */
    bsp_serial_mutibuf_t  rx_multibuf;  /* DMA 接收多缓冲区 */
    size_t                tx_xfer_size; /* 当前 TX 传输长度（ISR 需传给框架）*/
    const uint8_t         *tx_data;      /* INT TX 模式：当前数据指针 */
    size_t                tx_remaining;  /* INT TX 模式：剩余字节数 */
    /* DMA 通道 ID */
    uint8_t               dma_tx_ch;
    uint8_t               dma_rx_ch;
} bsp_serial_s, *bsp_serial_t;

/*---------------------------------------------------------------------------*/
/* 宏                                                                        */
/*---------------------------------------------------------------------------*/

#define BSP_SERIAL_STATIC_INIT(_handle, _name, _regparams, _tx_ch, _rx_ch)  \
    (bsp_serial_s) {                                                        \
        .handle    = (UART_Regs *)(_handle),                                \
        .name      = (_name),                                               \
        .regparams = (_regparams),                                          \
        .dma_tx_ch = (_tx_ch),                                              \
        .dma_rx_ch = (_rx_ch),                                              \
    }

#define BSP_SERIAL_MULTIBUF_STATIC_INIT(c0, c1, len)                        \
    (bsp_serial_mutibuf_s){                                                 \
        .container0 = (c0), .container1 = (c1),                             \
        .container_len = (len), .last_rx_cnt = 0                            \
    }

#define BSP_SERIAL_MULTIBUF_DEF(name, c0, c1, len)                          \
    bsp_serial_mutibuf_s name = BSP_SERIAL_MULTIBUF_STATIC_INIT(c0, c1, len)

/*---------------------------------------------------------------------------*/
/* 全局 / 函数声明                                                           */
/*---------------------------------------------------------------------------*/

extern bsp_serial_s g_bsp_serial[];

void bsp_serial_pre_init(bsp_serial_t s);
void bsp_serial_register(void);
void bsp_serial_dma_cfg(bsp_serial_t s, uint32_t dma_regparams);

/* 中断处理 — 由 serial_it.c 实现，串口 ISR 中调用 */
void bsp_serial_dmarx_isr(bsp_serial_t s, uint8_t dma_ch);
void bsp_serial_dmatx_isr(bsp_serial_t s, uint8_t dma_ch);
void bsp_serial_uart_isr(bsp_serial_t s);

#endif /* __OM_BSP_SERIAL_H__ */

/**
 * @file bsp_serial.h
 * @brief 串口bsp层头文件，职责如下：
 *          1、声明bsp层串口函数
 *          2、定义串口裁剪宏、配置宏
 * @note 1、宏说明：
 *
 */

#ifndef __OM_BSP_SERIAL_H__
#define __OM_BSP_SERIAL_H__

#include "drivers/peripheral/serial/pal_serial_dev.h"

#include "stm32f4xx_hal.h"

#define USE_SERIAL_1
#ifdef USE_SERIAL_1
#define SERIAL_1_REG_PARAMS (SERIAL_REG_DMA_RX | SERIAL_REG_DMA_TX) /* 注册参数 */
#define USE_SERIAL1_DMA_TX                                          /* 使用DMA发送 */
#define USE_SERIAL1_DMA_RX                                          /* 使用DMA接收 */
#ifdef USE_SERIAL1_DMA_TX
#define SERIAL_1_DMA_TX_DMA_STREAM DMA2_Stream7
#define SERIAL_1_DMA_TX_DMA_CHANNEL DMA_CHANNEL_4
#define SERIAL_1_DMA_TX_IRQn DMA2_Stream7_IRQn
#define SERIAL_1_DMA_TX_IRQ_Handler DMA2_Stream7_IRQHandler
#endif
#ifdef USE_SERIAL1_DMA_RX
#define SERIAL_1_RX_MULTIBUF_SIZE (256U) /* 多缓冲区接收长度 */
#define USE_SERIAL1_CONTAINER1           /* 使用多缓冲区接收 */
#define SERIAL_1_DMA_RX_DMA_STREAM DMA2_Stream2
#define SERIAL_1_DMA_RX_DMA_CHANNEL DMA_CHANNEL_4
#define SERIAL_1_DMA_RX_IRQn DMA2_Stream2_IRQn
#define SERIAL_1_DMA_RX_IRQ_Handler DMA2_Stream2_IRQHandler
#endif
#endif

#define USE_SERIAL_3
#ifdef USE_SERIAL_3
#define SERIAL_3_REG_PARAMS (SERIAL_REG_DMA_RX | SERIAL_REG_DMA_TX) /* 注册参数 */
#define USE_SERIAL3_DMA_TX                                          /* 使用DMA发送 */
#define USE_SERIAL3_DMA_RX                                          /* 使用DMA接收 */
#ifdef USE_SERIAL3_DMA_TX
#define SERIAL_3_DMA_TX_DMA_STREAM DMA1_Stream3
#define SERIAL_3_DMA_TX_DMA_CHANNEL DMA_CHANNEL_4
#define SERIAL_3_DMA_TX_IRQn DMA1_Stream3_IRQn
#define SERIAL_3_DMA_TX_IRQ_Handler DMA1_Stream3_IRQHandler
#endif
#ifdef USE_SERIAL3_DMA_RX
#define SERIAL_3_RX_MULTIBUF_SIZE (256U) /* 多缓冲区接收长度 */
#define USE_SERIAL3_CONTAINER1           /* 使用多缓冲区接收 */
#define SERIAL_3_DMA_RX_DMA_STREAM DMA1_Stream1
#define SERIAL_3_DMA_RX_DMA_CHANNEL DMA_CHANNEL_4
#define SERIAL_3_DMA_RX_IRQn DMA1_Stream1_IRQn
#define SERIAL_3_DMA_RX_IRQ_Handler DMA1_Stream1_IRQHandler
#endif
#endif

#define USE_SERIAL_7
#ifdef USE_SERIAL_7
#define SERIAL_7_REG_PARAMS (SERIAL_REG_INT_RX | SERIAL_REG_INT_TX) /* 注册参数 */
#endif

#define USE_SERIAL_8
#ifdef USE_SERIAL_8
#define SERIAL_8_REG_PARAMS (SERIAL_REG_INT_RX | SERIAL_REG_INT_TX) /* 注册参数 */
#endif

/* 串口配置宏 */
#define USE_SERIAL_6
#ifdef USE_SERIAL_6
#define SERIAL_6_REG_PARAMS (SERIAL_REG_DMA_RX | SERIAL_REG_DMA_TX) /* 注册参数 */
#define USE_SERIAL6_DMA_TX                                          /* 使用DMA发送 */
#define USE_SERIAL6_DMA_RX                                          /* 使用DMA接收 */
#ifdef USE_SERIAL6_DMA_TX
#define SERIAL_6_DMA_TX_DMA_STREAM DMA2_Stream6
#define SERIAL_6_DMA_TX_DMA_CHANNEL DMA_CHANNEL_5
#define SERIAL_6_DMA_TX_IRQn DMA2_Stream6_IRQn
#define SERIAL_6_DMA_TX_IRQ_Handler DMA2_Stream6_IRQHandler
#endif
#ifdef USE_SERIAL6_DMA_RX
#define SERIAL_6_RX_MULTIBUF_SIZE (256U) /* 多缓冲区接收长度 */
#define USE_SERIAL6_CONTAINER1           /* 使用多缓冲区接收 */
#define SERIAL_6_DMA_RX_DMA_STREAM DMA2_Stream1
#define SERIAL_6_DMA_RX_DMA_CHANNEL DMA_CHANNEL_5
#define SERIAL_6_DMA_RX_IRQn DMA2_Stream1_IRQn
#define SERIAL_6_DMA_RX_IRQ_Handler DMA2_Stream1_IRQHandler
#endif
#endif

typedef enum
{
#ifdef USE_SERIAL_1
    SERIAL1_IDX,
#endif
#ifdef USE_SERIAL_2
    SERIAL2_IDX,
#endif
#ifdef USE_SERIAL_3
    SERIAL3_IDX,
#endif
#ifdef USE_SERIAL_4
    SERIAL4_IDX,
#endif
#ifdef USE_SERIAL_5
    SERIAL5_IDX,
#endif
#ifdef USE_SERIAL_6
    SERIAL6_IDX,
#endif
#ifdef USE_SERIAL_7
    SERIAL7_IDX,
#endif
#ifdef USE_SERIAL_8
    SERIAL8_IDX,
#endif
#ifdef USE_SERIAL_9
    SERIAL8_IDX,
#endif
#ifdef USE_SERIAL_10
    SERIAL8_IDX,
#endif
} SerialIdx_e;

typedef struct bsp_serial_mutibuf* bsp_serial_mutibuf_t;
typedef struct bsp_serial_mutibuf
{
    uint8_t* container0;  // 缓冲区地址0
    uint8_t* container1;  // 缓冲区地址1
    size_t container_len; // 缓冲区接收长度
    size_t last_rx_cnt;   // 上次rxdma指针，仅开启DMA时启用
#ifdef STM32_H7_Serials
    dma_type_e dmaType; // DMA类型
#endif
} bsp_serial_mutibuf_s;

typedef struct bsp_serial* bsp_serial_t;
typedef struct bsp_serial
{
    UART_HandleTypeDef handle; // 一定要放在第一位
    HalSerial parent;
    char* name;
    uint32_t regparams;
    bsp_serial_mutibuf_t rx_multibuf; // 多缓冲区接收
} bsp_serial_s;

#define BSP_SERIAL_STATIC_INIT(INSTANCE, NAME, REGPARAMS)                                                                                  \
    (bsp_serial_s)                                                                                                                         \
    {                                                                                                                                      \
        .handle.Instance = (INSTANCE), .name = (NAME), .regparams = (REGPARAMS),                                                           \
    }

#define BSP_SERIAL_MULTIBUF_STATIC_INIT(CONTAINER0, CONTAINER1, CONTAINER_LEN)                                                             \
    (bsp_serial_mutibuf_s){.container0 = CONTAINER0, .container1 = CONTAINER1, .container_len = CONTAINER_LEN, .last_rx_cnt = 0}

#define BSP_SERIAL_MULTIBUF_DEF(NAME, CONTAINER0, CONTAINER1, CONTAINER_LEN)                                                               \
    bsp_serial_mutibuf_s NAME = BSP_SERIAL_MULTIBUF_STATIC_INIT(CONTAINER0, CONTAINER1, CONTAINER_LEN)

extern bsp_serial_s g_bsp_serial[];

void bsp_serial_pre_init(bsp_serial_t bsp_serial);
void bsp_serial_register(void);
void bsp_serial_dma_cfg(bsp_serial_t bsp_serial, uint32_t dma_regparams);

#endif

/**
 * @file bsp_serial.h
 * @brief rm-a-board 串口板配置 shim（板瘦身：类型/契约已上移 serial/bsp_serial_f4.h）
 * @details 本文件只留板配置（实例裁剪 + DMA profile + 寄存器参数）；全部类型见共享适配层头。
 */

#ifndef __OM_BSP_SERIAL_H__
#define __OM_BSP_SERIAL_H__
#ifdef OM_USE_BOARDCFG
#include "om_boardcfg.h" /* 工程板级覆写（boardcfg 契约见 ADR-0017）——须在默认值定义之前 */
#endif

#include "drivers/peripheral/serial/pal_serial_dev.h"

#include "stm32f4xx_hal.h"

/*
 * USART3_RX 与 UART7_TX 共享 DMA1_Stream1，因此 USART3 全 DMA 与
 * UART7 全 DMA 不能同时成立。
 *
 * rm-a-board 提供两套板级私有 profile：
 * 1. USART3 全 DMA + UART7 全中断（兼容旧行为）
 * 2. USART3 TX-DMA/RX-INT + UART7 全 DMA（默认）
 */
#define RM_A_SERIAL37_PROFILE_USART3_FULLDMA_UART7_INT   (0U)
#define RM_A_SERIAL37_PROFILE_USART3_TXDMA_UART7_FULLDMA (1U)

/* 兼容旧命名，避免打断已有命令行宏。 */
#define RM_A_SERIAL_DMA_PROFILE_USART3 RM_A_SERIAL37_PROFILE_USART3_FULLDMA_UART7_INT
#define RM_A_SERIAL_DMA_PROFILE_UART7  RM_A_SERIAL37_PROFILE_USART3_TXDMA_UART7_FULLDMA

#ifndef RM_A_SERIAL37_PROFILE
#ifdef RM_A_SERIAL_DMA_PROFILE
#define RM_A_SERIAL37_PROFILE RM_A_SERIAL_DMA_PROFILE
#else
#define RM_A_SERIAL37_PROFILE RM_A_SERIAL37_PROFILE_USART3_TXDMA_UART7_FULLDMA
#endif
#endif

#ifndef RM_A_SERIAL_DMA_PROFILE
#define RM_A_SERIAL_DMA_PROFILE RM_A_SERIAL37_PROFILE
#endif

/* 注：A 板未引出 USART1——不得定义 USE_SERIAL_1。
 * SerialIdx_e 枚举由 USE_SERIAL_* 宏驱动，残留该宏会导致枚举与
 * g_bsp_serial 数据表索引错位（所有串口 ISR 按枚举取句柄 → 静默取错设备，
 * 表现为 DMA 完成中断风暴/传输停滞——2026-08-20 实测根因）。 */

#define USE_SERIAL_3
#ifdef USE_SERIAL_3
#if (RM_A_SERIAL37_PROFILE == RM_A_SERIAL37_PROFILE_USART3_FULLDMA_UART7_INT)
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
#else
#define SERIAL_3_REG_PARAMS (SERIAL_REG_INT_RX | SERIAL_REG_DMA_TX) /* 注册参数 */
#define USE_SERIAL3_DMA_TX                                          /* 使用DMA发送 */
#ifdef USE_SERIAL3_DMA_TX
#define SERIAL_3_DMA_TX_DMA_STREAM DMA1_Stream4
#define SERIAL_3_DMA_TX_DMA_CHANNEL DMA_CHANNEL_7
#define SERIAL_3_DMA_TX_IRQn DMA1_Stream4_IRQn
#define SERIAL_3_DMA_TX_IRQ_Handler DMA1_Stream4_IRQHandler
#endif
#endif
#endif

#define USE_SERIAL_7
#ifdef USE_SERIAL_7
#if (RM_A_SERIAL37_PROFILE == RM_A_SERIAL37_PROFILE_USART3_TXDMA_UART7_FULLDMA)
#define SERIAL_7_REG_PARAMS (SERIAL_REG_DMA_RX | SERIAL_REG_DMA_TX) /* 注册参数 */
#define USE_SERIAL7_DMA_TX                                          /* 使用DMA发送 */
#define USE_SERIAL7_DMA_RX                                          /* 使用DMA接收 */
#ifdef USE_SERIAL7_DMA_TX
#define SERIAL_7_DMA_TX_DMA_STREAM DMA1_Stream1
#define SERIAL_7_DMA_TX_DMA_CHANNEL DMA_CHANNEL_5
#define SERIAL_7_DMA_TX_IRQn DMA1_Stream1_IRQn
#define SERIAL_7_DMA_TX_IRQ_Handler DMA1_Stream1_IRQHandler
#endif
#ifdef USE_SERIAL7_DMA_RX
#define SERIAL_7_RX_MULTIBUF_SIZE (256U) /* 多缓冲区接收长度 */
#define USE_SERIAL7_CONTAINER1           /* 使用多缓冲区接收 */
#define SERIAL_7_DMA_RX_DMA_STREAM DMA1_Stream3
#define SERIAL_7_DMA_RX_DMA_CHANNEL DMA_CHANNEL_5
#define SERIAL_7_DMA_RX_IRQn DMA1_Stream3_IRQn
#define SERIAL_7_DMA_RX_IRQ_Handler DMA1_Stream3_IRQHandler
#endif
#else
#define SERIAL_7_REG_PARAMS (SERIAL_REG_INT_RX | SERIAL_REG_INT_TX) /* 注册参数 */
#endif
#endif

#define USE_SERIAL_8
#ifdef USE_SERIAL_8
#define SERIAL_8_REG_PARAMS (SERIAL_REG_DMA_RX | SERIAL_REG_DMA_TX) /* 注册参数 */
#define USE_SERIAL8_DMA_TX                                          /* 使用DMA发送 */
#define USE_SERIAL8_DMA_RX                                          /* 使用DMA接收 */
#ifdef USE_SERIAL8_DMA_TX
#define SERIAL_8_DMA_TX_DMA_STREAM DMA1_Stream0
#define SERIAL_8_DMA_TX_DMA_CHANNEL DMA_CHANNEL_5
#define SERIAL_8_DMA_TX_IRQn DMA1_Stream0_IRQn
#define SERIAL_8_DMA_TX_IRQ_Handler DMA1_Stream0_IRQHandler
#endif
#ifdef USE_SERIAL8_DMA_RX
#define SERIAL_8_RX_MULTIBUF_SIZE (256U) /* 多缓冲区接收长度 */
#define USE_SERIAL8_CONTAINER1           /* 使用多缓冲区接收 */
#define SERIAL_8_DMA_RX_DMA_STREAM DMA1_Stream6
#define SERIAL_8_DMA_RX_DMA_CHANNEL DMA_CHANNEL_5
#define SERIAL_8_DMA_RX_IRQn DMA1_Stream6_IRQn
#define SERIAL_8_DMA_RX_IRQ_Handler DMA1_Stream6_IRQHandler
#endif
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

/* 实例数：必须等于 bsp_serial_data.c 中 g_bsp_serial 条目数（数据文件内编译期校验） */
#define BSP_SERIAL_COUNT (4U)

/* 日志口：串口实例名称（device_find 用；实例配置在 bsp_serial_data.c 表内） */
#ifndef BSP_LOG_SERIAL_NAME
#define BSP_LOG_SERIAL_NAME "usart6"
#endif

/* 日志口波特率（语义键——用户意图值；boardcfg/CLI 覆写本键即生效，见 ADR-0017） */
#ifndef BSP_LOG_BAUD
#define BSP_LOG_BAUD 115200u
#endif

/* 日志口实例波特率（usart6 即日志口：实例宏 = 语义键——板内映射；适配器消费点读本宏） */
#ifndef BSP_SERIAL6_BAUD
#define BSP_SERIAL6_BAUD BSP_LOG_BAUD
#endif

/* 日志口 TX FIFO 容量（构建期可覆盖；消息 > FIFO 时尾部段丢失 → 半行——
 * 保证完整性的前提是 FIFO ≥ 最大消息长度，测试见 feature/log-serial-backend） */
#ifndef BSP_SERIAL6_TXBUFSZ
#define BSP_SERIAL6_TXBUFSZ 1024
#endif

#include "serial/bsp_serial_f4.h"

#endif

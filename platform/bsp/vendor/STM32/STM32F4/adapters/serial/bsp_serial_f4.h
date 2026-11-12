/**
 * @file  bsp_serial_f4.h
 * @brief STM32F4 家族 Serial BSP 共享适配层：类型 + 板数据契约
 * @details 板瘦身（见 ADR-0011）：F4 家族一份共享实现（impl/init/it），各板只提供
 *          实例表（g_bsp_serial[]）与板配置宏（USE_SERIAL_x、DMA profile 等留板 shim）。
 *
 *          ── 板数据契约（板 opt-in 本适配层后必须提供）────────────────────
 *          板侧 include/bsp_serial.h（shim）：
 *            USE_SERIAL_x / USE_SERIALx_DMA_* / SERIAL_x_REG_PARAMS /
 *            SERIAL_x_DMA_{TX,RX}_DMA_STREAM/CHANNEL/IRQn / SERIAL_x_RX_MULTIBUF_SIZE
 *            #define BSP_SERIAL_COUNT   实例数（= g_bsp_serial 条目数）
 *            #include "serial/bsp_serial_f4.h"
 *          板侧 source/peripherals/serial/bsp_serial_data.c：
 *            bsp_serial_s g_bsp_serial[BSP_SERIAL_COUNT]   实例表（USE_SERIAL_x 守卫）
 *          适配层文件由板 lua 显式引用（opt-in 铁律，永不进 vendor/chip sources）：
 *            selfreg_sources  += .../adapters/serial/bsp_serial_f4.c
 *            override_sources += .../adapters/serial/bsp_serial_f4_it.c
 *          注：bsp_serial_f4_init.c（pre_init 逐实例块，USE_SERIAL_x 守卫）随 impl 一起
 *          进 selfreg_sources（无 OM_INIT，但被 impl 引用，需与 impl 同编译单元链路）。
 */

#ifndef __BSP_SERIAL_F4_H__
#define __BSP_SERIAL_F4_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "drivers/peripheral/serial/pal_serial_dev.h"
#include "stm32f4xx_hal.h"

/*===========================================================================
 * 类型（原板级头文件的共享部分）
 *===========================================================================*/

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

/*===========================================================================
 * 板数据契约声明（不完整维度 extern）
 *===========================================================================*/

extern bsp_serial_s g_bsp_serial[BSP_SERIAL_COUNT]; /* BSP_SERIAL_COUNT 由板 shim 定义 */

void bsp_serial_pre_init(bsp_serial_t bsp_serial);
void bsp_serial_register(void);
void bsp_serial_dma_cfg(bsp_serial_t bsp_serial, uint32_t dma_regparams);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SERIAL_F4_H__ */

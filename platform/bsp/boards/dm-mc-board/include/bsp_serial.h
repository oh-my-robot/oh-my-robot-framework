#ifndef __DM_MC_BOARD_SERIAL_H__
#define __DM_MC_BOARD_SERIAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "drivers/peripheral/serial/pal_serial_dev.h"
#include "stm32h7xx_hal.h"

#define USE_SERIAL_1
#define USE_SERIAL_5
#define USE_SERIAL_7
#define USE_SERIAL_8
#define USE_SERIAL_9
#define USE_SERIAL_10

#ifdef USE_SERIAL_1
#define SERIAL_1_REG_PARAMS (SERIAL_REG_INT_RX | SERIAL_REG_POLL_TX)
#endif

#ifdef USE_SERIAL_5
#define SERIAL_5_REG_PARAMS (SERIAL_REG_INT_RX | SERIAL_REG_POLL_TX)
#endif

#ifdef USE_SERIAL_7
#define SERIAL_7_REG_PARAMS (SERIAL_REG_INT_RX | SERIAL_REG_POLL_TX)
#endif

#ifdef USE_SERIAL_8
#define SERIAL_8_REG_PARAMS (SERIAL_REG_INT_RX | SERIAL_REG_POLL_TX)
#endif

#ifdef USE_SERIAL_9
#define SERIAL_9_REG_PARAMS (SERIAL_REG_INT_RX | SERIAL_REG_POLL_TX)
#endif

#ifdef USE_SERIAL_10
#define SERIAL_10_REG_PARAMS (SERIAL_REG_INT_RX | SERIAL_REG_POLL_TX)
#endif

typedef enum
{
#ifdef USE_SERIAL_1
    SERIAL1_IDX,
#endif
#ifdef USE_SERIAL_5
    SERIAL5_IDX,
#endif
#ifdef USE_SERIAL_7
    SERIAL7_IDX,
#endif
#ifdef USE_SERIAL_8
    SERIAL8_IDX,
#endif
#ifdef USE_SERIAL_9
    SERIAL9_IDX,
#endif
#ifdef USE_SERIAL_10
    SERIAL10_IDX,
#endif
    SERIAL_IDX_MAX,
} SerialIdx_e;

typedef struct bsp_serial_mutibuf* bsp_serial_mutibuf_t;
typedef struct bsp_serial_mutibuf
{
    uint8_t* container0;
    uint8_t* container1;
    size_t container_len;
    size_t last_rx_cnt;
} bsp_serial_mutibuf_s;

typedef struct
{
    uint32_t irq_entry_count;
    uint32_t irq_rxne_branch_count;
    uint32_t irq_hw_isr_ok_count;
    uint32_t irq_hw_isr_err_count;
    uint32_t irq_error_cb_count;
} bsp_serial_irq_diag_t;

typedef struct bsp_serial* bsp_serial_t;
typedef struct bsp_serial
{
    UART_HandleTypeDef handle;
    HalSerial parent;
    char* name;
    uint32_t regparams;
    bsp_serial_mutibuf_t rx_multibuf;
    bsp_serial_irq_diag_t irq_diag;
} bsp_serial_s;

extern bsp_serial_s g_bsp_serial[];

#define BSP_SERIAL_STATIC_INIT(INSTANCE, NAME, REGPARAMS)                                      \
    (bsp_serial_s)                                                                              \
    {                                                                                           \
        .handle.Instance = (INSTANCE), .name = (NAME), .regparams = (REGPARAMS), .rx_multibuf = NULL, \
    }

void bsp_serial_register(void);
void bsp_serial_pre_init(bsp_serial_t bsp_serial);
void bsp_serial_dma_cfg(bsp_serial_t bsp_serial, uint32_t dma_regparams);
void bsp_serial_get_irq_diag(bsp_serial_t bsp_serial, bsp_serial_irq_diag_t* diag);
void bsp_serial_reset_irq_diag(bsp_serial_t bsp_serial);

#ifdef __cplusplus
}
#endif

#endif

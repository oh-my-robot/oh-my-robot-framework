#ifndef __DM_MC_BOARD_CAN_H__
#define __DM_MC_BOARD_CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "core/om_def.h"
#include "stm32h7xx_hal.h"

typedef enum
{
    BSP_CAN_ID_STANDARD = 0U,
    BSP_CAN_ID_EXTENDED = 1U,
} bsp_can_id_type_t;

typedef enum
{
    BSP_CAN_FRAME_CLASSIC = 0U,
    BSP_CAN_FRAME_FD = 1U,
} bsp_can_frame_type_t;

typedef enum
{
    BSP_CAN_MODE_NORMAL = 0U,
    BSP_CAN_MODE_INTERNAL_LOOPBACK = 1U,
} bsp_can_mode_t;

typedef struct
{
    uint32_t id;
    uint8_t id_type;
    uint8_t frame_type;
    uint8_t brs;
    uint8_t len;
    uint8_t data[64];
} bsp_can_frame_t;

typedef struct
{
    uint8_t online;
    uint8_t seen;
    uint32_t tx_count;
    uint32_t tx_fail_count;
    uint32_t rx_count;
    uint32_t rx_overflow_count;
    uint32_t last_id;
    uint8_t last_len;
    uint8_t last_lec;
    uint32_t error_flags;
} bsp_can_stats_t;

typedef struct
{
    uint8_t activity;
    uint8_t data_last_error_code;
    uint8_t rx_esi_seen;
    uint8_t rx_brs_seen;
    uint8_t rx_fdf_seen;
    uint8_t protocol_exception_seen;
    uint8_t tx_error_cnt;
    uint8_t rx_error_cnt;
    uint8_t rx_error_passive;
    uint8_t error_logging;
} bsp_can_debug_state_t;

typedef struct
{
    uint8_t bus;
    uint8_t frame_type;
    uint8_t brs;
    uint8_t mode;
    uint32_t nominal_bitrate_kbps;
    uint32_t data_bitrate_kbps;
    uint8_t max_data_len;
} bsp_can_init_cfg_t;

#define BSP_CAN_BUS1 (1U)
#define BSP_CAN_BUS2 (2U)
#define BSP_CAN_BUS3 (3U)

#define BSP_CAN_ERR_INIT_FAILED         (1UL << 0)
#define BSP_CAN_ERR_TX_FIFO_FULL        (1UL << 1)
#define BSP_CAN_ERR_BUS_OFF             (1UL << 2)
#define BSP_CAN_ERR_ERROR_PASSIVE       (1UL << 3)
#define BSP_CAN_ERR_ERROR_WARNING       (1UL << 4)
#define BSP_CAN_ERR_RX_OVERRUN          (1UL << 5)
#define BSP_CAN_ERR_RAM_ACCESS_FAILURE  (1UL << 6)
#define BSP_CAN_ERR_PROTOCOL_OR_ACK     (1UL << 7)

void bsp_can_register(void);
OmRet bsp_can_init(uint8_t bus);
OmRet bsp_can_init_ex(const bsp_can_init_cfg_t* cfg);
OmRet bsp_can_send_frame(uint8_t bus, const bsp_can_frame_t* frame);
OmRet bsp_can_try_recv_frame(uint8_t bus, bsp_can_frame_t* frame);
OmRet bsp_can_send(uint8_t bus, uint32_t id, const uint8_t* data, uint8_t len);
OmRet bsp_can_get_stats(uint8_t bus, bsp_can_stats_t* stats);
OmRet bsp_can_get_debug_state(uint8_t bus, bsp_can_debug_state_t* debug_state);
uint32_t bsp_can_get_kernel_clock_hz(void);

#ifdef __cplusplus
}
#endif

#endif

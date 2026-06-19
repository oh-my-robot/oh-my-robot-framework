#include "bsp_can.h"

#include "bsp_pinmap.h"
#include "stm32h7xx_hal_gpio_ex.h"

#include <string.h>

#define DM_MC02_FDCAN_MESSAGE_RAM_WORDS_TOTAL        2560U
#define DM_MC02_FDCAN_CLASSIC_STD_FILTER_WORDS       1U
#define DM_MC02_FDCAN_CLASSIC_EXT_FILTER_WORDS       0U
#define DM_MC02_FDCAN_CLASSIC_RX_FIFO0_ELMTS         32U
#define DM_MC02_FDCAN_CLASSIC_TX_FIFO_ELMTS          32U
#define DM_MC02_FDCAN_CLASSIC_DATA_WORDS             4U
#define DM_MC02_FDCAN_CLASSIC_TOTAL_WORDS            (DM_MC02_FDCAN_CLASSIC_STD_FILTER_WORDS + \
                                                      DM_MC02_FDCAN_CLASSIC_EXT_FILTER_WORDS + \
                                                      (DM_MC02_FDCAN_CLASSIC_RX_FIFO0_ELMTS * DM_MC02_FDCAN_CLASSIC_DATA_WORDS) + \
                                                      (DM_MC02_FDCAN_CLASSIC_TX_FIFO_ELMTS * DM_MC02_FDCAN_CLASSIC_DATA_WORDS))
#define DM_MC02_FDCAN_CAN1_OFFSET_WORDS              0U
#define DM_MC02_FDCAN_CAN2_OFFSET_WORDS              (DM_MC02_FDCAN_CAN1_OFFSET_WORDS + DM_MC02_FDCAN_CLASSIC_TOTAL_WORDS)
#define DM_MC02_FDCAN_CAN3_OFFSET_WORDS              (DM_MC02_FDCAN_CAN2_OFFSET_WORDS + DM_MC02_FDCAN_CLASSIC_TOTAL_WORDS)
#define DM_MC02_FDCAN_FD_RX_FIFO0_ELMTS              8U
#define DM_MC02_FDCAN_FD_TX_FIFO_ELMTS               8U
#define DM_MC02_FDCAN_FD_DATA_WORDS                  18U
#define DM_MC02_FDCAN_FD_TOTAL_WORDS                 (DM_MC02_FDCAN_CLASSIC_STD_FILTER_WORDS + \
                                                      (DM_MC02_FDCAN_FD_RX_FIFO0_ELMTS * DM_MC02_FDCAN_FD_DATA_WORDS) + \
                                                      (DM_MC02_FDCAN_FD_TX_FIFO_ELMTS * DM_MC02_FDCAN_FD_DATA_WORDS))
#define DM_MC02_FDCAN_FD_CAN1_OFFSET_WORDS           0U
#define DM_MC02_FDCAN_FD_CAN2_OFFSET_WORDS           (DM_MC02_FDCAN_FD_CAN1_OFFSET_WORDS + DM_MC02_FDCAN_FD_TOTAL_WORDS)
#define DM_MC02_FDCAN_FD_CAN3_OFFSET_WORDS           (DM_MC02_FDCAN_FD_CAN2_OFFSET_WORDS + DM_MC02_FDCAN_FD_TOTAL_WORDS)
#define DM_MC02_FDCAN_SUPPORTED_KERNEL_CLOCK_HZ      120000000U
#define DM_MC02_CAN_RX_QUEUE_SIZE                    32U
#define DM_MC02_CAN_NOTIFY_MASK                      (FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST | \
                                                      FDCAN_IT_RX_FIFO0_FULL | FDCAN_IT_RAM_ACCESS_FAILURE | \
                                                      FDCAN_IT_ARB_PROTOCOL_ERROR | FDCAN_IT_DATA_PROTOCOL_ERROR | \
                                                      FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_ERROR_WARNING | FDCAN_IT_BUS_OFF)

#if (DM_MC02_FDCAN_CAN3_OFFSET_WORDS + DM_MC02_FDCAN_CLASSIC_TOTAL_WORDS) > DM_MC02_FDCAN_MESSAGE_RAM_WORDS_TOTAL
#error "DM-MC02 classic CAN message RAM exceeds H7 FDCAN RAM."
#endif

#if (DM_MC02_FDCAN_FD_CAN3_OFFSET_WORDS + DM_MC02_FDCAN_FD_TOTAL_WORDS) > DM_MC02_FDCAN_MESSAGE_RAM_WORDS_TOTAL
#error "DM-MC02 CAN FD message RAM exceeds H7 FDCAN RAM."
#endif

typedef struct
{
    uint8_t bus;
    FDCAN_GlobalTypeDef* instance;
    uint32_t message_ram_offset_words;
    GPIO_TypeDef* gpio_port;
    uint16_t rx_pin;
    uint16_t tx_pin;
    uint8_t gpio_af;
    IRQn_Type it0_irqn;
    IRQn_Type it1_irqn;
} dm_mc02_can_hw_cfg_t;

typedef struct
{
    uint8_t initialized;
    uint8_t frame_type;
    uint8_t brs;
    uint8_t mode;
    uint32_t nominal_bitrate_kbps;
    uint32_t data_bitrate_kbps;
    uint8_t max_data_len;
    FDCAN_HandleTypeDef handle;
    bsp_can_stats_t stats;
    bsp_can_debug_state_t debug_state;
    bsp_can_frame_t rx_queue[DM_MC02_CAN_RX_QUEUE_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    volatile uint16_t rx_count;
} dm_mc02_can_ctx_t;

static const dm_mc02_can_hw_cfg_t g_dm_mc02_can_hw_cfgs[] = {
    {
        .bus = BSP_CAN_BUS1,
        .instance = FDCAN1,
        .message_ram_offset_words = DM_MC02_FDCAN_CAN1_OFFSET_WORDS,
        .gpio_port = GPIOD,
        .rx_pin = DM_MC_BOARD_CAN1_RX_PIN,
        .tx_pin = DM_MC_BOARD_CAN1_TX_PIN,
        .gpio_af = GPIO_AF9_FDCAN1,
        .it0_irqn = FDCAN1_IT0_IRQn,
        .it1_irqn = FDCAN1_IT1_IRQn,
    },
    {
        .bus = BSP_CAN_BUS2,
        .instance = FDCAN2,
        .message_ram_offset_words = DM_MC02_FDCAN_CAN2_OFFSET_WORDS,
        .gpio_port = GPIOB,
        .rx_pin = DM_MC_BOARD_CAN2_RX_PIN,
        .tx_pin = DM_MC_BOARD_CAN2_TX_PIN,
        .gpio_af = GPIO_AF9_FDCAN2,
        .it0_irqn = FDCAN2_IT0_IRQn,
        .it1_irqn = FDCAN2_IT1_IRQn,
    },
    {
        .bus = BSP_CAN_BUS3,
        .instance = FDCAN3,
        .message_ram_offset_words = DM_MC02_FDCAN_CAN3_OFFSET_WORDS,
        .gpio_port = GPIOD,
        .rx_pin = DM_MC_BOARD_CAN3_RX_PIN,
        .tx_pin = DM_MC_BOARD_CAN3_TX_PIN,
        .gpio_af = GPIO_AF5_FDCAN3,
        .it0_irqn = FDCAN3_IT0_IRQn,
        .it1_irqn = FDCAN3_IT1_IRQn,
    },
};

static dm_mc02_can_ctx_t g_dm_mc02_can1_ctx = {
    .handle.Instance = FDCAN1,
};

static dm_mc02_can_ctx_t g_dm_mc02_can2_ctx = {
    .handle.Instance = FDCAN2,
};

static dm_mc02_can_ctx_t g_dm_mc02_can3_ctx = {
    .handle.Instance = FDCAN3,
};

static const dm_mc02_can_hw_cfg_t* bsp_can_hw_cfg_from_bus(uint8_t bus)
{
    uint32_t i;

    for (i = 0U; i < (sizeof(g_dm_mc02_can_hw_cfgs) / sizeof(g_dm_mc02_can_hw_cfgs[0])); i++)
    {
        if (g_dm_mc02_can_hw_cfgs[i].bus == bus)
        {
            return &g_dm_mc02_can_hw_cfgs[i];
        }
    }

    return NULL;
}

static const dm_mc02_can_hw_cfg_t* bsp_can_hw_cfg_from_instance(FDCAN_GlobalTypeDef* instance)
{
    uint32_t i;

    for (i = 0U; i < (sizeof(g_dm_mc02_can_hw_cfgs) / sizeof(g_dm_mc02_can_hw_cfgs[0])); i++)
    {
        if (g_dm_mc02_can_hw_cfgs[i].instance == instance)
        {
            return &g_dm_mc02_can_hw_cfgs[i];
        }
    }

    return NULL;
}

static dm_mc02_can_ctx_t* bsp_can_ctx_from_bus(uint8_t bus)
{
    if (bus == BSP_CAN_BUS1)
    {
        return &g_dm_mc02_can1_ctx;
    }
    if (bus == BSP_CAN_BUS2)
    {
        return &g_dm_mc02_can2_ctx;
    }
    if (bus == BSP_CAN_BUS3)
    {
        return &g_dm_mc02_can3_ctx;
    }

    return NULL;
}

static dm_mc02_can_ctx_t* bsp_can_ctx_from_instance(FDCAN_GlobalTypeDef* instance)
{
    if (instance == FDCAN1)
    {
        return &g_dm_mc02_can1_ctx;
    }
    if (instance == FDCAN2)
    {
        return &g_dm_mc02_can2_ctx;
    }
    if (instance == FDCAN3)
    {
        return &g_dm_mc02_can3_ctx;
    }

    return NULL;
}

static uint32_t bsp_can_message_ram_offset_words(uint8_t bus, uint8_t frame_type)
{
    if (frame_type == BSP_CAN_FRAME_FD)
    {
        if (bus == BSP_CAN_BUS1)
        {
            return DM_MC02_FDCAN_FD_CAN1_OFFSET_WORDS;
        }
        if (bus == BSP_CAN_BUS2)
        {
            return DM_MC02_FDCAN_FD_CAN2_OFFSET_WORDS;
        }
        if (bus == BSP_CAN_BUS3)
        {
            return DM_MC02_FDCAN_FD_CAN3_OFFSET_WORDS;
        }
        return 0U;
    }

    if (bus == BSP_CAN_BUS1)
    {
        return DM_MC02_FDCAN_CAN1_OFFSET_WORDS;
    }
    if (bus == BSP_CAN_BUS2)
    {
        return DM_MC02_FDCAN_CAN2_OFFSET_WORDS;
    }
    if (bus == BSP_CAN_BUS3)
    {
        return DM_MC02_FDCAN_CAN3_OFFSET_WORDS;
    }

    return 0U;
}

static void bsp_can_reset_ctx(dm_mc02_can_ctx_t* ctx, FDCAN_GlobalTypeDef* instance)
{
    if (ctx == NULL)
    {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->handle.Instance = instance;
}

static void bsp_can_clear_runtime_state(dm_mc02_can_ctx_t* ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    memset(&ctx->stats, 0, sizeof(ctx->stats));
    memset(&ctx->debug_state, 0, sizeof(ctx->debug_state));
    memset(ctx->rx_queue, 0, sizeof(ctx->rx_queue));
    ctx->rx_head = 0U;
    ctx->rx_tail = 0U;
    ctx->rx_count = 0U;
}

uint32_t bsp_can_get_kernel_clock_hz(void)
{
    return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN);
}

static OmRet bsp_can_apply_nominal_timing(
    FDCAN_HandleTypeDef* handle,
    uint32_t prescaler,
    uint32_t sync_jump_width,
    uint32_t time_seg1,
    uint32_t time_seg2)
{
    if (handle == NULL)
    {
        return OM_ERROR_PARAM;
    }

    handle->Init.NominalPrescaler = prescaler;
    handle->Init.NominalSyncJumpWidth = sync_jump_width;
    handle->Init.NominalTimeSeg1 = time_seg1;
    handle->Init.NominalTimeSeg2 = time_seg2;
    return OM_OK;
}

static OmRet bsp_can_apply_data_timing(
    FDCAN_HandleTypeDef* handle,
    uint32_t prescaler,
    uint32_t sync_jump_width,
    uint32_t time_seg1,
    uint32_t time_seg2)
{
    if (handle == NULL)
    {
        return OM_ERROR_PARAM;
    }

    handle->Init.DataPrescaler = prescaler;
    handle->Init.DataSyncJumpWidth = sync_jump_width;
    handle->Init.DataTimeSeg1 = time_seg1;
    handle->Init.DataTimeSeg2 = time_seg2;
    return OM_OK;
}

static OmRet bsp_can_apply_bitrate_timing(FDCAN_HandleTypeDef* handle, const bsp_can_init_cfg_t* cfg, uint32_t kernel_clock_hz)
{
    if (handle == NULL || cfg == NULL)
    {
        return OM_ERROR_PARAM;
    }

    if (kernel_clock_hz != DM_MC02_FDCAN_SUPPORTED_KERNEL_CLOCK_HZ)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    if (cfg->nominal_bitrate_kbps != 1000U)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    if (bsp_can_apply_nominal_timing(handle, 10U, 1U, 9U, 2U) != OM_OK)
    {
        return OM_ERROR;
    }

    if (cfg->data_bitrate_kbps == 1000U)
    {
        if (bsp_can_apply_data_timing(handle, 10U, 1U, 9U, 2U) != OM_OK)
        {
            return OM_ERROR;
        }
        return OM_OK;
    }

    if (cfg->data_bitrate_kbps == 5000U)
    {
        if (bsp_can_apply_data_timing(handle, 2U, 1U, 9U, 2U) != OM_OK)
        {
            return OM_ERROR;
        }
        return OM_OK;
    }

    if (cfg->data_bitrate_kbps == 2000U)
    {
        if (bsp_can_apply_data_timing(handle, 5U, 1U, 9U, 2U) != OM_OK)
        {
            return OM_ERROR;
        }
        return OM_OK;
    }

    return OM_ERROR_NOT_SUPPORT;
}

static uint8_t bsp_can_is_supported_data_bitrate(uint32_t bitrate_kbps)
{
    return (uint8_t)(bitrate_kbps == 1000U || bitrate_kbps == 2000U || bitrate_kbps == 5000U);
}

static uint8_t bsp_can_is_supported_kernel_clock(uint32_t kernel_clock_hz)
{
    return (uint8_t)(kernel_clock_hz == DM_MC02_FDCAN_SUPPORTED_KERNEL_CLOCK_HZ);
}

static uint32_t bsp_can_frame_format_from_cfg(const bsp_can_init_cfg_t* cfg)
{
    if (cfg == NULL)
    {
        return FDCAN_FRAME_CLASSIC;
    }

    if (cfg->frame_type == BSP_CAN_FRAME_FD)
    {
        return (cfg->brs != 0U) ? FDCAN_FRAME_FD_BRS : FDCAN_FRAME_FD_NO_BRS;
    }

    return FDCAN_FRAME_CLASSIC;
}

static uint8_t bsp_can_ctx_cfg_matches(const dm_mc02_can_ctx_t* ctx, const bsp_can_init_cfg_t* cfg)
{
    if (ctx == NULL || cfg == NULL)
    {
        return 0U;
    }

    return (uint8_t)(
        ctx->frame_type == cfg->frame_type &&
        ctx->brs == cfg->brs &&
        ctx->mode == cfg->mode &&
        ctx->nominal_bitrate_kbps == cfg->nominal_bitrate_kbps &&
        ctx->data_bitrate_kbps == cfg->data_bitrate_kbps &&
        ctx->max_data_len == cfg->max_data_len
    );
}

static uint32_t bsp_can_dlc_from_len(uint8_t len)
{
    switch (len)
    {
    case 0U:
        return FDCAN_DLC_BYTES_0;
    case 1U:
        return FDCAN_DLC_BYTES_1;
    case 2U:
        return FDCAN_DLC_BYTES_2;
    case 3U:
        return FDCAN_DLC_BYTES_3;
    case 4U:
        return FDCAN_DLC_BYTES_4;
    case 5U:
        return FDCAN_DLC_BYTES_5;
    case 6U:
        return FDCAN_DLC_BYTES_6;
    case 7U:
        return FDCAN_DLC_BYTES_7;
    case 8U:
        return FDCAN_DLC_BYTES_8;
    case 12U:
        return FDCAN_DLC_BYTES_12;
    case 16U:
        return FDCAN_DLC_BYTES_16;
    case 20U:
        return FDCAN_DLC_BYTES_20;
    case 24U:
        return FDCAN_DLC_BYTES_24;
    case 32U:
        return FDCAN_DLC_BYTES_32;
    case 48U:
        return FDCAN_DLC_BYTES_48;
    case 64U:
        return FDCAN_DLC_BYTES_64;
    default:
        return 0xFFFFFFFFUL;
    }
}

static uint8_t bsp_can_len_from_dlc(uint32_t dlc)
{
    switch (dlc)
    {
    case FDCAN_DLC_BYTES_0:
        return 0U;
    case FDCAN_DLC_BYTES_1:
        return 1U;
    case FDCAN_DLC_BYTES_2:
        return 2U;
    case FDCAN_DLC_BYTES_3:
        return 3U;
    case FDCAN_DLC_BYTES_4:
        return 4U;
    case FDCAN_DLC_BYTES_5:
        return 5U;
    case FDCAN_DLC_BYTES_6:
        return 6U;
    case FDCAN_DLC_BYTES_7:
        return 7U;
    case FDCAN_DLC_BYTES_8:
        return 8U;
    case FDCAN_DLC_BYTES_12:
        return 12U;
    case FDCAN_DLC_BYTES_16:
        return 16U;
    case FDCAN_DLC_BYTES_20:
        return 20U;
    case FDCAN_DLC_BYTES_24:
        return 24U;
    case FDCAN_DLC_BYTES_32:
        return 32U;
    case FDCAN_DLC_BYTES_48:
        return 48U;
    case FDCAN_DLC_BYTES_64:
        return 64U;
    default:
        return 0U;
    }
}

static uint8_t bsp_can_is_valid_frame_len(uint8_t frame_type, uint8_t len)
{
    if (frame_type == BSP_CAN_FRAME_CLASSIC)
    {
        return (uint8_t)(len <= 8U);
    }

    return (uint8_t)(bsp_can_dlc_from_len(len) != 0xFFFFFFFFUL);
}

static OmRet bsp_can_validate_init_cfg(const bsp_can_init_cfg_t* cfg)
{
    if (cfg == NULL)
    {
        return OM_ERROR_PARAM;
    }

    if (cfg->bus != BSP_CAN_BUS1 && cfg->bus != BSP_CAN_BUS2 && cfg->bus != BSP_CAN_BUS3)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    if (cfg->mode != BSP_CAN_MODE_NORMAL && cfg->mode != BSP_CAN_MODE_INTERNAL_LOOPBACK)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    if (cfg->frame_type == BSP_CAN_FRAME_CLASSIC)
    {
        if (cfg->brs != 0U)
        {
            return OM_ERROR_NOT_SUPPORT;
        }
        if (cfg->nominal_bitrate_kbps != 1000U || cfg->data_bitrate_kbps != 1000U)
        {
            return OM_ERROR_NOT_SUPPORT;
        }
        if (cfg->max_data_len != 8U)
        {
            return OM_ERROR_NOT_SUPPORT;
        }
    }
    else if (cfg->frame_type == BSP_CAN_FRAME_FD)
    {
        if (cfg->nominal_bitrate_kbps != 1000U)
        {
            return OM_ERROR_NOT_SUPPORT;
        }
        if (cfg->max_data_len != 64U)
        {
            return OM_ERROR_NOT_SUPPORT;
        }
        if (cfg->brs == 0U)
        {
            if (cfg->data_bitrate_kbps != 1000U)
            {
                return OM_ERROR_NOT_SUPPORT;
            }
        }
        else if (cfg->brs == 1U)
        {
            if (bsp_can_is_supported_data_bitrate(cfg->data_bitrate_kbps) == 0U ||
                cfg->data_bitrate_kbps == 1000U)
            {
                return OM_ERROR_NOT_SUPPORT;
            }
        }
        else
        {
            return OM_ERROR_NOT_SUPPORT;
        }
    }
    else
    {
        return OM_ERROR_PARAM;
    }

    return OM_OK;
}

static OmRet bsp_can_validate_frame_id(const bsp_can_frame_t* frame)
{
    if (frame == NULL)
    {
        return OM_ERROR_PARAM;
    }

    if (frame->id_type == BSP_CAN_ID_STANDARD)
    {
        return (frame->id <= 0x7FFU) ? OM_OK : OM_ERROR_PARAM;
    }

    if (frame->id_type == BSP_CAN_ID_EXTENDED)
    {
        return (frame->id <= 0x1FFFFFFFU) ? OM_OK : OM_ERROR_PARAM;
    }

    return OM_ERROR_PARAM;
}

static void bsp_can_protocol_status_refresh(dm_mc02_can_ctx_t* ctx)
{
    FDCAN_ProtocolStatusTypeDef protocol = {0};
    FDCAN_ErrorCountersTypeDef counters = {0};

    if (ctx == NULL)
    {
        return;
    }

    if (HAL_FDCAN_GetProtocolStatus(&ctx->handle, &protocol) != HAL_OK)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_RAM_ACCESS_FAILURE;
        return;
    }

    ctx->debug_state.activity = (uint8_t)protocol.Activity;
    ctx->debug_state.data_last_error_code = (uint8_t)protocol.DataLastErrorCode;
    if (protocol.RxESIflag != 0U)
    {
        ctx->debug_state.rx_esi_seen = 1U;
    }
    if (protocol.RxBRSflag != 0U)
    {
        ctx->debug_state.rx_brs_seen = 1U;
    }
    if (protocol.RxFDFflag != 0U)
    {
        ctx->debug_state.rx_fdf_seen = 1U;
    }
    if (protocol.ProtocolException != 0U)
    {
        ctx->debug_state.protocol_exception_seen = 1U;
    }

    ctx->stats.error_flags &= ~(BSP_CAN_ERR_BUS_OFF | BSP_CAN_ERR_ERROR_PASSIVE | BSP_CAN_ERR_ERROR_WARNING);
    if (protocol.BusOff != 0U)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_BUS_OFF;
    }
    if (protocol.ErrorPassive != 0U)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_ERROR_PASSIVE;
    }
    if (protocol.Warning != 0U)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_ERROR_WARNING;
    }

    if (protocol.LastErrorCode != FDCAN_PROTOCOL_ERROR_NONE &&
        protocol.LastErrorCode != FDCAN_PROTOCOL_ERROR_NO_CHANGE)
    {
        ctx->stats.last_lec = (uint8_t)protocol.LastErrorCode;
        ctx->stats.error_flags |= BSP_CAN_ERR_PROTOCOL_OR_ACK;
    }

    if (HAL_FDCAN_GetErrorCounters(&ctx->handle, &counters) != HAL_OK)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_RAM_ACCESS_FAILURE;
        return;
    }

    ctx->debug_state.tx_error_cnt = (uint8_t)counters.TxErrorCnt;
    ctx->debug_state.rx_error_cnt = (uint8_t)counters.RxErrorCnt;
    ctx->debug_state.rx_error_passive = (uint8_t)counters.RxErrorPassive;
    ctx->debug_state.error_logging = (uint8_t)counters.ErrorLogging;
}

static void bsp_can_rx_push(dm_mc02_can_ctx_t* ctx, const bsp_can_frame_t* frame)
{
    uint16_t next_tail;

    if (ctx == NULL || frame == NULL)
    {
        return;
    }

    if (ctx->rx_count >= DM_MC02_CAN_RX_QUEUE_SIZE)
    {
        ctx->stats.rx_overflow_count++;
        ctx->stats.error_flags |= BSP_CAN_ERR_RX_OVERRUN;
        return;
    }

    next_tail = ctx->rx_tail;
    ctx->rx_queue[next_tail] = *frame;
    ctx->rx_tail = (uint16_t)((next_tail + 1U) % DM_MC02_CAN_RX_QUEUE_SIZE);
    ctx->rx_count++;
}

static void bsp_can_hw_rx_drain(dm_mc02_can_ctx_t* ctx)
{
    if (ctx == NULL || ctx->initialized == 0U)
    {
        return;
    }

    while (HAL_FDCAN_GetRxFifoFillLevel(&ctx->handle, FDCAN_RX_FIFO0) > 0U)
    {
        FDCAN_RxHeaderTypeDef header = {0};
        bsp_can_frame_t frame = {0};

        if (HAL_FDCAN_GetRxMessage(&ctx->handle, FDCAN_RX_FIFO0, &header, frame.data) != HAL_OK)
        {
            ctx->stats.error_flags |= BSP_CAN_ERR_PROTOCOL_OR_ACK;
            break;
        }

        frame.id = header.Identifier;
        frame.id_type = (uint8_t)((header.IdType == FDCAN_EXTENDED_ID) ? BSP_CAN_ID_EXTENDED : BSP_CAN_ID_STANDARD);
        frame.frame_type = (uint8_t)((header.FDFormat == FDCAN_FD_CAN) ? BSP_CAN_FRAME_FD : BSP_CAN_FRAME_CLASSIC);
        frame.brs = (uint8_t)((header.BitRateSwitch == FDCAN_BRS_ON) ? 1U : 0U);
        frame.len = bsp_can_len_from_dlc(header.DataLength);

        ctx->stats.seen = 1U;
        ctx->stats.rx_count++;
        ctx->stats.last_id = frame.id;
        ctx->stats.last_len = frame.len;

        bsp_can_rx_push(ctx, &frame);
    }

    bsp_can_protocol_status_refresh(ctx);
}

static void bsp_can_hw_rx_drain_fallback(dm_mc02_can_ctx_t* ctx)
{
    const dm_mc02_can_hw_cfg_t* hw_cfg;

    if (ctx == NULL || ctx->initialized == 0U)
    {
        return;
    }

    hw_cfg = bsp_can_hw_cfg_from_instance(ctx->handle.Instance);
    if (hw_cfg == NULL)
    {
        return;
    }

    /* Fallback path for cases where RX FIFO0 has pending frames but the
     * notification callback did not run yet. Mask the line-0 IRQ briefly
     * so software queue filling cannot race with the ISR while we drain. */
    HAL_NVIC_DisableIRQ(hw_cfg->it0_irqn);
    bsp_can_hw_rx_drain(ctx);
    HAL_NVIC_EnableIRQ(hw_cfg->it0_irqn);
}

static void bsp_can_nvic_enable(const dm_mc02_can_hw_cfg_t* hw_cfg)
{
    if (hw_cfg == NULL)
    {
        return;
    }

    HAL_NVIC_SetPriority(hw_cfg->it0_irqn, 6, 0);
    HAL_NVIC_ClearPendingIRQ(hw_cfg->it0_irqn);
    HAL_NVIC_EnableIRQ(hw_cfg->it0_irqn);

    HAL_NVIC_ClearPendingIRQ(hw_cfg->it1_irqn);
    HAL_NVIC_DisableIRQ(hw_cfg->it1_irqn);
}

void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef* hfdcan)
{
    GPIO_InitTypeDef gpio = {0};
    const dm_mc02_can_hw_cfg_t* hw_cfg;

    if (hfdcan == NULL)
    {
        return;
    }

    hw_cfg = bsp_can_hw_cfg_from_instance(hfdcan->Instance);
    if (hw_cfg == NULL)
    {
        return;
    }

    __HAL_RCC_FDCAN_CLK_ENABLE();
    if (hw_cfg->gpio_port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (hw_cfg->gpio_port == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }

    gpio.Pin = hw_cfg->rx_pin | hw_cfg->tx_pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = hw_cfg->gpio_af;
    HAL_GPIO_Init(hw_cfg->gpio_port, &gpio);
}

void HAL_FDCAN_MspDeInit(FDCAN_HandleTypeDef* hfdcan)
{
    const dm_mc02_can_hw_cfg_t* hw_cfg;

    if (hfdcan == NULL)
    {
        return;
    }

    hw_cfg = bsp_can_hw_cfg_from_instance(hfdcan->Instance);
    if (hw_cfg == NULL)
    {
        return;
    }

    HAL_NVIC_DisableIRQ(hw_cfg->it0_irqn);
    HAL_NVIC_DisableIRQ(hw_cfg->it1_irqn);
    HAL_GPIO_DeInit(hw_cfg->gpio_port, hw_cfg->rx_pin | hw_cfg->tx_pin);
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t rx_fifo0_its)
{
    dm_mc02_can_ctx_t* ctx;

    if (hfdcan == NULL)
    {
        return;
    }

    ctx = bsp_can_ctx_from_instance(hfdcan->Instance);
    if (ctx == NULL)
    {
        return;
    }

    bsp_can_hw_rx_drain(ctx);

    if ((rx_fifo0_its & (FDCAN_IT_RX_FIFO0_MESSAGE_LOST | FDCAN_IT_RX_FIFO0_FULL)) != 0U)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_RX_OVERRUN;
    }
}

void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef* hfdcan, uint32_t error_status_its)
{
    dm_mc02_can_ctx_t* ctx;

    if (hfdcan == NULL)
    {
        return;
    }

    ctx = bsp_can_ctx_from_instance(hfdcan->Instance);
    if (ctx == NULL)
    {
        return;
    }

    if ((error_status_its & FDCAN_IT_RAM_ACCESS_FAILURE) != 0U)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_RAM_ACCESS_FAILURE;
    }

    if ((error_status_its & (FDCAN_IT_ARB_PROTOCOL_ERROR | FDCAN_IT_DATA_PROTOCOL_ERROR)) != 0U)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_PROTOCOL_OR_ACK;
    }

    bsp_can_protocol_status_refresh(ctx);
}

void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef* hfdcan)
{
    dm_mc02_can_ctx_t* ctx;

    if (hfdcan == NULL)
    {
        return;
    }

    ctx = bsp_can_ctx_from_instance(hfdcan->Instance);
    if (ctx == NULL)
    {
        return;
    }

    bsp_can_protocol_status_refresh(ctx);
}

void FDCAN1_IT0_IRQHandler(void)
{
    HAL_FDCAN_IRQHandler(&g_dm_mc02_can1_ctx.handle);
}

void FDCAN1_IT1_IRQHandler(void)
{
    HAL_FDCAN_IRQHandler(&g_dm_mc02_can1_ctx.handle);
}

void FDCAN2_IT0_IRQHandler(void)
{
    HAL_FDCAN_IRQHandler(&g_dm_mc02_can2_ctx.handle);
}

void FDCAN2_IT1_IRQHandler(void)
{
    HAL_FDCAN_IRQHandler(&g_dm_mc02_can2_ctx.handle);
}

void FDCAN3_IT0_IRQHandler(void)
{
    HAL_FDCAN_IRQHandler(&g_dm_mc02_can3_ctx.handle);
}

void FDCAN3_IT1_IRQHandler(void)
{
    HAL_FDCAN_IRQHandler(&g_dm_mc02_can3_ctx.handle);
}

void bsp_can_register(void)
{
    bsp_can_reset_ctx(&g_dm_mc02_can1_ctx, FDCAN1);
    bsp_can_reset_ctx(&g_dm_mc02_can2_ctx, FDCAN2);
    bsp_can_reset_ctx(&g_dm_mc02_can3_ctx, FDCAN3);
}

OmRet bsp_can_init(uint8_t bus)
{
    bsp_can_init_cfg_t cfg = {
        .bus = bus,
        .frame_type = BSP_CAN_FRAME_CLASSIC,
        .brs = 0U,
        .mode = BSP_CAN_MODE_NORMAL,
        .nominal_bitrate_kbps = 1000U,
        .data_bitrate_kbps = 1000U,
        .max_data_len = 8U,
    };

    return bsp_can_init_ex(&cfg);
}

OmRet bsp_can_init_ex(const bsp_can_init_cfg_t* cfg)
{
    FDCAN_FilterTypeDef filter = {0};
    dm_mc02_can_ctx_t* ctx;
    const dm_mc02_can_hw_cfg_t* hw_cfg;
    uint32_t kernel_clock_hz;
    uint32_t message_ram_offset_words;

    if (bsp_can_validate_init_cfg(cfg) != OM_OK)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    ctx = bsp_can_ctx_from_bus(cfg->bus);
    hw_cfg = bsp_can_hw_cfg_from_bus(cfg->bus);

    if (ctx == NULL || hw_cfg == NULL)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    if (ctx->initialized != 0U)
    {
        return (bsp_can_ctx_cfg_matches(ctx, cfg) != 0U) ? OM_OK : OM_ERROR_BUSY;
    }

    kernel_clock_hz = bsp_can_get_kernel_clock_hz();
    if (bsp_can_is_supported_kernel_clock(kernel_clock_hz) == 0U)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    bsp_can_clear_runtime_state(ctx);
    ctx->handle.Instance = hw_cfg->instance;
    ctx->initialized = 0U;
    ctx->frame_type = cfg->frame_type;
    ctx->brs = cfg->brs;
    ctx->mode = cfg->mode;
    ctx->nominal_bitrate_kbps = cfg->nominal_bitrate_kbps;
    ctx->data_bitrate_kbps = cfg->data_bitrate_kbps;
    ctx->max_data_len = cfg->max_data_len;

    (void)HAL_FDCAN_DeInit(&ctx->handle);

    memset(&ctx->handle.Init, 0, sizeof(ctx->handle.Init));

    ctx->handle.Init.FrameFormat = bsp_can_frame_format_from_cfg(cfg);
    ctx->handle.Init.Mode = (cfg->mode == BSP_CAN_MODE_INTERNAL_LOOPBACK) ? FDCAN_MODE_INTERNAL_LOOPBACK : FDCAN_MODE_NORMAL;
    ctx->handle.Init.AutoRetransmission = ENABLE;
    ctx->handle.Init.TransmitPause = DISABLE;
    ctx->handle.Init.ProtocolException = DISABLE;
    if (bsp_can_apply_bitrate_timing(&ctx->handle, cfg, kernel_clock_hz) != OM_OK)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    message_ram_offset_words = bsp_can_message_ram_offset_words(cfg->bus, cfg->frame_type);
    ctx->handle.Init.MessageRAMOffset = message_ram_offset_words;
    ctx->handle.Init.StdFiltersNbr = 1U;
    ctx->handle.Init.ExtFiltersNbr = 0U;
    if (cfg->frame_type == BSP_CAN_FRAME_FD)
    {
        ctx->handle.Init.RxFifo0ElmtsNbr = DM_MC02_FDCAN_FD_RX_FIFO0_ELMTS;
        ctx->handle.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_64;
        ctx->handle.Init.TxFifoQueueElmtsNbr = DM_MC02_FDCAN_FD_TX_FIFO_ELMTS;
        ctx->handle.Init.TxElmtSize = FDCAN_DATA_BYTES_64;
    }
    else
    {
        ctx->handle.Init.RxFifo0ElmtsNbr = DM_MC02_FDCAN_CLASSIC_RX_FIFO0_ELMTS;
        ctx->handle.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
        ctx->handle.Init.TxFifoQueueElmtsNbr = DM_MC02_FDCAN_CLASSIC_TX_FIFO_ELMTS;
        ctx->handle.Init.TxElmtSize = FDCAN_DATA_BYTES_8;
    }
    ctx->handle.Init.RxFifo1ElmtsNbr = 0U;
    ctx->handle.Init.RxFifo1ElmtSize = FDCAN_DATA_BYTES_8;
    ctx->handle.Init.RxBuffersNbr = 0U;
    ctx->handle.Init.RxBufferSize = FDCAN_DATA_BYTES_8;
    ctx->handle.Init.TxEventsNbr = 0U;
    ctx->handle.Init.TxBuffersNbr = 0U;
    ctx->handle.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

    if (HAL_FDCAN_Init(&ctx->handle) != HAL_OK)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_INIT_FAILED;
        return OM_ERROR;
    }

    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0U;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = 0x000U;
    filter.FilterID2 = 0x000U;
    filter.RxBufferIndex = 0U;
    filter.IsCalibrationMsg = 0U;
    if (HAL_FDCAN_ConfigFilter(&ctx->handle, &filter) != HAL_OK)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_INIT_FAILED;
        return OM_ERROR;
    }

    if (HAL_FDCAN_ConfigGlobalFilter(
            &ctx->handle,
            FDCAN_ACCEPT_IN_RX_FIFO0,
            FDCAN_REJECT,
            FDCAN_REJECT_REMOTE,
            FDCAN_REJECT_REMOTE
        ) != HAL_OK)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_INIT_FAILED;
        return OM_ERROR;
    }

    if (HAL_FDCAN_ConfigInterruptLines(&ctx->handle, DM_MC02_CAN_NOTIFY_MASK, FDCAN_INTERRUPT_LINE0) != HAL_OK)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_INIT_FAILED;
        return OM_ERROR;
    }

    bsp_can_nvic_enable(hw_cfg);

    if (HAL_FDCAN_ActivateNotification(&ctx->handle, DM_MC02_CAN_NOTIFY_MASK, 0U) != HAL_OK)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_INIT_FAILED;
        return OM_ERROR;
    }

    if (HAL_FDCAN_Start(&ctx->handle) != HAL_OK)
    {
        ctx->stats.error_flags |= BSP_CAN_ERR_INIT_FAILED;
        return OM_ERROR;
    }

    ctx->initialized = 1U;
    bsp_can_protocol_status_refresh(ctx);
    return OM_OK;
}

OmRet bsp_can_send_frame(uint8_t bus, const bsp_can_frame_t* frame)
{
    FDCAN_TxHeaderTypeDef header = {0};
    dm_mc02_can_ctx_t* ctx = bsp_can_ctx_from_bus(bus);
    uint32_t dlc;

    if (ctx == NULL)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    if (frame == NULL)
    {
        return OM_ERROR_PARAM;
    }

    if (ctx->initialized == 0U)
    {
        return OM_ERROR;
    }

    if (bsp_can_validate_frame_id(frame) != OM_OK)
    {
        return OM_ERROR_PARAM;
    }

    if (frame->frame_type != ctx->frame_type)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    if (frame->brs != ctx->brs)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    if (frame->len > ctx->max_data_len || bsp_can_is_valid_frame_len(frame->frame_type, frame->len) == 0U)
    {
        return OM_ERROR_PARAM;
    }

    dlc = bsp_can_dlc_from_len(frame->len);
    if (dlc == 0xFFFFFFFFUL)
    {
        return OM_ERROR_PARAM;
    }

    if (HAL_FDCAN_GetTxFifoFreeLevel(&ctx->handle) == 0U)
    {
        ctx->stats.tx_fail_count++;
        ctx->stats.error_flags |= BSP_CAN_ERR_TX_FIFO_FULL;
        return OM_ERR_OVERFLOW;
    }

    header.Identifier = frame->id;
    header.IdType = (frame->id_type == BSP_CAN_ID_EXTENDED) ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    header.TxFrameType = FDCAN_DATA_FRAME;
    header.DataLength = dlc;
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch = (frame->brs != 0U) ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    header.FDFormat = (frame->frame_type == BSP_CAN_FRAME_FD) ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    header.MessageMarker = 0U;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&ctx->handle, &header, (uint8_t*)frame->data) != HAL_OK)
    {
        ctx->stats.tx_fail_count++;
        ctx->stats.error_flags |= BSP_CAN_ERR_PROTOCOL_OR_ACK;
        bsp_can_protocol_status_refresh(ctx);
        return OM_ERROR;
    }

    ctx->stats.tx_count++;
    bsp_can_protocol_status_refresh(ctx);
    return OM_OK;
}

OmRet bsp_can_try_recv_frame(uint8_t bus, bsp_can_frame_t* frame)
{
    dm_mc02_can_ctx_t* ctx = bsp_can_ctx_from_bus(bus);
    uint32_t primask;

    if (ctx == NULL)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    if (frame == NULL)
    {
        return OM_ERROR_PARAM;
    }

    if (ctx->initialized == 0U)
    {
        return OM_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    if (ctx->rx_count == 0U)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }

        bsp_can_hw_rx_drain_fallback(ctx);

        primask = __get_PRIMASK();
        __disable_irq();
        if (ctx->rx_count == 0U)
        {
            if (primask == 0U)
            {
                __enable_irq();
            }
            return OM_ERROR_EMPTY;
        }
    }

    *frame = ctx->rx_queue[ctx->rx_head];
    ctx->rx_head = (uint16_t)((ctx->rx_head + 1U) % DM_MC02_CAN_RX_QUEUE_SIZE);
    ctx->rx_count--;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return OM_OK;
}

OmRet bsp_can_send(uint8_t bus, uint32_t id, const uint8_t* data, uint8_t len)
{
    bsp_can_frame_t frame = {0};

    if (len > 8U || (len > 0U && data == NULL))
    {
        return OM_ERROR_PARAM;
    }

    frame.id = id;
    frame.id_type = BSP_CAN_ID_STANDARD;
    frame.frame_type = BSP_CAN_FRAME_CLASSIC;
    frame.brs = 0U;
    frame.len = len;
    if (len > 0U)
    {
        memcpy(frame.data, data, len);
    }

    return bsp_can_send_frame(bus, &frame);
}

OmRet bsp_can_get_stats(uint8_t bus, bsp_can_stats_t* stats)
{
    dm_mc02_can_ctx_t* ctx = bsp_can_ctx_from_bus(bus);
    uint32_t primask;

    if (ctx == NULL)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    if (stats == NULL)
    {
        return OM_ERROR_PARAM;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    *stats = ctx->stats;
    stats->online = (uint8_t)((ctx->initialized != 0U && (ctx->stats.error_flags & BSP_CAN_ERR_BUS_OFF) == 0U) ? 1U : 0U);
    if (primask == 0U)
    {
        __enable_irq();
    }

    return OM_OK;
}

OmRet bsp_can_get_debug_state(uint8_t bus, bsp_can_debug_state_t* debug_state)
{
    dm_mc02_can_ctx_t* ctx = bsp_can_ctx_from_bus(bus);
    uint32_t primask;

    if (ctx == NULL)
    {
        return OM_ERROR_NOT_SUPPORT;
    }

    if (debug_state == NULL)
    {
        return OM_ERROR_PARAM;
    }

    if (ctx->initialized == 0U)
    {
        return OM_ERROR;
    }

    bsp_can_protocol_status_refresh(ctx);

    primask = __get_PRIMASK();
    __disable_irq();
    *debug_state = ctx->debug_state;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return OM_OK;
}

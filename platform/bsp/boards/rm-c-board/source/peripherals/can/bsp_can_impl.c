/*
 * @Description: CAN BSP 适配实现（对接 STM32 HAL CAN 外设）
 * @date 2025-11-10
 * @author Bamboo
 */
#include "bsp_can.h"
#include <string.h>

#define BSP_CAN_FILTER_SPLIT_BANK      (14U)
#define BSP_CAN_MAX_FILTER_BANK_COUNT  (28U)

// 保持映射表按枚举顺序排列，便于人工核对。
// clang-format off（避免数组紧凑排版被打散）
static uint32_t gBs1Table[CAN_TSEG1_MAX] =
{
    CAN_BS1_1TQ, CAN_BS1_2TQ, CAN_BS1_3TQ,
    CAN_BS1_4TQ, CAN_BS1_5TQ, CAN_BS1_6TQ,
    CAN_BS1_7TQ, CAN_BS1_8TQ, CAN_BS1_9TQ,
    CAN_BS1_10TQ, CAN_BS1_11TQ, CAN_BS1_12TQ,
    CAN_BS1_13TQ, CAN_BS1_14TQ, CAN_BS1_15TQ, 
    CAN_BS1_16TQ,
};

static uint32_t gBs2Table[CAN_TSEG2_MAX] =
{
    CAN_BS2_1TQ, CAN_BS2_2TQ, CAN_BS2_3TQ,
    CAN_BS2_4TQ, CAN_BS2_5TQ, CAN_BS2_6TQ,
    CAN_BS2_7TQ, CAN_BS2_8TQ,
};

// clang-format on

static inline uint32_t bsp_can_bs1_trans(CanBS1_e bs1)
{
    while (bs1 >= CAN_TSEG1_MAX || bs1 < 0)
    {
    } // TODO: assert
    return gBs1Table[bs1];
}

static inline uint32_t bsp_can_bs2_trans(CanBS2_e bs2)
{
    while (bs2 >= CAN_TSEG2_MAX || bs2 < 0)
    {
    } // TODO: assert
    return gBs2Table[bs2];
}

static uint32_t bsp_can_sjw_trans(CanSjw_e sjw)
{
    uint32_t ret = 0;
    switch (sjw)
    {
    case CAN_SYNCJW_1TQ:
        ret = CAN_SJW_1TQ;
        break;
    case CAN_SYNCJW_2TQ:
        ret = CAN_SJW_2TQ;
        break;
    case CAN_SYNCJW_3TQ:
        ret = CAN_SJW_3TQ;
        break;
    case CAN_SYNCJW_4TQ:
        ret = CAN_SJW_4TQ;
        break;
    default:
        while (1)
        {
        }; // TODO: assert
        break;
    }
    return ret;
}

static OmRet_e bsp_can_set_filter(CAN_HandleTypeDef* hcan, CanHwFilterCfg_t cfg)
{
    CAN_FilterTypeDef FilterConfig;
    FilterConfig.FilterBank = cfg->bank;
    FilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    FilterConfig.FilterActivation = ENABLE;
    FilterConfig.FilterFIFOAssignment = (cfg->bank % 2 == 0) ? CAN_FILTER_FIFO0 : CAN_FILTER_FIFO1;
    // STM32F4 双 CAN 共享过滤器 bank，分界由 SlaveStartFilterBank 决定。
    FilterConfig.SlaveStartFilterBank = BSP_CAN_FILTER_SPLIT_BANK;
    // MASK 模式：id/mask 共同决定匹配窗口。
    if (cfg->workMode == CAN_FILTER_MODE_MASK)
    {
        uint32_t id;
        uint32_t mask;
        FilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
        if (cfg->idType == CAN_FILTER_ID_STD) // 仅标准 ID
        {
            id = cfg->id << 5;
            mask = cfg->mask << 5;
            FilterConfig.FilterIdHigh = id;
            FilterConfig.FilterIdLow = 0;
            FilterConfig.FilterMaskIdHigh = mask;
            FilterConfig.FilterMaskIdLow = CAN_ID_EXT;
        }
        else
        {
            id = cfg->id << 3;
            mask = cfg->mask << 3;
            FilterConfig.FilterIdHigh = (id >> 16) & 0xffff;
            FilterConfig.FilterIdLow = (id & 0xffff);

            FilterConfig.FilterMaskIdHigh = (mask >> 16) & 0xffff;
            FilterConfig.FilterMaskIdLow = (mask & 0xffff);

            if (cfg->idType == CAN_FILTER_ID_EXT) // 仅扩展 ID
            {
                FilterConfig.FilterIdLow |= CAN_ID_EXT;
                FilterConfig.FilterMaskIdLow |= CAN_ID_EXT;
            }
            else // 标准 + 扩展混合模式
            {
                FilterConfig.FilterIdLow = (id & 0xffff);
                FilterConfig.FilterMaskIdLow &= ~CAN_ID_EXT;
            }
        }
    }
    else
    {
        FilterConfig.FilterMode = CAN_FILTERMODE_IDLIST;
        if (cfg->idType == CAN_FILTER_ID_STD)
        {
            FilterConfig.FilterIdHigh = cfg->id << 5;
            FilterConfig.FilterIdLow = 0;
        }
        else
        {
            if (cfg->idType == CAN_FILTER_ID_EXT)
                FilterConfig.FilterIdLow = ((cfg->id << 3) & 0xffff) | CAN_ID_EXT;
            else
                FilterConfig.FilterIdLow = ((cfg->id << 3) & 0xffff);
            FilterConfig.FilterIdHigh = ((cfg->id << 3) >> 16) & 0xffff;
        }
    }
    if (HAL_CAN_ConfigFilter(hcan, &FilterConfig) != HAL_OK)
        return OM_ERROR;
    return OM_OK;
}

static OmRet_e bsp_can_clear_filter(CAN_HandleTypeDef* hcan, size_t bank)
{
    CAN_FilterTypeDef FilterConfig = {0};
    FilterConfig.FilterBank = bank;
    FilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    FilterConfig.FilterActivation = DISABLE;
    FilterConfig.FilterFIFOAssignment = (bank % 2 == 0) ? CAN_FILTER_FIFO0 : CAN_FILTER_FIFO1;
    FilterConfig.SlaveStartFilterBank = BSP_CAN_FILTER_SPLIT_BANK;
    FilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    if (HAL_CAN_ConfigFilter(hcan, &FilterConfig) != HAL_OK)
        return OM_ERROR;
    return OM_OK;
}

// 常用波特率预设参数（假设 CAN 内核时钟为 42MHz）。
static CanTimeCfg_s BspCanBitTimeTable[] = {
    {CAN_BAUD_10K, 300, {CAN_TSEG1_9TQ, CAN_TSEG2_4TQ, CAN_SYNCJW_2TQ}},
    {CAN_BAUD_20K, 150, {CAN_TSEG1_9TQ, CAN_TSEG2_4TQ, CAN_SYNCJW_2TQ}},
    {CAN_BAUD_50K, 60, {CAN_TSEG1_9TQ, CAN_TSEG2_4TQ, CAN_SYNCJW_2TQ}},
    {CAN_BAUD_100K, 30, {CAN_TSEG1_9TQ, CAN_TSEG2_4TQ, CAN_SYNCJW_2TQ}},
    {CAN_BAUD_125K, 24, {CAN_TSEG1_9TQ, CAN_TSEG2_4TQ, CAN_SYNCJW_2TQ}},
    {CAN_BAUD_250K, 12, {CAN_TSEG1_9TQ, CAN_TSEG2_4TQ, CAN_SYNCJW_2TQ}},
    {CAN_BAUD_500K, 6, {CAN_TSEG1_9TQ, CAN_TSEG2_4TQ, CAN_SYNCJW_2TQ}},
    {CAN_BAUD_800K, 4, {CAN_TSEG1_8TQ, CAN_TSEG2_4TQ, CAN_SYNCJW_2TQ}},
    {CAN_BAUD_1M, 3, {CAN_TSEG1_9TQ, CAN_TSEG2_4TQ, CAN_SYNCJW_2TQ}},
};

static CanTimeCfg_t bsp_can_time_cfg_matched(CanBaudRate_e baud)
{
    for (int i = 0; i < sizeof(BspCanBitTimeTable) / sizeof(BspCanBitTimeTable[0]); i++)
    {
        if (BspCanBitTimeTable[i].baudRate == baud)
        {
            return &BspCanBitTimeTable[i];
        }
    }
    while (1)
    {
    }; // TODO: assert
}

static OmRet_e bsp_can_configure(HalCanHandler_t can, CanCfg_t cfg)
{
    BspCan_t bsp_can = (BspCan_t)can->parent.handle;
    CAN_HandleTypeDef* hcan = (CAN_HandleTypeDef*)bsp_can;
    CanTimeCfg_t TimeCfg = bsp_can_time_cfg_matched(cfg->normalTimeCfg.baudRate);
    CanBS1_e bs1 = TimeCfg->bitTimeCfg.bs1;
    CanBS2_e bs2 = TimeCfg->bitTimeCfg.bs2;
    CanSjw_e sjw = TimeCfg->bitTimeCfg.syncJumpWidth;
    hcan->Init.Prescaler = TimeCfg->psc;
    switch (cfg->workMode)
    {
    case CAN_WORK_NORMAL:
        hcan->Init.Mode = CAN_MODE_NORMAL;
        break;
    case CAN_WORK_LOOPBACK:
        hcan->Init.Mode = CAN_MODE_LOOPBACK;
        break;
    case CAN_WORK_SILENT:
        hcan->Init.Mode = CAN_MODE_SILENT;
        break;
    case CAN_WORK_SILENT_LOOPBACK:
        hcan->Init.Mode = CAN_MODE_SILENT_LOOPBACK;
        break;
    default:
        while (1)
        {
        }; // TODO: assert
        break;
    }
    hcan->Init.SyncJumpWidth = bsp_can_sjw_trans(sjw);
    hcan->Init.TimeSeg1 = bsp_can_bs1_trans(bs1);
    hcan->Init.TimeSeg2 = bsp_can_bs2_trans(bs2);
    hcan->Init.ReceiveFifoLocked = (cfg->functionalCfg.rxFifoLockMode == 1) ? ENABLE : DISABLE;
    hcan->Init.TimeTriggeredMode = (cfg->functionalCfg.timeTriggeredMode == 1) ? ENABLE : DISABLE;
    hcan->Init.AutoBusOff = (cfg->functionalCfg.autoBusOff == 1) ? ENABLE : DISABLE;
    hcan->Init.AutoRetransmission = (cfg->functionalCfg.autoRetransmit == 1) ? ENABLE : DISABLE;
    hcan->Init.AutoWakeUp = (cfg->functionalCfg.autoWakeUp == 1) ? ENABLE : DISABLE;
    if (HAL_CAN_Init(hcan) != HAL_OK)
    {
        while (1)
        {
        }; // TODO: assert
    }
    HAL_CAN_ActivateNotification(hcan, CAN_IT_ERROR | CAN_IT_BUSOFF | CAN_IT_ERROR_PASSIVE | CAN_IT_ERROR_WARNING | CAN_IT_LAST_ERROR_CODE);
    return OM_OK;
}

#ifdef USE_CAN1
static const uint8_t gCan1HwBanks[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
#endif

#ifdef USE_CAN2
static const uint8_t gCan2HwBanks[] = {14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
#endif

static OmRet_e bsp_can_control(HalCanHandler_t can, uint32_t cmd, void *arg)
{
    if (!can || !can->parent.handle)
        return OM_ERROR_PARAM;

    CAN_HandleTypeDef *hcan = (CAN_HandleTypeDef *)can->parent.handle;
    OmRet_e ret = OM_OK;

    switch (cmd)
    {
    case CAN_CMD_GET_STATUS:
    {
        CanErrCounter_t errCounter = (CanErrCounter_t)arg;
        uint32_t errReg = READ_REG(hcan->Instance->ESR);
        errCounter->txErrCnt = (errReg >> 16) & 0xFF;
        errCounter->rxErrCnt = (errReg >> 24);
        ret = OM_OK;
    }
    break;
    case CAN_CMD_GET_CAPABILITY:
    {
        if (arg == NULL)
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        CanHwCapability_t capability = (CanHwCapability_t)arg;
#ifdef USE_CAN2
        if (hcan->Instance == CAN2)
        {
            capability->hwBankCount = sizeof(gCan2HwBanks) / sizeof(gCan2HwBanks[0]);
            capability->hwBankList = gCan2HwBanks;
        }
        else
#endif
        {
#ifdef USE_CAN1
            capability->hwBankCount = sizeof(gCan1HwBanks) / sizeof(gCan1HwBanks[0]);
            capability->hwBankList = gCan1HwBanks;
#else
            capability->hwBankCount = 0;
            capability->hwBankList = NULL;
#endif
        }
    }
    break;
    case CAN_CMD_START:
        // 启动 CAN 外设
        if (HAL_CAN_Start(hcan) != HAL_OK)
            ret = OM_ERROR;
        break;

    case CAN_CMD_CFG:
        // 配置 CAN
        ret = bsp_can_configure(can, (CanCfg_t)arg);
        break;

    case CAN_CMD_SUSPEND:
        // 暂停 CAN 外设
        if (HAL_CAN_Stop(hcan) != HAL_OK)
            ret = OM_ERROR;
        break;

    case CAN_CMD_RESUME:
        // 恢复 CAN 外设
        if (HAL_CAN_Start(hcan) != HAL_OK)
            ret = OM_ERROR;
        break;

    case CAN_CMD_SET_IOTYPE:
        // 设置 CAN IO 类型并开启对应中断
        if (arg == NULL)
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        uint32_t io_type = *(uint32_t*)arg;
        if (io_type == CAN_REG_INT_TX)
        {
            // 开启发送邮箱空中断
            uint32_t txIntEvents = CAN_IT_TX_MAILBOX_EMPTY;
            HAL_CAN_ActivateNotification(hcan, txIntEvents);
        }
        else if (io_type == CAN_REG_INT_RX)
        {
            uint32_t rxIntEvents = CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO1_MSG_PENDING | CAN_IT_RX_FIFO0_OVERRUN |
                                   CAN_IT_RX_FIFO1_OVERRUN | CAN_IT_RX_FIFO0_FULL | CAN_IT_RX_FIFO1_FULL;
            HAL_CAN_ActivateNotification(hcan, rxIntEvents);
        }
        break;

    case CAN_CMD_CLR_IOTYPE:
    {
        if (arg == NULL)
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        // 清除 CAN IO 类型并关闭对应中断
        uint32_t io_type = *(uint32_t*)arg;
        if (io_type == CAN_REG_INT_TX)
        {
            // 关闭发送邮箱空中断
            HAL_CAN_DeactivateNotification(hcan, CAN_IT_TX_MAILBOX_EMPTY);
        }
        else if (io_type == CAN_REG_INT_RX)
        {
            uint32_t rxIntEvents = CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO1_MSG_PENDING | // FIFO 待处理
                                   CAN_IT_RX_FIFO0_OVERRUN | CAN_IT_RX_FIFO1_OVERRUN |         // FIFO 溢出
                                   CAN_IT_RX_FIFO0_FULL | CAN_IT_RX_FIFO1_FULL;                // FIFO 满
            HAL_CAN_DeactivateNotification(hcan, rxIntEvents);
        }
    }
    break;

    case CAN_CMD_CLOSE:
        // 关闭 CAN 外设
        if (HAL_CAN_Stop(hcan) != HAL_OK)
            ret = OM_ERROR;
        // TODO: 清理错误状态、邮箱状态和可能的残留中断标志
        break;

    case CAN_CMD_FLUSH:
        // 清空接收 FIFO（按 STM32 HAL 语义手动释放 FIFO 输出邮箱）
        // 注意：该操作只影响硬件接收缓存，不影响上层软件 FIFO
        hcan->Instance->RF0R |= CAN_RF0R_RFOM0;
        hcan->Instance->RF1R |= CAN_RF1R_RFOM1;
        break;

    case CAN_CMD_FILTER_ALLOC:
    {
        if (arg == NULL)
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        CanHwFilterCfg_t filter_cfg = (CanHwFilterCfg_t)arg;
        if ((hcan->Instance == CAN1) && filter_cfg->bank >= BSP_CAN_FILTER_SPLIT_BANK)
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        if ((hcan->Instance == CAN2) &&
            (filter_cfg->bank < BSP_CAN_FILTER_SPLIT_BANK || filter_cfg->bank >= BSP_CAN_MAX_FILTER_BANK_COUNT))
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        ret = bsp_can_set_filter(hcan, filter_cfg);
    }
    break;

    case CAN_CMD_FILTER_FREE:
    {
        if (arg == NULL)
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        CanHwFilterCfg_t filter_cfg = (CanHwFilterCfg_t)arg;
        ret = bsp_can_clear_filter(hcan, filter_cfg->bank);
    }
    break;

    default:
        ret = OM_ERROR_PARAM;
        break;
    }

    return ret;
}

static OmRet_e bsp_can_recv_msg(HalCanHandler_t can, CanHwMsg_t msg, int32_t rxfifo_bank)
{
    CAN_RxHeaderTypeDef rx_header;
    uint8_t data[8];

    CAN_HandleTypeDef *hcan = (CAN_HandleTypeDef *)can->parent.handle;

    // 先检查指定硬件 FIFO 是否有可读报文
    uint32_t fifo_fill_level = HAL_CAN_GetRxFifoFillLevel(hcan, rxfifo_bank);
    if (fifo_fill_level == 0)
        return OM_ERROR_EMPTY;

    // 从指定 FIFO 读取一帧报文
    if (HAL_CAN_GetRxMessage(hcan, rxfifo_bank, &rx_header, data) != HAL_OK)
        return OM_ERROR;

    if (!msg) // 上层软件 FIFO 满时，core 会传入 NULL，BSP 只需完成硬件读出并返回溢出
        return OM_ERR_OVERFLOW;

    // 填充 core 层硬件报文结构
    msg->dsc.id = (rx_header.IDE == CAN_ID_STD) ? rx_header.StdId : rx_header.ExtId;
    msg->dsc.idType = (rx_header.IDE == CAN_IDE_STD) ? CAN_IDE_STD : CAN_IDE_EXT;
    msg->dsc.msgType = (rx_header.RTR == CAN_RTR_DATA) ? CAN_MSG_TYPE_DATA : CAN_MSG_TYPE_REMOTE;
    msg->dsc.dataLen = rx_header.DLC; 
    // 标准 CAN 数据长度范围 0~8，按 HAL 返回 DLC 直接透传
    // FilterMatchIndex 是本 CAN 实例内索引，需要换算为全局硬件 bank
    int16_t hwFilterBank = (int16_t)rx_header.FilterMatchIndex;
#ifdef USE_CAN2
    if (hcan->Instance == CAN2)
        hwFilterBank = (int16_t)(hwFilterBank + (int16_t)BSP_CAN_FILTER_SPLIT_BANK);
#endif
    msg->hwFilterBank = hwFilterBank;
    msg->hwTxMailbox = -1;
    msg->dsc.timeStamp = rx_header.Timestamp;
    memcpy(msg->data, data, msg->dsc.dataLen);
    return OM_OK;
}

static OmRet_e bsp_can_send_msg(HalCanHandler_t can, CanHwMsg_t msg)
{
    CAN_TxHeaderTypeDef tx_header;
    if (!can || !can->parent.handle || !msg)
        return OM_ERROR_PARAM;

    CAN_HandleTypeDef *hcan = (CAN_HandleTypeDef *)can->parent.handle;

    // 发送前先检查硬件邮箱是否有空槽
    uint32_t free_level = HAL_CAN_GetTxMailboxesFreeLevel(hcan);
    if (free_level == 0)
    {
        msg->hwTxMailbox = -1;
        return OM_ERR_OVERFLOW;
    }

    // 组织发送头
    tx_header.StdId = msg->dsc.id;
    tx_header.ExtId = msg->dsc.id;
    tx_header.IDE = (msg->dsc.idType == CAN_IDE_EXT) ? CAN_ID_EXT : CAN_ID_STD;
    tx_header.RTR = (msg->dsc.msgType == CAN_MSG_TYPE_REMOTE) ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    tx_header.DLC = msg->dsc.dataLen;
    tx_header.TransmitGlobalTime = DISABLE; // 不使能全局时间戳回传
    // 拷贝发送数据
    uint8_t data[8];
    if (msg->data != NULL && msg->dsc.dataLen > 0)
    {
        memcpy(data, msg->data, msg->dsc.dataLen);
    }
    // 发送并获取实际占用的硬件邮箱
    uint32_t txMailboxBank = 0;
    if (HAL_CAN_AddTxMessage(hcan, &tx_header, data, &txMailboxBank) != HAL_OK)
    {
        msg->hwTxMailbox = -1;
        return OM_ERROR;
    }

    // 将 HAL 邮箱标识映射为 core 约定的 0/1/2
    switch (txMailboxBank)
    {
    case CAN_TX_MAILBOX0:
        msg->hwTxMailbox = 0;
        break;
    case CAN_TX_MAILBOX1:
        msg->hwTxMailbox = 1;
        break;
    case CAN_TX_MAILBOX2:
        msg->hwTxMailbox = 2;
        break;
    }
    return OM_OK;
}

static CanHwInterface_s gCanHwInterface = {
    .configure = bsp_can_configure,
    .control = bsp_can_control,
    .recvMsg = bsp_can_recv_msg,
    .sendMsgMailbox = bsp_can_send_msg,
};

BspCan_s gBspCan[] = {
#ifdef USE_CAN1
    BSP_CAN_STATIC_INIT(CAN1, "can1", CAN1_REG_PARAMS),
#endif
#ifdef USE_CAN2
    BSP_CAN_STATIC_INIT(CAN2, "can2", CAN2_REG_PARAMS),
#endif
};

static void bsp_can_pre_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    CAN_HandleTypeDef* hcan = &gBspCan[BSP_CAN1_IDX].handle;
#ifdef USE_CAN1
    {
        (void)hcan;
        __HAL_RCC_CAN1_CLK_ENABLE();
        if (__HAL_RCC_GPIOD_IS_CLK_DISABLED())
            __HAL_RCC_GPIOD_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
        GPIO_InitStruct.Alternate = GPIO_AF9_CAN1;
        HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
        HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 5,0); // 中断优先级需低于 FreeRTOS 可屏蔽阈值，避免破坏内核临界区
        HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
        HAL_NVIC_SetPriority(CAN1_RX1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(CAN1_RX1_IRQn);
        HAL_NVIC_SetPriority(CAN1_TX_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(CAN1_TX_IRQn);
        HAL_NVIC_SetPriority(CAN1_SCE_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);
    }
#endif
#ifdef USE_CAN2
    {
        __HAL_RCC_CAN2_CLK_ENABLE();
        if (__HAL_RCC_GPIOB_IS_CLK_DISABLED())
            __HAL_RCC_GPIOB_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6;
        GPIO_InitStruct.Alternate = GPIO_AF9_CAN2;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
        HAL_NVIC_EnableIRQ(CAN2_RX0_IRQn);
        HAL_NVIC_SetPriority(CAN2_RX0_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(CAN2_RX1_IRQn);
        HAL_NVIC_SetPriority(CAN2_RX1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(CAN2_TX_IRQn);
        HAL_NVIC_SetPriority(CAN2_TX_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(CAN2_SCE_IRQn);
        HAL_NVIC_SetPriority(CAN2_SCE_IRQn, 5, 0);
    }
#endif
}

void bsp_can_register(void)
{
    uint8_t cnt = sizeof(gBspCan) / sizeof(gBspCan[0]);
    OmRet_e ret = OM_OK;
    for (int i = 0; i < cnt; i++)
    {
        gBspCan[i].parent.hwInterface = &gCanHwInterface;
        gBspCan[i].parent.adapterInterface = hal_can_get_classic_adapter_interface();
        ret = hal_can_register(&gBspCan[i].parent, gBspCan[i].name, &gBspCan[i], gBspCan[i].regparams);
        while (ret != OM_OK)
        {
        }; // TODO: assert
    }
    bsp_can_pre_init();
}

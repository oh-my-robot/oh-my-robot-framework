/*
 * @Description: STM32F4 家族 CAN BSP 共享适配层（板瘦身：板=数据、驱动=通用）
 * @details 由 rm-a/rm-c 两板逐字相同的 bsp_can_impl.c 提炼；板特有数据（实例/波特率/
 *          引脚/中断）全部外置到板侧 bsp_can_data.c，经 f4.h 契约 extern 引用。
 *          启用外设 = 板 lua selfreg_sources 引用本文件（含 OM_INIT 自注册）。
 * @date 2025-11-10
 * @author Bamboo
 */
#include "bsp_can.h"
#include <string.h>
#include "core/om_init.h"
#include "f4_clk.h"

/* 契约守卫：板 shim bsp_can.h 必须定义实例数，否则编译期报错（防漏板宏静默漂移） */
#ifndef BSP_CAN_COUNT
#error "bsp_can_f4.c requires BSP_CAN_COUNT (define in board bsp_can.h shim)"
#endif

/* 分散加载自注册：把 bsp_can_register 挂到 .om_init 段（BOARD 级，由 om_do_initcalls 自动调用） */
static OmRet bsp_can_self_init(void)
{
    bsp_can_register();
    return OM_OK;
}
OM_INIT_BOARD(bsp_can_self_init);

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

static inline uint32_t bsp_can_bs1_trans(CanBS1 bs1)
{
    while (bs1 >= CAN_TSEG1_MAX || bs1 < 0)
    {
    } // TODO: assert
    return gBs1Table[bs1];
}

static inline uint32_t bsp_can_bs2_trans(CanBS2 bs2)
{
    while (bs2 >= CAN_TSEG2_MAX || bs2 < 0)
    {
    } // TODO: assert
    return gBs2Table[bs2];
}

static uint32_t bsp_can_sjw_trans(CanSjw sjw)
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

static OmRet bsp_can_set_filter(CAN_HandleTypeDef* hcan, CanHwFilterCfg* cfg)
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

static OmRet bsp_can_clear_filter(CAN_HandleTypeDef* hcan, size_t bank)
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

/** 波特率匹配：表来自板数据，以 psc=0 哨兵项结尾（extern 数组不可 sizeof） */
static CanTimeCfg* bsp_can_time_cfg_matched(CanBaudRate baud)
{
    for (int i = 0; BspCanBitTimeTable[i].psc != 0; i++)
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

static OmRet bsp_can_configure(HalCanHandler* can, CanCfg* cfg)
{
    BspCan_t bsp_can = (BspCan_t)can->parent.handle;
    CAN_HandleTypeDef* hcan = (CAN_HandleTypeDef*)bsp_can;
    CanTimeCfg* TimeCfg = bsp_can_time_cfg_matched(cfg->normalTimeCfg.baudRate);
    CanBS1 bs1 = TimeCfg->bitTimeCfg.bs1;
    CanBS2 bs2 = TimeCfg->bitTimeCfg.bs2;
    CanSjw sjw = TimeCfg->bitTimeCfg.syncJumpWidth;
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

static OmRet bsp_can_control(HalCanHandler* can, uint32_t cmd, void *arg)
{
    if (!can || !can->parent.handle)
        return OM_ERROR_PARAM;

    CAN_HandleTypeDef *hcan = (CAN_HandleTypeDef *)can->parent.handle;
    BspCan_t bsp_can = (BspCan_t)can->parent.handle;
    OmRet ret = OM_OK;

    switch (cmd)
    {
    case CAN_CMD_GET_STATUS:
    {
        CanErrCounter* errCounter = (CanErrCounter*)arg;
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
        CanHwCapability* capability = (CanHwCapability*)arg;
        /* 板数据驱动：本实例的 bank 分配由数据表描述 */
        capability->hwBankCount = bsp_can->hwBankCount;
        capability->hwBankList = bsp_can->hwBankList;
    }
    break;
    case CAN_CMD_START:
        // 启动 CAN 外设
        if (HAL_CAN_Start(hcan) != HAL_OK)
            ret = OM_ERROR;
        break;

    case CAN_CMD_CFG:
        // 配置 CAN
        ret = bsp_can_configure(can, (CanCfg*)arg);
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
        CanHwFilterCfg* filter_cfg = (CanHwFilterCfg*)arg;
        /* 板数据驱动：bank 合法性 = 本实例的 [hwBankList[0], hwBankList[0]+hwBankCount) 区间 */
        uint8_t first_bank = bsp_can->hwBankList[0];
        if (filter_cfg->bank < first_bank ||
            filter_cfg->bank >= (size_t)first_bank + bsp_can->hwBankCount)
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
        CanHwFilterCfg* filter_cfg = (CanHwFilterCfg*)arg;
        ret = bsp_can_clear_filter(hcan, filter_cfg->bank);
    }
    break;

    default:
        ret = OM_ERROR_PARAM;
        break;
    }

    return ret;
}

static OmRet bsp_can_recv_msg(HalCanHandler* can, CanHwMsg* msg, int32_t rxfifo_bank)
{
    CAN_RxHeaderTypeDef rx_header;
    uint8_t data[8];

    CAN_HandleTypeDef *hcan = (CAN_HandleTypeDef *)can->parent.handle;
    BspCan_t bsp_can = (BspCan_t)can->parent.handle;

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
    /* bxCAN 返回的 FilterMatchIndex 是"当前 FIFO 内命中的过滤器序号"，
     * 不是全局 bank 编号。当前 BSP 固定按 bank 奇偶分配 FIFO：
     * - 偶数 bank -> FIFO0
     * - 奇数 bank -> FIFO1
     * 且全部使用 32bit filter，因此可以按 FIFO 内序号还原真实 bank。
     * 板数据驱动：全局 bank = FIFO 内序号还原 + 本实例起始 bank（hwBankList[0]）偏移。
     */
    int16_t hwFilterBank = (int16_t)(rx_header.FilterMatchIndex * 2u + (uint32_t)rxfifo_bank + bsp_can->hwBankList[0]);
    if (msg->dsc.dataLen > 8u)
    {
        return OM_ERROR_PARAM;
    }
    msg->hwFilterBank = hwFilterBank;
    msg->hwTxMailbox = -1;
    msg->dsc.timeStamp = rx_header.Timestamp;
    memcpy(msg->data, data, msg->dsc.dataLen);
    return OM_OK;
}

static OmRet bsp_can_send_msg(HalCanHandler* can, CanHwMsg* msg)
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
    if (msg->dsc.dataLen > 8u || (msg->dsc.dataLen > 0u && msg->data == NULL))
    {
        msg->hwTxMailbox = -1;
        return OM_ERROR_PARAM;
    }
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

static CanHwInterface gCanHwInterface = {
    .configure = bsp_can_configure,
    .control = bsp_can_control,
    .recvMsg = bsp_can_recv_msg,
    .sendMsgMailbox = bsp_can_send_msg,
};

/*===========================================================================
 * 板级预初始化（数据驱动：引脚表 + 中断表；时钟宏无法数据化，用家族 switch-helper）
 *===========================================================================*/

/** @brief 使能 CAN 外设时钟（先查后开） */
static void bsp_can_enable_can_clk(CAN_TypeDef *instance)
{
    if (instance == CAN1)
    {
        if (__HAL_RCC_CAN1_IS_CLK_DISABLED()) __HAL_RCC_CAN1_CLK_ENABLE();
    }
    else if (instance == CAN2)
    {
        if (__HAL_RCC_CAN2_IS_CLK_DISABLED()) __HAL_RCC_CAN2_CLK_ENABLE();
    }
}

/** @brief 板级预初始化（数据驱动：引脚 AF + 时钟使能 + 中断，逐实例） */
static void bsp_can_pre_init(void)
{
    GPIO_InitTypeDef init = {0};
    init.Mode = GPIO_MODE_AF_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;

    for (uint8_t i = 0; i < BSP_CAN_COUNT; i++)
    {
        bsp_can_enable_can_clk(gBspCan[i].handle.Instance);

        for (uint8_t j = 0; j < 2; j++) /* [RX, TX] 引脚 */
        {
            const BspCanPinCfg *pin = &BspCanPinTable[i][j];
            bsp_f4_enable_gpio_clk(pin->port);
            init.Pin = pin->pin;
            init.Alternate = pin->af;
            HAL_GPIO_Init(pin->port, &init);
        }

        for (uint8_t k = 0; k < 4; k++) /* [RX0, RX1, TX, SCE] 中断 */
        {
            /* 中断优先级需低于 RTOS 可屏蔽阈值，避免破坏内核临界区（板数据决定） */
            HAL_NVIC_SetPriority(BspCanIrqTable[i][k].irqn, BspCanIrqTable[i][k].preemptPrio, 0);
            HAL_NVIC_EnableIRQ(BspCanIrqTable[i][k].irqn);
        }
    }
}

void bsp_can_register(void)
{
    OmRet ret = OM_OK;
    for (int i = 0; i < BSP_CAN_COUNT; i++)
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

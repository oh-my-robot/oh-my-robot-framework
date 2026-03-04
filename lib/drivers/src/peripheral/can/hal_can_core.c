#include "core/data_struct/bitmap.h"
#include "core/om_def.h"
#include "drivers/peripheral/can/pal_can_dev.h"
#include "osal/osal_core.h"
#include <string.h>

static inline OsalIrqIsrState can_irq_lock(void)
{
    if (osal_is_in_isr())
        return osal_irq_lock_from_isr();

    osal_irq_lock_task();
    return (OsalIrqIsrState)0u;
}

static inline void can_irq_unlock(OsalIrqIsrState state)
{
    if (osal_is_in_isr())
    {
        osal_irq_unlock_from_isr(state);
        return;
    }
    osal_irq_unlock_task();
}

/**
 * @brief 根据硬件滤波 bank 查找框架 filter slot
 * @note 仅在 slot 已占用时检查映射关系，返回值可直接作为 `filterHandle` 使用
 */
static int32_t can_find_slot_by_hwbank(HalCanHandler* can, int32_t hw_bank)
{
    CanFilterResMgr* mgr = &can->filterResMgr;
    for (uint16_t slot = 0; slot < mgr->slotCount; slot++)
    {
        // 只在“已占用 slot”上做映射匹配，避免读取到历史残留的 slotToHwBank 值
        if (!om_bitmap_atomic_test(&mgr->slotUsedMap, slot))
            continue;
        if (mgr->slotToHwBank[slot] == hw_bank)
            return slot;
    }
    return -1;
}

/**
 * @brief 释放指定 slot，并回收对应硬件 bank 占用状态
 */
static void can_release_slot(HalCanHandler* can, uint16_t slot)
{
    CanFilterResMgr* mgr = &can->filterResMgr;
    OsalIrqIsrState int_level = can_irq_lock();
    int16_t hw_bank = mgr->slotToHwBank[slot];
    // 释放顺序：先回收硬件 bank，再回收 slot，最后清空映射，保持资源状态一致
    if (hw_bank >= 0 && hw_bank <= mgr->maxHwBank)
        om_bitmap_atomic_clear(&mgr->hwBankUsedMap, (size_t)hw_bank);
    om_bitmap_atomic_clear(&mgr->slotUsedMap, slot);
    mgr->slotToHwBank[slot] = -1;
    can_irq_unlock(int_level);
}

/**
 * @brief 分配一个 filter slot 和一个可用硬件 bank
 * @note 该函数在临界区内同时更新 slot/hwBank 两个位图，保证分配原子性
 */
static OmRet can_reserve_slot(HalCanHandler* can, uint16_t *slot_out, int16_t *hw_bank_out)
{
    CanFilterResMgr* mgr = &can->filterResMgr;
    OsalIrqIsrState int_level = can_irq_lock();
    uint16_t slot = (uint16_t)0xFFFFu;
    int16_t hw_bank = -1;

    // 第一步：在 slot 空间中找一个空闲逻辑句柄
    for (uint16_t idx = 0; idx < mgr->slotCount; idx++)
    {
        if (!om_bitmap_atomic_test(&mgr->slotUsedMap, idx))
        {
            slot = idx;
            break;
        }
    }
    if (slot == (uint16_t)0xFFFFu)
    {
        can_irq_unlock(int_level);
        return OM_ERROR_BUSY;
    }

    // 第二步：只在 capability 给出的 bank 列表中挑选可用硬件 bank
    // 不假设 bank 连续，也不依赖固定区间
    for (uint16_t idx = 0; idx < mgr->slotCount; idx++)
    {
        uint16_t candidate = mgr->hwBankList[idx];
        if (!om_bitmap_atomic_test(&mgr->hwBankUsedMap, candidate))
        {
            hw_bank = (int16_t)candidate;
            break;
        }
    }
    if (hw_bank < 0)
    {
        can_irq_unlock(int_level);
        return OM_ERROR_BUSY;
    }

    // 第三步：CAS 方式占用 slot；失败说明并发下被其他路径抢占
    if (!om_bitmap_atomic_try_set(&mgr->slotUsedMap, slot))
    {
        can_irq_unlock(int_level);
        return OM_ERROR_BUSY;
    }

    // 第四步：占用硬件 bank；若失败则回滚 slot，保证“不半成功”
    if (!om_bitmap_atomic_try_set(&mgr->hwBankUsedMap, (size_t)hw_bank))
    {
        om_bitmap_atomic_clear(&mgr->slotUsedMap, slot);
        can_irq_unlock(int_level);
        return OM_ERROR_BUSY;
    }

    mgr->slotToHwBank[slot] = hw_bank;
    can_irq_unlock(int_level);

    *slot_out = slot;
    *hw_bank_out = hw_bank;
    return OM_OK;
}

/**
 * @brief 释放过滤器资源管理器内部动态资源
 */
static void can_filter_resmgr_deinit(HalCanHandler* can)
{
    CanFilterResMgr* mgr = &can->filterResMgr;
    // words init 阶段动态分配，这里统一回收
    if (mgr->slotUsedMap.words != NULL)
        osal_free((void *)mgr->slotUsedMap.words);
    if (mgr->hwBankUsedMap.words != NULL)
        osal_free((void *)mgr->hwBankUsedMap.words);
    if (mgr->hwBankList)
        osal_free(mgr->hwBankList);
    if (mgr->slotToHwBank)
        osal_free(mgr->slotToHwBank);
    // 清零后可安全重复初始化，避免悬挂指针
    memset(mgr, 0, sizeof(CanFilterResMgr));
}

/**
 * @brief 从硬件能力初始化过滤器资源管理器
 * @note slot 数量由 BSP 上报 `hwBankCount` 决定，slot 与 hw bank 通过 `slotToHwBank` 建立映射
 */
static OmRet can_filter_resmgr_init(HalCanHandler* can)
{
    CanFilterResMgr* mgr = &can->filterResMgr;
    CanHwCapability capability = {0};
    OmRet ret = can->hwInterface->control(can, CAN_CMD_GET_CAPABILITY, &capability);
    if (ret != OM_OK || capability.hwBankCount == 0 || capability.hwBankList == NULL)
        return OM_ERROR_PARAM;

    // 支持重复 open/init：先清理旧资源，再按最新 capability 重建
    can_filter_resmgr_deinit(can);

    mgr->slotCount = capability.hwBankCount;
    mgr->hwBankList = (uint8_t *)osal_malloc(mgr->slotCount);
    mgr->slotToHwBank = (int16_t *)osal_malloc(sizeof(int16_t) * mgr->slotCount);
    if (mgr->hwBankList == NULL || mgr->slotToHwBank == NULL)
    {
        can_filter_resmgr_deinit(can);
        return OM_ERROR_MEMORY;
    }
    // 拷贝 bank 列表，避免直接依赖 BSP 侧内存生命周期
    memcpy(mgr->hwBankList, capability.hwBankList, mgr->slotCount);

    mgr->maxHwBank = 0;
    for (uint16_t i = 0; i < mgr->slotCount; i++)
    {
        // -1 表示 slot 当前未绑定任何硬件 bank
        mgr->slotToHwBank[i] = -1;
        if (mgr->hwBankList[i] > mgr->maxHwBank)
            mgr->maxHwBank = mgr->hwBankList[i];
    }

    // slot 位图按逻辑句柄数量分配
    // hwBank 位图按“最大 bank 下标 + 1”分配，支持离散 bank 编号
    size_t hw_bank_bit_count = (size_t)mgr->maxHwBank + 1u;
    OmAtomicUlong *slot_words = om_bitmap_atomic_buffer_alloc((size_t)mgr->slotCount, osal_malloc);
    OmAtomicUlong *hw_bank_words = om_bitmap_atomic_buffer_alloc(hw_bank_bit_count, osal_malloc);
    if (slot_words == NULL || hw_bank_words == NULL)
    {
        if (slot_words != NULL)
            om_bitmap_buffer_free(slot_words, osal_free);
        if (hw_bank_words != NULL)
            om_bitmap_buffer_free(hw_bank_words, osal_free);
        can_filter_resmgr_deinit(can);
        return OM_ERROR_MEMORY;
    }

    // 初始化后两张位图均为空闲状态（0）
    ret = om_bitmap_atomic_init(&mgr->slotUsedMap, slot_words, (size_t)mgr->slotCount);
    if (ret != OM_OK)
    {
        om_bitmap_buffer_free(slot_words, osal_free);
        om_bitmap_buffer_free(hw_bank_words, osal_free);
        can_filter_resmgr_deinit(can);
        return ret;
    }
    ret = om_bitmap_atomic_init(&mgr->hwBankUsedMap, hw_bank_words, hw_bank_bit_count);
    if (ret != OM_OK)
    {
        om_bitmap_buffer_free(slot_words, osal_free);
        om_bitmap_buffer_free(hw_bank_words, osal_free);
        can_filter_resmgr_deinit(can);
        return ret;
    }
    return OM_OK;
}

static void can_status_timer_cb(OsalTimer* x_timer)
{
    HalCanHandler* can = (HalCanHandler*)osal_timer_get_id(x_timer);
    CanStatusManager* status_manager = &can->statusManager;
    // 检查 CAN 状态
    can->hwInterface->control(can, CAN_CMD_GET_STATUS, (void *)&status_manager->errCounter);
    size_t can_tx_err_cnt = status_manager->errCounter.txErrCnt;
    size_t can_rx_err_cnt = status_manager->errCounter.rxErrCnt;
    if (can_rx_err_cnt < 127 && can_tx_err_cnt < 127)
        can->statusManager.nodeErrStatus = CAN_NODE_STATUS_ACTIVE;
    else if (can_rx_err_cnt > 127 || can_tx_err_cnt > 127)
        can->statusManager.nodeErrStatus = CAN_NODE_STATUS_PASSIVE;
    else if (can_tx_err_cnt > 255)
        can->statusManager.nodeErrStatus = CAN_NODE_STATUS_BUSOFF;
    device_err_cb(&can->parent, CAN_ERR_EVENT_BUS_STATUS, can->statusManager.nodeErrStatus);
}

/*
 * @brief 初始化 CAN 接收 FIFO
 * @param Can CAN 句柄
 * @param msgNum 接收消息缓存数量
 * @retval  错误码（OmRet）描述
 *          OM_OK             成功
 *          OM_ERROR_MEMORY   内存分配失败
 */
static OmRet can_fifo_init(HalCanHandler* can, CanMsgFifo* can_fifo, uint32_t msg_num, uint8_t is_over_write)
{
    OmRet ret = OM_OK;
    INIT_LIST_HEAD(&can_fifo->freeList);
    can_fifo->msgBuffer = can->adapterInterface->msgbufferAlloc(&can_fifo->freeList, msg_num);
    while (can_fifo->msgBuffer == NULL)
    {
    };
    // 初始化链表
    INIT_LIST_HEAD(&can_fifo->usedList);
    can_fifo->freeCount = msg_num;
    can_fifo->isOverwrite = is_over_write;
    return ret;
}

/*
 * @brief 初始化 CAN 接收过滤器
 * @param RxHandler CAN 接收句柄
 * @param filterNum 接收过滤器数量
 * @retval  错误码（OmRet）描述
 *          OM_OK             成功
 *          OM_ERROR_MEMORY   内存分配失败
 */
static OmRet can_filter_init(CanRxHandler* rx_handler, uint32_t filter_num)
{
    uint32_t filter_sz;
    if (filter_num > 0)
    {
        filter_sz = filter_num * sizeof(CanFilter);
        rx_handler->filterTable = (CanFilter*)osal_malloc(filter_sz);
        if (rx_handler->filterTable == NULL)
        {
            // TODO: ASSERT
            return OM_ERROR_MEMORY;
        }
        memset(rx_handler->filterTable, 0, filter_sz);
        for (uint32_t i = 0; i < filter_num; i++)
            INIT_LIST_HEAD(&rx_handler->filterTable[i].msgMatchedList);
    }
    else
    {
        rx_handler->filterTable = NULL;
    }
    return OM_OK;
}

/*
 * @brief 初始化 CAN 接收句柄
 * @param Can CAN 句柄
 * @param filterNum 接收过滤器数量
 * @param msgNum 接收消息缓存数量
 * @retval  错误码（OmRet）描述
 *          OM_OK             成功
 *          OM_ERROR_BUSY     句柄已初始化
 *          OM_ERROR_MEMORY   内存分配失败
 */
static OmRet can_rxhandler_init(HalCanHandler* can, uint32_t iotype, uint32_t filter_num, uint32_t msg_num)
{
    CanRxHandler* rx_handler;
    uint32_t reg_io_type;
    uint32_t is_oparam_valid;
    OmRet ret = OM_OK;
    // 至少初始化一个滤波器
    while (filter_num <= 0 || msg_num <= 0 || !can->adapterInterface->msgbufferAlloc)
    {
    }; // TODO: ASSERT

    // 检查 IO 类型是否有效
    reg_io_type = device_get_regparams(&can->parent) & DEVICE_REG_RXTYPE_MASK;
    is_oparam_valid = (iotype & reg_io_type);
    if (!is_oparam_valid)
    {
        // TODO: ASSERT "Invalid IO type"
        return OM_ERROR_PARAM;
    }

    rx_handler = &can->rxHandler;
    if (rx_handler->filterTable != NULL || rx_handler->rxFifo.msgBuffer != NULL)
        return OM_ERROR_BUSY; // TODO:LOG

    ret = can_fifo_init(can, &rx_handler->rxFifo, msg_num, can->cfg.functionalCfg.isRxOverwrite);
    if (ret != OM_OK)
        return ret; // TODO: ASSERT
    ret = can_filter_init(rx_handler, filter_num);
    if (ret != OM_OK)
    {
        osal_free(rx_handler->rxFifo.msgBuffer);
        rx_handler->rxFifo.msgBuffer = NULL;
        // TODO: ASSERT
        return ret;
    }
    can->hwInterface->control(can, CAN_CMD_SET_IOTYPE, (void *)&iotype);
    return ret;
}

static OmRet can_status_manager_init(HalCanHandler* can)
{
    if (can->statusManager.statusTimer != NULL)
        return OM_ERROR_BUSY;
    CanStatusManager* status_manager;
    char *name = device_get_name(&can->parent);
    OsalStatus osal_status;
    status_manager = &can->statusManager;
    osal_status = osal_timer_create(&status_manager->statusTimer, name, can->cfg.statusCheckTimeout, OSAL_TIMER_PERIODIC,
                                    (void *)can, can_status_timer_cb);
    if (osal_status != OSAL_OK)
    {
        // TODO: ASSERT
        return OM_ERROR_MEMORY;
    }

    osal_status = osal_timer_start(status_manager->statusTimer, can->cfg.statusCheckTimeout);
    if (osal_status != OSAL_OK)
    {
        // TODO: ASSERT
        return OM_ERROR_TIMEOUT;
    }
    return OM_OK;
}

static void can_status_manager_deinit(HalCanHandler* can)
{
    CanStatusManager* status_manager = &can->statusManager;
    if (status_manager->statusTimer != NULL)
    {
        osal_timer_delete(status_manager->statusTimer, can->cfg.statusCheckTimeout);
        status_manager->statusTimer = NULL;
    }
}

DBG_PARAM_DEF(CanMailbox*, dbg_mailbox[3]) = {0};

static OmRet can_txhandler_init(HalCanHandler* can, uint32_t iotype, size_t mailbox_num, uint32_t tx_msg_num)
{
    CanTxHandler* tx_handler;
    uint32_t reg_io_type;
    uint32_t is_oparam_valid;
    OmRet ret = OM_OK;
    while (mailbox_num <= 0 || tx_msg_num <= 0)
    {
    }; // TODO: ASSERT

    // 检查 IO 类型是否有效
    reg_io_type = device_get_regparams(&can->parent) & DEVICE_REG_TXTYPE_MASK;
    is_oparam_valid = (reg_io_type & iotype);
    if (!is_oparam_valid)
    {
        // TODO: ASSERT "Invalid IO type"
        return OM_ERROR_PARAM;
    }

    // 初始化FIFO
    tx_handler = &can->txHandler;
    size_t tx_mail_boxsz = can->cfg.mailboxNum * sizeof(CanMailbox);
    ret = can_fifo_init(can, &tx_handler->txFifo, tx_msg_num, can->cfg.functionalCfg.isTxOverwrite);
    while (ret != OM_OK)
    {
    }; // TODO: ASSERT
    // 初始化Mailbox
    tx_handler->pMailboxes = (CanMailbox*)osal_malloc(tx_mail_boxsz);
    while (tx_handler->pMailboxes == NULL)
    {
    }; // TODO: ASSERT
    memset(tx_handler->pMailboxes, 0, tx_mail_boxsz);

    INIT_LIST_HEAD(&tx_handler->mailboxList);
    for (uint32_t i = 0; i < can->cfg.mailboxNum; i++)
    {
        tx_handler->pMailboxes[i].bank = i;
        dbg_mailbox[i] = &tx_handler->pMailboxes[i];
        INIT_LIST_HEAD(&tx_handler->pMailboxes[i].list);
        list_add_tail(&tx_handler->pMailboxes[i].list, &tx_handler->mailboxList);
    }
    can->hwInterface->control(can, CAN_CMD_SET_IOTYPE, (void *)&iotype);
    return ret;
}

static void can_txhandler_deinit(CanTxHandler* tx_handler)
{
    if (tx_handler->txFifo.msgBuffer != NULL)
    {
        osal_free(tx_handler->txFifo.msgBuffer);
        tx_handler->txFifo.msgBuffer = NULL;
    }
    tx_handler->txFifo.freeCount = 0;
    INIT_LIST_HEAD(&tx_handler->txFifo.freeList);
    INIT_LIST_HEAD(&tx_handler->txFifo.usedList);
}

/*
 * @brief CAN 接收模块反初始化
 * @param RxHandler CAN 接收句柄
 */
static void can_rxhandler_deinit(CanRxHandler* rx_handler)
{
    if (rx_handler->filterTable != NULL)
    {
        osal_free(rx_handler->filterTable);
        rx_handler->filterTable = NULL;
    }
    if (rx_handler->rxFifo.msgBuffer != NULL)
    {
        osal_free(rx_handler->rxFifo.msgBuffer);
        rx_handler->rxFifo.msgBuffer = NULL;
    }
}

/*
 * @brief 将 CAN 消息链表项中的数据拷贝到 CAN 用户消息
 * @param msgList CAN消息链表
 * @param pUserRxMsg CAN用户消息指针
 */
static inline void can_container_copy_to_usermsg(CanMsgList* msg_list, CanUserMsg* p_user_rx_msg)
{
    msg_list->userMsg.userBuf = p_user_rx_msg->userBuf; // 防止框架层的userBuf覆盖原有的用户内存指针
    *p_user_rx_msg = msg_list->userMsg;
    // 拷贝数据到用户缓冲区
    memcpy((void *)p_user_rx_msg->userBuf, (void *)msg_list->container, msg_list->userMsg.dsc.dataLen);
    msg_list->userMsg.userBuf = msg_list->container; // 恢复框架层的userBuf指针
}

/**
 * @brief 从 CAN FIFO 中获取一个空闲链表项
 * @param Fifo CAN FIFO
 * @return 错误
 * @retval 1. CAN_ERR_NONE          成功
 * @retval 2. CAN_ERR_SOFT_FIFO_OVERFLOW 接收FIFO溢出
 *
 * @note 在覆写模式下，返回CAN_ERR_SOFT_FIFO_OVERFLOW时MsgList将指向被取出的链表项
 *       否则MsgList将指向NULL
 * @note 该函数非线程安全，需要调用者自行保护数据安全
 */
static CanErrorCode can_get_free_msg_list(CanMsgFifo* fifo, CanMsgList* *msg_list)
{
    CanErrorCode ret = CAN_ERR_NONE;

    // 如果空闲链表非空，则取出一个链表项
    if (!list_empty(&fifo->freeList))
    {
        *msg_list = list_first_entry(&fifo->freeList, CanMsgList, fifoListNode);
        list_del(&(*msg_list)->fifoListNode); // 将该节点从空闲链表中删除
        fifo->freeCount--;
    }
    // 如果空闲链表为空，则代表链表已满
    // 若开启覆盖模式，则从已用链表取出最旧消息，实现“覆盖旧消息”
    // 若未开启覆盖模式，则返回FIFO溢出错误
    else if (!list_empty(&fifo->usedList) && fifo->isOverwrite)
    {
        ret = CAN_ERR_SOFT_FIFO_OVERFLOW;
        *msg_list = list_first_entry(&fifo->usedList, CanMsgList, fifoListNode);
        list_del(&(*msg_list)->fifoListNode); // 将该节点从已用链表中删除
    }
    else if (!fifo->isOverwrite) // 若未开启覆盖模式，则返回FIFO溢出错误
    {
        *msg_list = NULL;
        ret = CAN_ERR_SOFT_FIFO_OVERFLOW;
    }
    // 理论上空闲链表与已用链表的节点总数应恒定且为正整数
    // 出现两表皆空的情况，只能是缓存区未初始化
    else
    {
        while (1)
        {
        }; // TODO: ASSERT "Receive message buffer is not initialized"
    }
    return ret;
}

/**
 * @brief 将CAN消息链表项添加回CAN接收FIFO的空闲链表中
 * @param RxFifo CAN接收FIFO
 * @param MsgList CAN消息链表
 *
 * @note 该函数非线程安全，需要调用者自行保护数据安全
 */
static inline void can_add_free_msg_list(CanMsgFifo* fifo, CanMsgList* msg_list)
{
    list_add_tail(&msg_list->fifoListNode, &fifo->freeList); // 将该节点添加到空闲链表中
    fifo->freeCount++;
}

/**
 * @brief 将CAN消息链表项添加回CAN发送FIFO的已用链表中
 * @param Fifo CANFIFO
 * @param MsgList CAN消息链表
 *
 * @note 该函数非线程安全，需要调用者自行保护数据安全
 */
static inline void can_add_used_msg_list(CanMsgFifo* fifo, CanMsgList* msg_list)
{
    list_add_tail(&msg_list->fifoListNode, &fifo->usedList); // 将该节点添加到已用链表中
}

/**
 * @brief 从CAN接收FIFO中获取一个空闲链表项
 *
 * @param RxHandler CAN 接收句柄
 * @param MsgList CAN 消息链表项指针
 * @return CanErrorCode 错误
 * @retval 1. CAN_ERR_NONE          成功
 * @retval 2. CAN_ERR_SOFT_FIFO_OVERFLOW 接收FIFO溢出
 */
static CanErrorCode canrx_get_free_msg_list(CanRxHandler* rx_handler, CanMsgList* *pp_msg_list)
{
    uint32_t int_level;
    CanErrorCode ret = CAN_ERR_NONE;
    int_level = can_irq_lock();
    ret = can_get_free_msg_list(&rx_handler->rxFifo, pp_msg_list);
    if (*pp_msg_list == NULL) // 若ppMsgList指向NULL，说明发生了某种错误，直接返回结果
    {
        can_irq_unlock(int_level);
        return ret;
    }
    // 如果该链表项有匹配的滤波器，则将其从滤波器的匹配链表中删除
    if ((*pp_msg_list)->owner != NULL)
    {
        list_del(&(*pp_msg_list)->matchedListNode);
        // 如果是FIFO溢出，说明取得的是还来不及读取的数据，所以其原本匹配滤波器的消息数量需要减一
        if (ret == CAN_ERR_SOFT_FIFO_OVERFLOW)
            ((CanFilter*)(*pp_msg_list)->owner)->msgCount--;
        (*pp_msg_list)->owner = NULL;
    }
    can_irq_unlock(int_level);
    return ret;
}

/**
 * @brief 从CAN发送FIFO中获取一个空闲链表项
 * @param TxHandler CAN 发送句柄
 * @param MsgList CAN 消息链表项指针
 * @return CanErrorCode 错误
 * @retval 1. CAN_ERR_NONE          成功
 * @retval 2. CAN_ERR_SOFT_FIFO_OVERFLOW 发送FIFO溢出
 *
 */
static CanErrorCode cantx_get_free_msg_list(CanTxHandler* tx_handler, CanMsgList* *pp_msg_list)
{
    uint32_t int_level;
    CanErrorCode ret = CAN_ERR_NONE;
    int_level = can_irq_lock();
    ret = can_get_free_msg_list(&tx_handler->txFifo, pp_msg_list);
    if (*pp_msg_list == NULL) // 若ppMsgList指向NULL，说明发生了某种错误，直接返回结果
    {
        can_irq_unlock(int_level);
        return ret;
    }
    while ((*pp_msg_list)->owner != NULL && !tx_handler->txFifo.isOverwrite)
    {
    }; // TODO: 非覆写模式下不应出现“取到正在发送报文”的情况
    // 如果该链表项是正在发送的报文(覆写模式下会触发)，则将其从发送邮箱的匹配链表中删
    // TODO: 这个逻辑可能导致正在发送或等待重传的报文被错误地从匹配链表中删
    if ((*pp_msg_list)->owner != NULL)
    {
        list_del(&(*pp_msg_list)->matchedListNode);
        // 如果是FIFO溢出，说明取得的是还来不及发送的数据，所以其原本匹配发送邮箱的消息需要中断发送
        if (ret == CAN_ERR_SOFT_FIFO_OVERFLOW)
            ((CanMailbox*)(*pp_msg_list)->owner)->isBusy = 0;
        (*pp_msg_list)->owner = NULL;
    }
    // TODO: 未来拓展TxHandler数据结构时，此处可能需要做额外操作
    can_irq_unlock(int_level);
    return ret;
}

static inline void canrx_add_free_msg_list(CanRxHandler* rx_handler, CanMsgList* msg_list)
{
    uint32_t int_level;
    int_level = can_irq_lock();
    can_add_free_msg_list(&rx_handler->rxFifo, msg_list); // 将该节点添加到空闲链表中
    can_irq_unlock(int_level);
}

static inline void cantx_add_free_msg_list(CanTxHandler* tx_handler, CanMailbox* mailbox)
{
    CanMsgList* msg_list;
    uint32_t int_level;

    int_level = can_irq_lock();
    msg_list = mailbox->pMsgList;
    list_del(&msg_list->fifoListNode); // 从已用链表中删除
    list_del(&msg_list->matchedListNode);
    mailbox->isBusy = 0;
    mailbox->pMsgList = NULL;
    msg_list->owner = NULL;
    list_add_tail(&mailbox->list, &tx_handler->mailboxList);
    can_add_free_msg_list(&tx_handler->txFifo, msg_list); // 将该节点添加到空闲链表中
    can_irq_unlock(int_level);
}

/**
 * @brief 将填充好数据的CAN消息链表（已用项）添加回CAN接收FIFO和滤波器匹配链表
 * @param RxHandler CAN 接收句柄
 * @param Filter 接收过滤器
 * @param msgList CAN消息链表
 * @note Filter 合法性由调用者保证，本函数不做检查
 */
static inline void canrx_add_used_msg_list(CanRxHandler* rx_handler, CanFilter* filter, CanMsgList* msg_list)
{
    uint32_t int_level;
    int_level = can_irq_lock();
    list_add_tail(&msg_list->matchedListNode, &filter->msgMatchedList); // 将该节点添加到滤波器匹配链表
    msg_list->owner = filter;                                           // 记录该消息链表项所属滤波器
    filter->msgCount++;                                                 // 增加滤波器匹配消息数
    can_add_used_msg_list(&rx_handler->rxFifo, msg_list);               // 将该节点添加到已用链表中
    can_irq_unlock(int_level);
}

static inline void cantx_add_used_msg_list(CanTxHandler* tx_handler, CanMsgList* p_msg_list)
{
    uint32_t int_level;
    int_level = can_irq_lock();
    can_add_used_msg_list(&tx_handler->txFifo, p_msg_list); // 将该节点添加到已用链表中
    can_irq_unlock(int_level);
}

/**
 * @brief 从CAN接收FIFO中获取一个CAN消息链表项，同时将其从已用链表和滤波器匹配链表中删除
 * @param RxHandler CAN 接收句柄
 * @param Filter 接收滤波
 * @return CAN 消息链表项指针
 * @note Filter 合法性由调用者保证，本函数不做检查
 */
static CanMsgList* canrx_get_used_msg_list(CanRxHandler* rx_handler, CanFilter* filter)
{
    uint32_t int_level;
    CanMsgList* msg_list;
    int_level = can_irq_lock();
    // 如果指定了滤波器，且该滤波器有匹配的消息链表项，则取出第一个匹配项
    if (filter != NULL)
    {
        msg_list = list_first_entry(&filter->msgMatchedList, CanMsgList, matchedListNode);
        filter->msgCount--;
        msg_list->owner = NULL;
    }
    // 如果没有指定滤波器，则从已用链表中取出一个链表项
    else
    {
        msg_list = list_first_entry(&rx_handler->rxFifo.usedList, CanMsgList, fifoListNode);
    }
    list_del(&msg_list->fifoListNode);    // 将该节点从已用链表中删除
    list_del(&msg_list->matchedListNode); // 将该节点从滤波器匹配链表中删除
    can_irq_unlock(int_level);
    return msg_list;
}

static inline CanMsgList* cantx_get_used_msg_list(CanTxHandler* tx_handler)
{
    uint32_t int_level;
    CanMsgList* msg_list;
    int_level = can_irq_lock();
    msg_list = list_first_entry(&tx_handler->txFifo.usedList, CanMsgList, fifoListNode);
    list_del(&msg_list->fifoListNode); // 将该节点从已用链表中删除
    can_irq_unlock(int_level);
    return msg_list;
}

/*
 * @brief 添加接收消息到CAN接收FIFO和滤波器匹配链表
 * @param Can CAN 句柄
 * @param userRxMsg CAN用户接收消息指针
 */
static void canrx_msg_put(HalCanHandler* can, CanMsgList* hw_rx_msg_list)
{
    CanRxHandler* rx_handler;
    CanFilter* filter;
    CanUserMsg* hw_rx_msg;
    CanFilterHandle filter_handle;
    while (!hw_rx_msg_list)
    {
    }; // TODO: assert
    hw_rx_msg = &hw_rx_msg_list->userMsg;
    // 参数检查与初始化
    // TODO: ASSERT "User message is NULL"
    while (!hw_rx_msg->userBuf)
    {
    }; // TODO: assert
    filter_handle = hw_rx_msg->filterHandle;
    // TODO: ASSERT "Filter bank %d is out of range"
    while (IS_CAN_FILTER_INVALID(can, filter_handle))
    {
    };

    filter = NULL;
    rx_handler = &can->rxHandler;
    filter = &rx_handler->filterTable[filter_handle];

    // 将消息链表添加回已用链表和滤波器匹配链表
    canrx_add_used_msg_list(rx_handler, filter, hw_rx_msg_list);

    if (filter != NULL && filter->request.rxCallback != NULL)
    {
        filter->request.rxCallback(&can->parent, filter->request.param, filter_handle, filter->msgCount);
    }
    else
    {
        // 由于当前CAN总线必须要设置一个滤波器，而设置滤波器必须要设置rx_callback，所以大概率不会跳到这里
        // 而且暂时也想不到device模型的read_cb在CAN中有什么价值，所以暂时不处理
        while (1)
        {
        }; // TODO: ASSERT "CAN filter bank %d RX callback is NULL"
        // // size_t msgCount = Can->cfg.rxMsgListBufSize - RxHandler->rxFifo.freeCount;
        // // device_read_cb(&Can->parent, msgCount);
    }
}

/**
 * @brief 从CAN接收FIFO中获取一个CAN消息，非阻塞
 * @param rxHandler CAN接收处理
 * @param pUserRxMsg CAN用户接收消息指针
 * @retval  1. CAN_ERR_NONE           成功
 * @retval  2. CAN_ERR_FIFO_EMPTY     滤波器无可读消息或接FIFO 为空
 */
static CanErrorCode canrx_msg_get_noblock(CanRxHandler* rx_handler, CanUserMsg* p_user_rx_msg)
{
    CanFilter* filter;
    size_t filter_handle;
    uint32_t int_level;
    CanMsgList* msg_list;
    filter_handle = p_user_rx_msg->filterHandle;
    msg_list = NULL;
    filter = NULL;

    int_level = can_irq_lock();
    filter = &rx_handler->filterTable[filter_handle];
    if (filter->msgCount == 0) // 非阻塞，直接退出
    {
        // TODO: ASSERT "Filter bank %d is empty"
        can_irq_unlock(int_level);
        return CAN_ERR_FIFO_EMPTY;
    }
    else if (list_empty(&rx_handler->rxFifo.usedList))
    {
        // TODO: ASSERT "Soft FIFO is empty"
        can_irq_unlock(int_level);
        return CAN_ERR_FIFO_EMPTY;
    }
    can_irq_unlock(int_level);
    // 开始获取报文
    // 从rxFifo中取出一个已用链表项
    msg_list = canrx_get_used_msg_list(rx_handler, filter);
    // 从这里之后，MsgList已经从rxFifo和Filter中被取出
    // 此时除了当前操作之外，已经没有任何代码能访问这个链表项，所以针对MsgList的操作无需考虑竞争

    // 将容器中的报文拷贝到用户接收消息指针
    can_container_copy_to_usermsg(msg_list, p_user_rx_msg);
    // 将该消息链表项添加回空闲链表
    can_add_free_msg_list(&rx_handler->rxFifo, msg_list);
    return CAN_ERR_NONE;
}

/**
 * @brief CAN 发送调度器
 * @param Can CAN 句柄
 * @note 核心逻辑：从待发 FIFO 取数-> 写入空闲硬件邮箱 -> 触发发送
 * @note 该函数可在用户线程触发，也可在 ISR 中调用
 */
static void can_tx_scheduler(HalCanHandler* can)
{
    CanTxHandler* tx_handler = &can->txHandler;
    CanMailbox* mailbox;
    CanMsgList* p_msg_list;
    uint32_t int_level;

    // 进入临界区，保护 FIFO 和 mailbox 状态
    int_level = can_irq_lock();

    if (list_empty(&tx_handler->txFifo.usedList))
    {
        // 没有数据要发了，退出循环
        can_irq_unlock(int_level);
        return;
    }
    // TODO: 完善mailbox机制，使我们能够主动调度mailbox
    // TODO: 当邮箱数量增多时，中断中循环次数可能过多
    // 当前 CAN 负载与硬件邮箱规模可控，暂不做进一步拆分调度
    // 各家CAN IP都不会有太多的邮箱，所以暂时不需要考虑。即便未来要改，由于架构设计得当，改动也会被限制在这个函数中
    while (!list_empty(&tx_handler->mailboxList))
    {
        p_msg_list = list_first_entry(&tx_handler->txFifo.usedList, CanMsgList, fifoListNode);
        CanHwMsg hw_msg = {
            .dsc = p_msg_list->userMsg.dsc,
            .hwFilterBank = -1,
            .hwTxMailbox = -1,
            .data = p_msg_list->userMsg.userBuf,
        };
        OmRet ret = can->hwInterface->sendMsgMailbox(can, &hw_msg);
        if (ret == OM_OK)
        {
            // 发送成功
            while (IS_CAN_MAILBOX_INVALID(can, hw_msg.hwTxMailbox))
            {
            }; // TODO: assert
            mailbox = &tx_handler->pMailboxes[hw_msg.hwTxMailbox];
            // 从等待队列（usedList）中移除消息
            list_del(&p_msg_list->fifoListNode);
            // 从可用邮箱中移除邮箱
            list_del(&mailbox->list);
            // 标记邮箱为空闲
            mailbox->isBusy = 1;
            mailbox->pMsgList = p_msg_list;
            p_msg_list->owner = mailbox;
        }
        else
        {
            // TODO: 进入这里通常说明进入 BUS_OFF 状态或其他未知原因
            // 这里简单处理，直接退出循环
            // TODO: LOG "CAN status: %d，发送失败，bank: %d，msgID: 0x%08x"
            if (ret == OM_ERR_OVERFLOW)
                can->statusManager.errCounter.txMailboxFullCnt++;
            break;
        }
        if (list_empty(&tx_handler->txFifo.usedList))
            break;
    }
    can_irq_unlock(int_level);
}

/**
 * @brief 向CAN发送FIFO中添加一个CAN消息，非阻塞
 *
 * @param Can CAN 句柄
 * @param pUserTxMsgBuf CAN 用户发送消息数组指针
 * @param msgNum 消息数量
 * @return size_t 实际发送的消息数量
 */
static size_t cantx_msg_put_nonblock(HalCanHandler* can, CanUserMsg* p_user_tx_msg_buf, size_t msg_num)
{
    uint32_t int_level;
    size_t msg_counter = 0;
    CanMsgList* p_msg_list = NULL;
    for (msg_counter = 0; msg_counter < msg_num; msg_counter++)
    {
        // 获取一个空闲消息链表项，用于存储信息
        if (cantx_get_free_msg_list(&can->txHandler, &p_msg_list) == CAN_ERR_SOFT_FIFO_OVERFLOW)
        {
            if (!can->txHandler.txFifo.isOverwrite)
            {
                int_level = can_irq_lock();
                can->statusManager.errCounter.txSoftOverFlowCnt += (msg_num - msg_counter);
                can->statusManager.errCounter.txFailCnt += (msg_num - msg_counter); // 被覆盖的消息定义为发送失败的消息
                can_irq_unlock(int_level);
                break; // 跳出循环，结束写入
            }
            else // 覆写
            {
                int_level = can_irq_lock();
                can->statusManager.errCounter.txFailCnt++; // 未被发送的消息定义为发送失败的消息
                can->statusManager.errCounter.txSoftOverFlowCnt++;
                can_irq_unlock(int_level);
            }
        }
        // 从这里之后，pMsgList已经从Fifo中被取出
        // 此时除了当前操作之外，已经没有任何代码能访问这个链表项，所以针对pMsgList的操作无需考虑竞争

        // 填充用户消息指针
        p_msg_list->userMsg = p_user_tx_msg_buf[msg_counter];
        // 填充CAN消息容器
        memcpy((void *)p_msg_list->container, (void *)p_user_tx_msg_buf[msg_counter].userBuf, p_user_tx_msg_buf[msg_counter].dsc.dataLen);
        p_msg_list->userMsg.userBuf = p_msg_list->container;

        int_level = can_irq_lock();
        can_add_used_msg_list(&can->txHandler.txFifo, p_msg_list);
        can_irq_unlock(int_level);
    }
    // 在中断上下文中不需要主动调度
    if (!osal_is_in_isr())
        can_tx_scheduler(can);
    return msg_counter;
}

/**
 * @brief CAN软件重传机制
 *
 * @param Can CAN设备指针
 * @param mailbox 邮箱指针
 */
static void cantx_soft_retransmit(HalCanHandler* can, uint32_t mailbox_bank)
{
    // 相关邮箱与消息节点会先从共享链表摘除，再执行重传流程
    // 因此该流程内对这两类对象的访问不与外部并发冲突（仅限该邮箱与该消息节点）
    CanMailbox* mailbox = &can->txHandler.pMailboxes[mailbox_bank];
    CanMsgList* p_msg_list = mailbox->pMsgList;
    while (!p_msg_list)
    {
    }; // TODO: assert
    p_msg_list->owner = NULL;
    uint32_t int_level;
    int_level = can_irq_lock();
    list_add(&p_msg_list->fifoListNode, &can->txHandler.txFifo.usedList); // 头插，确保先发送的消息优先重发
    can_irq_unlock(int_level);
    mailbox->isBusy = 0;
    mailbox->pMsgList = NULL;
    list_add_tail(&mailbox->list, &can->txHandler.mailboxList);
    can_tx_scheduler(can);
}

/*
 * @brief 初始化CAN设备
 * @param Dev CAN设备指针
 * @retval  1. OM_ERROR_PARAM 参数错误
 *          2. OM_ERROR_HW 硬件错误
 *          3. OM_ERROR_NONE 成功
 */
static OmRet can_init(Device* dev)
{
    OmRet ret;
    if (!dev)
        return OM_ERROR_PARAM;
    HalCanHandler* can = (HalCanHandler*)dev;
    if (!can->adapterInterface || !can->hwInterface)
        return OM_ERROR_PARAM;
    can_filter_resmgr_deinit(can);
    can_rxhandler_deinit(&can->rxHandler);
    can_txhandler_deinit(&can->txHandler);
    ret = can->hwInterface->configure(can, &can->cfg);
    return ret;
}

static OmRet can_open(Device* dev, uint32_t oparam)
{
    OmRet ret;
    uint32_t iotype;
    if (!dev)
        return OM_ERROR_PARAM;
    HalCanHandler* can = (HalCanHandler*)dev;

    // 先初始化过滤器资源管理器，再初始化 RX/TX 子模块
    // 这样后续模块可以直接使用稳定slot <-> hwBank 映射
    ret = can_filter_resmgr_init(can);
    if (ret != OM_OK)
        return ret;
    // filterNum capability 驱动，不再依赖静态配置值
    can->cfg.filterNum = can->filterResMgr.slotCount;

    iotype = oparam & DEVICE_O_RXTYPE_MASK;

    ret = can_rxhandler_init(can, iotype, can->filterResMgr.slotCount, can->cfg.rxMsgListBufSize);
    if (ret != OM_OK)
    {
        // 回滚 filter_resmgr，避免 open 失败后残留资源占用
        can_filter_resmgr_deinit(can);
        return ret;
    }

    iotype = oparam & DEVICE_O_TXTYPE_MASK;
    ret = can_txhandler_init(can, iotype, can->cfg.mailboxNum, can->cfg.txMsgListBufSize);
    if (ret != OM_OK)
    {
        // TODO: LOG ERR
        can_rxhandler_deinit(&can->rxHandler);
        // TX 初始化失败同样需要释放 filter_resmgr
        can_filter_resmgr_deinit(can);
        return ret;
    }
    ret = can_status_manager_init(can);
    if (ret != OM_OK)
    {
        // TODO: LOG ERR
        can_rxhandler_deinit(&can->rxHandler);
        can_txhandler_deinit(&can->txHandler);
        // 状态管理器失败时，保持 open 过程全量回滚
        can_filter_resmgr_deinit(can);
        return ret;
    }
    return ret;
}

static size_t can_write(Device* dev, void *pos, void *data, size_t len)
{
    size_t msg_num = 0;
    HalCanHandler* can = (HalCanHandler*)dev;
    // 目前只支持非阻塞发送策略
    msg_num = cantx_msg_put_nonblock(can, (CanUserMsg*)data, len);
    return msg_num;
}

/**
 * @brief 从 CAN 接收 FIFO 中读取多个 CAN 消息
 *
 * @param Dev CAN设备指针
 * @param pos CAN 不需要该参数，保持NULL即可
 * @param buf 接收消息缓冲区指针
 * @param len 接收消息缓冲区长度
 * @return size_t 实际读取的消息数
 */
static size_t can_read(Device* dev, void *pos, void *buf, size_t len)
{
    size_t cnt = 0;
    HalCanHandler* can = (HalCanHandler*)dev;
    CanRxHandler* rx_handler = &can->rxHandler;
    CanErrorCode ret = CAN_ERR_NONE;
    CanUserMsg* p_user_rx_msg_buf = (CanUserMsg*)buf;

    for (cnt = 0; cnt < len; cnt++)
    {
        while (!p_user_rx_msg_buf[cnt].userBuf)
        {
        }; // TODO: assert
        size_t filter_handle = p_user_rx_msg_buf[cnt].filterHandle;
        while (IS_CAN_FILTER_INVALID(can, filter_handle))
        {
        }; // TODO: ASSERT "Filter bank %d is INVALID"
        while (rx_handler->filterTable[filter_handle].isActived == 0)
        {
        }; // TODO: ASSERT "Filter bank %d is not active"
        ret = canrx_msg_get_noblock(rx_handler, &p_user_rx_msg_buf[cnt]);
        if (ret != CAN_ERR_NONE)
            break;
    }
    if (ret != CAN_ERR_NONE)
    {
        // TODO: LOG ERR
        device_err_cb(&can->parent, ret, cnt);
    }
    return cnt;
}

/**
 * @brief 控制 CAN 设备
 * @param Dev CAN设备指针
 * @param cmd 命令 @ref CAN_CMD_DEF
 * @param args 参数指针
 * @retval  1. OM_ERROR_PARAM 参数错误
 *          2. OM_ERROR_NONE 成功
 */
static OmRet can_ctrl(Device* dev, size_t cmd, void *args)
{
    OmRet ret = OM_OK;
    if (!dev)
        return OM_ERROR_PARAM;
    HalCanHandler* can = (HalCanHandler*)dev;
    switch (cmd)
    {
    case CAN_CMD_SET_IOTYPE:
        ret = can->hwInterface->control(can, CAN_CMD_SET_IOTYPE, args);
        break;

    case CAN_CMD_CLR_IOTYPE:
        ret = can->hwInterface->control(can, CAN_CMD_CLR_IOTYPE, args);
        break;

    // 学习者需注意这里的程序设计，检查参数合法性，并且操作失败后也不会产生副作用（即如果配置失败，CAN设备的状态或数据不会改变），这在架构设计中是必要
    case CAN_CMD_CFG:
        if (!args)
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        ret = can->hwInterface->control(can, CAN_CMD_CFG, args);
        if (ret == OM_OK)
            can->cfg = *(CanCfg*)args;
        break;

    // 学习者需注意这里的程序设计，检查参数合法性，并且操作失败后也不会产生副作用（即如果配置失败，CAN设备的状态或数据不会改变），这在架构设计中是必要
    case CAN_CMD_FILTER_ALLOC: {
        CanFilterAllocArg* alloc_arg = (CanFilterAllocArg*)args;
        if (alloc_arg == NULL || alloc_arg->request.rxCallback == NULL)
        {
            ret = OM_ERROR_PARAM;
            break;
        }

        uint16_t slot = 0;
        int16_t hw_bank = -1;
        // 先在 filter_resmgr 中占slot/bank，再下发硬件配置
        ret = can_reserve_slot(can, &slot, &hw_bank);
        if (ret != OM_OK)
            break;

        CanHwFilterCfg hw_cfg = {
            .bank = (size_t)hw_bank,
            .workMode = alloc_arg->request.workMode,
            .idType = alloc_arg->request.idType,
            .id = alloc_arg->request.id,
            .mask = alloc_arg->request.mask,
        };
        ret = can->hwInterface->control(can, CAN_CMD_FILTER_ALLOC, &hw_cfg);
        if (ret != OM_OK)
        {
            // 硬件配置失败必须回滚资源管理器，避免“逻辑已占硬件未生效”不一致
            can_release_slot(can, slot);
            break;
        }

        CanFilter* filter = &can->rxHandler.filterTable[slot];
        // 硬件配置成功后再发布框架filter 信息
        filter->request = alloc_arg->request;
        filter->isActived = 1;
        filter->msgCount = 0;
        alloc_arg->handle = (CanFilterHandle)slot;
    }
    break;

    case CAN_CMD_FILTER_FREE: {
        if (args == NULL)
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        CanFilterHandle handle = *(CanFilterHandle *)args;
        if (IS_CAN_FILTER_INVALID(can, handle))
        {
            ret = OM_ERROR_PARAM;
            break;
        }

        CanFilter* filter = &can->rxHandler.filterTable[handle];
        if (!filter->isActived || filter->msgCount > 0)
        {
            ret = OM_ERROR_BUSY;
            break;
        }

        int16_t hw_bank = can->filterResMgr.slotToHwBank[handle];
        if (hw_bank < 0)
        {
            ret = OM_ERROR_PARAM;
            break;
        }

        CanHwFilterCfg hw_cfg = {
            .bank = (size_t)hw_bank,
            .workMode = filter->request.workMode,
            .idType = filter->request.idType,
            .id = filter->request.id,
            .mask = filter->request.mask,
        };
        ret = can->hwInterface->control(can, CAN_CMD_FILTER_FREE, &hw_cfg);
        if (ret != OM_OK)
            break;

        // 先清空框架filter，再释放 slot/bank 占用位
        memset(filter, 0, sizeof(CanFilter));
        INIT_LIST_HEAD(&filter->msgMatchedList);
        can_release_slot(can, handle);
    }
    break;

    case CAN_CMD_START:
        ret = can->hwInterface->control(can, CAN_CMD_START, NULL);
        break;

    case CAN_CMD_CLOSE:
        ret = can->hwInterface->control(can, CAN_CMD_CLOSE, NULL);
        break;
    default:
        ret = can->hwInterface->control(can, cmd, args);
        break;
    }
    return ret;
}

static OmRet can_close(Device* dev)
{
    if (!dev)
        return OM_ERROR_PARAM;
    HalCanHandler* can = (HalCanHandler*)dev;
    if (!can->adapterInterface || !can->hwInterface)
        return OM_ERROR_PARAM;
    uint32_t io_type = CAN_REG_INT_TX;
    can->hwInterface->control(can, CAN_CMD_CLR_IOTYPE, &io_type);
    io_type = CAN_REG_INT_RX;
    can->hwInterface->control(can, CAN_CMD_CLR_IOTYPE, &io_type);
    can->hwInterface->control(can, CAN_CMD_CLOSE, NULL);
    can_rxhandler_deinit(&can->rxHandler);
    can_txhandler_deinit(&can->txHandler);
    can_status_manager_deinit(can);
    // close 阶段必须回收 filter_resmgr，避免下open 看到陈旧映射
    can_filter_resmgr_deinit(can);
    return OM_OK;
}

/*
 * @brief CAN错误中断处理函数
 * @param Can CAN设备指针
 * @param errorEvent 错误事件
 * @note 该函数主要更新 CAN 设备状态；更细粒度的恢复策略由状态管理线程处理
 */
void can_error_isr(HalCanHandler* can, uint32_t err_event, size_t param)
{
    switch (err_event)
    {
    case CAN_ERR_EVENT_TX_FAIL:
        can->statusManager.errCounter.txFailCnt++;
        while (IS_CAN_MAILBOX_INVALID(can, param))
        {
        }; // TODO: assert
        cantx_soft_retransmit(can, param);
        break;
    case CAN_ERR_EVENT_ARBITRATION_FAIL: {

        // TODO: LOG
        can->statusManager.errCounter.txArbitrationFailCnt++;
        while (IS_CAN_MAILBOX_INVALID(can, param))
        {
        }; // TODO: assert
        cantx_soft_retransmit(can, param);
    }
    break;
    case CAN_ERR_EVENT_BUS_STATUS: // TODO: 处理总线错误
    {
        size_t can_tx_err_cnt = can->statusManager.errCounter.txErrCnt;
        size_t can_rx_err_cnt = can->statusManager.errCounter.rxErrCnt;
        if (can_rx_err_cnt < 127 && can_tx_err_cnt < 127)
            can->statusManager.nodeErrStatus = CAN_NODE_STATUS_ACTIVE;
        else if (can_rx_err_cnt > 127 || can_tx_err_cnt > 127)
            can->statusManager.nodeErrStatus = CAN_NODE_STATUS_PASSIVE;
        else if (can_tx_err_cnt > 255)
            can->statusManager.nodeErrStatus = CAN_NODE_STATUS_BUSOFF;
        device_err_cb(&can->parent, CAN_ERR_EVENT_BUS_STATUS, can->statusManager.nodeErrStatus);
    }
    break;
    }
}

void hal_can_isr(HalCanHandler* can, CanIsrEvent event, size_t param)
{
    switch (event)
    {
    case CAN_ISR_EVENT_INT_RX_DONE: // 接收完成中断，param是接收消息的FIFO索引
    {
        OmRet ret;
        CanMsgList* msg_list = NULL;
        CanHwMsg hw_msg = {0};
        uint8_t hw_data[64];
        hw_msg.data = hw_data;
        if (canrx_get_free_msg_list(&can->rxHandler, &msg_list) == CAN_ERR_SOFT_FIFO_OVERFLOW)
        {
            can->statusManager.errCounter.rxSoftOverFlowCnt++; // 接收软件 FIFO 溢出计数器增加
            if (!can->rxHandler.rxFifo.isOverwrite)            // 非覆写策略直接退出
            {
                can->hwInterface->recvMsg(can, NULL, param); // 丢弃当前帧（清中断）
                break;
            }
        }
        ret = can->hwInterface->recvMsg(can, &hw_msg, param);
        if (ret == OM_OK)
        {
            int32_t slot = can_find_slot_by_hwbank(can, hw_msg.hwFilterBank);
            if (slot < 0 || IS_CAN_FILTER_INVALID(can, (size_t)slot))
            {
                canrx_add_free_msg_list(&can->rxHandler, msg_list);
                can->statusManager.errCounter.rxFailCnt++;
                break;
            }
            msg_list->userMsg.dsc = hw_msg.dsc;
            msg_list->userMsg.filterHandle = (CanFilterHandle)slot;
            memcpy(msg_list->container, hw_msg.data, hw_msg.dsc.dataLen);
            canrx_msg_put(can, msg_list);
            can->statusManager.errCounter.rxMsgCount++; // 接收消息计数器增加
        }
        else
        {
            can->statusManager.errCounter.rxFailCnt++; // 接收错误计数器增加
        }
    }
    break;
    case CAN_ISR_EVENT_INT_TX_DONE: // 发送完成中断，param 是邮箱编号
        while (IS_CAN_MAILBOX_INVALID(can, (int32_t)param))
        {
        }; // TODO: assert
        CanMailbox* mailbox = &can->txHandler.pMailboxes[param];

        // 资源回收
        uint32_t int_level = can_irq_lock();
        cantx_add_free_msg_list(&can->txHandler, mailbox);
        can->statusManager.errCounter.txMsgCount++; // 发送消息计数器增加
        device_write_cb(&can->parent, param);
        can_tx_scheduler(can);
        can_irq_unlock(int_level);
        break;
    default:
        break;
    }
}

static DevInterface can_dev_interface = {
    .init = can_init,
    .open = can_open,
    .write = can_write,
    .read = can_read,
    .control = can_ctrl,
    .close = can_close,
};

/**
 * @brief CAN 设备注册（兼容经典 CAN / CAN FD）
 * @param can CAN 设备句柄
 * @param name 设备名称
 * @param handle 硬件句柄
 * @param regparams 注册参数 @ref CAN_REG_DEF
 * @return OmRet 注册结果
 * @retval OM_OK 成功
 * @retval OM_ERROR_PARAM 参数错误
 * @retval OM_ERR_CONFLICT 设备名称冲突
 */
OmRet hal_can_register(HalCanHandler* can, char *name, void *handle, uint32_t regparams)
{
    OmRet ret;
    if (!can || !name || !can->adapterInterface)
        return OM_ERROR_PARAM;
    ret = device_register(&can->parent, name, regparams);
    if (ret != OM_OK)
        return ret;

    can->parent.handle = handle;
    can->parent.interface = &can_dev_interface;
    can->cfg = CAN_DEFUALT_CFG;
    return ret;
}

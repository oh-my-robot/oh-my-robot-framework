#include "core/data_struct/bitmap.h"
#include "core/om_def.h"
#include "drivers/peripheral/can/pal_can_dev.h"
#include "osal/osal_core.h"
#include <string.h>

static inline OsalIrqIsrState_t can_irq_lock(void)
{
    if (osal_is_in_isr())
        return osal_irq_lock_from_isr();

    osal_irq_lock_task();
    return (OsalIrqIsrState_t)0u;
}

static inline void can_irq_unlock(OsalIrqIsrState_t state)
{
    if (osal_is_in_isr())
    {
        osal_irq_unlock_from_isr(state);
        return;
    }
    osal_irq_unlock_task();
}

/**
 * @brief 鏍规嵁纭欢婊ゆ尝鍣?bank 鏌ユ壘妗嗘灦灞?filter slot銆?
 * @note 浠呭湪 slot 宸插崰鐢ㄦ椂妫€鏌ユ槧灏勫叧绯伙紝杩斿洖鍊煎彲鐩存帴浣滀负 `filterHandle` 浣跨敤銆?
 */
static int32_t can_find_slot_by_hwbank(HalCanHandler_t can, int32_t hw_bank)
{
    CanFilterResMgr_t mgr = &can->filterResMgr;
    for (uint16_t slot = 0; slot < mgr->slotCount; slot++)
    {
        // 鍙湪鈥滃凡鍗犵敤 slot鈥濅笂鍋氭槧灏勫尮閰嶏紝閬垮厤璇诲彇鍒板巻鍙叉畫鐣欑殑 slotToHwBank 鍊笺€?
        if (!om_bitmap_atomic_test(&mgr->slotUsedMap, slot))
            continue;
        if (mgr->slotToHwBank[slot] == hw_bank)
            return slot;
    }
    return -1;
}

/**
 * @brief 閲婃斁鎸囧畾 slot锛屽苟鍥炴敹瀵瑰簲纭欢 bank 鍗犵敤鐘舵€併€?
 */
static void can_release_slot(HalCanHandler_t can, uint16_t slot)
{
    CanFilterResMgr_t mgr = &can->filterResMgr;
    OsalIrqIsrState_t int_level = can_irq_lock();
    int16_t hw_bank = mgr->slotToHwBank[slot];
    // 閲婃斁椤哄簭锛氬厛鍥炴敹纭欢 bank锛屽啀鍥炴敹 slot锛屾渶鍚庢竻绌烘槧灏勶紝淇濇寔璧勬簮鐘舵€佷竴鑷淬€?
    if (hw_bank >= 0 && hw_bank <= mgr->maxHwBank)
        om_bitmap_atomic_clear(&mgr->hwBankUsedMap, (size_t)hw_bank);
    om_bitmap_atomic_clear(&mgr->slotUsedMap, slot);
    mgr->slotToHwBank[slot] = -1;
    can_irq_unlock(int_level);
}

/**
 * @brief 鍒嗛厤涓€涓?filter slot 鍜屼竴涓彲鐢ㄧ‖浠?bank銆?
 * @note 璇ュ嚱鏁板湪涓寸晫鍖哄唴鍚屾椂鏇存柊 slot/hwBank 涓や釜浣嶅浘锛屼繚璇佸垎閰嶅師瀛愭€с€?
 */
static OmRet_e can_reserve_slot(HalCanHandler_t can, uint16_t *slot_out, int16_t *hw_bank_out)
{
    CanFilterResMgr_t mgr = &can->filterResMgr;
    OsalIrqIsrState_t int_level = can_irq_lock();
    uint16_t slot = (uint16_t)0xFFFFu;
    int16_t hw_bank = -1;

    // 绗竴姝ワ細鍦ㄧ嚎鎬?slot 绌洪棿涓壘涓€涓┖闂查€昏緫鍙ユ焺銆?
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

    // 绗簩姝ワ細鍙湪 capability 缁欏嚭鐨?bank 鍒楄〃涓寫閫夊彲鐢ㄧ‖浠?bank锛?
    // 涓嶅亣璁?bank 杩炵画锛屼篃涓嶄緷璧栧浐瀹氬尯闂淬€?
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

    // 绗笁姝ワ細CAS 鏂瑰紡鍗犵敤 slot锛涘け璐ヨ鏄庡苟鍙戜笅琚叾浠栬矾寰勬姠鍗犮€?
    if (!om_bitmap_atomic_try_set(&mgr->slotUsedMap, slot))
    {
        can_irq_unlock(int_level);
        return OM_ERROR_BUSY;
    }

    // 绗洓姝ワ細鍗犵敤纭欢 bank锛涜嫢澶辫触鍒欏洖婊?slot锛屼繚璇佲€滀笉鍗婃垚鍔熲€濄€?
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
 * @brief 閲婃斁杩囨护鍣ㄨ祫婧愮鐞嗗櫒鍐呴儴鍔ㄦ€佽祫婧愩€?
 */
static void can_filter_resmgr_deinit(HalCanHandler_t can)
{
    CanFilterResMgr_t mgr = &can->filterResMgr;
    // words 鐢?init 闃舵鍔ㄦ€佸垎閰嶏紝杩欓噷缁熶竴鍥炴敹銆?
    if (mgr->slotUsedMap.words != NULL)
        osal_free((void *)mgr->slotUsedMap.words);
    if (mgr->hwBankUsedMap.words != NULL)
        osal_free((void *)mgr->hwBankUsedMap.words);
    if (mgr->hwBankList)
        osal_free(mgr->hwBankList);
    if (mgr->slotToHwBank)
        osal_free(mgr->slotToHwBank);
    // 娓呴浂鍚庡彲瀹夊叏閲嶅鍒濆鍖栵紝閬垮厤鎮寕鎸囬拡銆?
    memset(mgr, 0, sizeof(CanFilterResMgr_s));
}

/**
 * @brief 浠庣‖浠惰兘鍔涘垵濮嬪寲杩囨护鍣ㄨ祫婧愮鐞嗗櫒銆?
 * @note slot 鏁伴噺鐢?BSP 涓婃姤鐨?`hwBankCount` 鍐冲畾锛宻lot 涓?hw bank 閫氳繃 `slotToHwBank` 寤虹珛鏄犲皠銆?
 */
static OmRet_e can_filter_resmgr_init(HalCanHandler_t can)
{
    CanFilterResMgr_t mgr = &can->filterResMgr;
    CanHwCapability_s capability = {0};
    OmRet_e ret = can->hwInterface->control(can, CAN_CMD_GET_CAPABILITY, &capability);
    if (ret != OM_OK || capability.hwBankCount == 0 || capability.hwBankList == NULL)
        return OM_ERROR_PARAM;

    // 鏀寔閲嶅 open/init锛氬厛娓呯悊鏃ц祫婧愶紝鍐嶆寜鏈€鏂?capability 閲嶅缓銆?
    can_filter_resmgr_deinit(can);

    mgr->slotCount = capability.hwBankCount;
    mgr->hwBankList = (uint8_t *)osal_malloc(mgr->slotCount);
    mgr->slotToHwBank = (int16_t *)osal_malloc(sizeof(int16_t) * mgr->slotCount);
    if (mgr->hwBankList == NULL || mgr->slotToHwBank == NULL)
    {
        can_filter_resmgr_deinit(can);
        return OM_ERROR_MEMORY;
    }
    // 鎷疯礉 bank 鍒楄〃锛岄伩鍏嶇洿鎺ヤ緷璧?BSP 渚у唴瀛樼敓鍛藉懆鏈熴€?
    memcpy(mgr->hwBankList, capability.hwBankList, mgr->slotCount);

    mgr->maxHwBank = 0;
    for (uint16_t i = 0; i < mgr->slotCount; i++)
    {
        // -1 琛ㄧず璇?slot 褰撳墠鏈粦瀹氫换浣曠‖浠?bank銆?
        mgr->slotToHwBank[i] = -1;
        if (mgr->hwBankList[i] > mgr->maxHwBank)
            mgr->maxHwBank = mgr->hwBankList[i];
    }

    // slot 浣嶅浘鎸夐€昏緫鍙ユ焺鏁伴噺鍒嗛厤锛?
    // hwBank 浣嶅浘鎸夆€滄渶澶?bank 涓嬫爣 + 1鈥濆垎閰嶏紝鏀寔绂绘暎 bank 缂栧彿銆?
    size_t hw_bank_bit_count = (size_t)mgr->maxHwBank + 1u;
    om_atomic_ulong_t *slot_words = om_bitmap_atomic_buffer_alloc((size_t)mgr->slotCount, osal_malloc);
    om_atomic_ulong_t *hw_bank_words = om_bitmap_atomic_buffer_alloc(hw_bank_bit_count, osal_malloc);
    if (slot_words == NULL || hw_bank_words == NULL)
    {
        if (slot_words != NULL)
            om_bitmap_buffer_free(slot_words, osal_free);
        if (hw_bank_words != NULL)
            om_bitmap_buffer_free(hw_bank_words, osal_free);
        can_filter_resmgr_deinit(can);
        return OM_ERROR_MEMORY;
    }

    // 鍒濆鍖栧悗涓ゅ紶浣嶅浘鍧囦负绌洪棽鐘舵€侊紙鍏?0锛夈€?
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

static void can_status_timer_cb(OsalTimer_t x_timer)
{
    HalCanHandler_t can = (HalCanHandler_t)osal_timer_get_id(x_timer);
    CanStatusManager_t status_manager = &can->statusManager;
    // 妫€鏌?CAN 鐘舵€?
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
 * @brief 鍒濆鍖朇AN鎺ユ敹FIFO
 * @param Can CAN鍙ユ焺
 * @param msgNum 鎺ユ敹娑堟伅缂撳瓨鏁伴噺
 * @retval  閿欒鐮侊紙OmRet_e锛夋弿杩?
 *          OM_OK             鎴愬姛
 *          OM_ERROR_MEMORY   鍐呭瓨鍒嗛厤澶辫触
 */
static OmRet_e can_fifo_init(HalCanHandler_t can, CanMsgFifo_t can_fifo, uint32_t msg_num, uint8_t is_over_write)
{
    OmRet_e ret = OM_OK;
    INIT_LIST_HEAD(&can_fifo->freeList);
    can_fifo->msgBuffer = can->adapterInterface->msgbufferAlloc(&can_fifo->freeList, msg_num);
    while (can_fifo->msgBuffer == NULL)
    {
    };
    // 鍒濆鍖栭摼琛?
    INIT_LIST_HEAD(&can_fifo->usedList);
    can_fifo->freeCount = msg_num;
    can_fifo->isOverwrite = is_over_write;
    return ret;
}

/*
 * @brief 鍒濆鍖朇AN鎺ユ敹杩囨护鍣?
 * @param RxHandler CAN鎺ユ敹鍙ユ焺
 * @param filterNum 鎺ユ敹杩囨护鍣ㄦ暟閲?
 * @retval  閿欒鐮侊紙OmRet_e锛夋弿杩?
 *          OM_OK             鎴愬姛
 *          OM_ERROR_MEMORY   鍐呭瓨鍒嗛厤澶辫触
 */
static OmRet_e can_filter_init(CanRxHandler_t rx_handler, uint32_t filter_num)
{
    uint32_t filter_sz;
    if (filter_num > 0)
    {
        filter_sz = filter_num * sizeof(CanFilter_s);
        rx_handler->filterTable = (CanFilter_t)osal_malloc(filter_sz);
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
 * @brief 鍒濆鍖朇AN鎺ユ敹鍙ユ焺
 * @param Can CAN鍙ユ焺
 * @param filterNum 鎺ユ敹杩囨护鍣ㄦ暟閲?
 * @param msgNum 鎺ユ敹娑堟伅缂撳瓨鏁伴噺
 * @retval  閿欒鐮侊紙OmRet_e锛夋弿杩?
 *          OM_OK             鎴愬姛
 *          OM_ERROR_BUSY     鍙ユ焺宸插垵濮嬪寲
 *          OM_ERROR_MEMORY   鍐呭瓨鍒嗛厤澶辫触
 */
static OmRet_e can_rxhandler_init(HalCanHandler_t can, uint32_t iotype, uint32_t filter_num, uint32_t msg_num)
{
    CanRxHandler_t rx_handler;
    uint32_t reg_io_type;
    uint32_t is_oparam_valid;
    OmRet_e ret = OM_OK;
    // 鑷冲皯鍒濆鍖栦竴涓护娉㈠櫒
    while (filter_num <= 0 || msg_num <= 0 || !can->adapterInterface->msgbufferAlloc)
    {
    }; // TODO: ASSERT

    // 妫€鏌O绫诲瀷鏄惁鏈夋晥
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

static OmRet_e can_status_manager_init(HalCanHandler_t can)
{
    if (can->statusManager.statusTimer != NULL)
        return OM_ERROR_BUSY;
    CanStatusManager_t status_manager;
    char *name = device_get_name(&can->parent);
    OsalStatus_t osal_status;
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

static void can_status_manager_deinit(HalCanHandler_t can)
{
    CanStatusManager_t status_manager = &can->statusManager;
    if (status_manager->statusTimer != NULL)
    {
        osal_timer_delete(status_manager->statusTimer, can->cfg.statusCheckTimeout);
        status_manager->statusTimer = NULL;
    }
}

DBG_PARAM_DEF(CanMailbox_t, dbg_mailbox[3]) = {0};

static OmRet_e can_txhandler_init(HalCanHandler_t can, uint32_t iotype, size_t mailbox_num, uint32_t tx_msg_num)
{
    CanTxHandler_t tx_handler;
    uint32_t reg_io_type;
    uint32_t is_oparam_valid;
    OmRet_e ret = OM_OK;
    while (mailbox_num <= 0 || tx_msg_num <= 0)
    {
    }; // TODO: ASSERT

    // 妫€鏌O绫诲瀷鏄惁鏈夋晥
    reg_io_type = device_get_regparams(&can->parent) & DEVICE_REG_TXTYPE_MASK;
    is_oparam_valid = (reg_io_type & iotype);
    if (!is_oparam_valid)
    {
        // TODO: ASSERT "Invalid IO type"
        return OM_ERROR_PARAM;
    }

    // 鍒濆鍖朏IFO
    tx_handler = &can->txHandler;
    size_t tx_mail_boxsz = can->cfg.mailboxNum * sizeof(CanMailbox_s);
    ret = can_fifo_init(can, &tx_handler->txFifo, tx_msg_num, can->cfg.functionalCfg.isTxOverwrite);
    while (ret != OM_OK)
    {
    }; // TODO: ASSERT
    // 鍒濆鍖朚ailbox
    tx_handler->pMailboxes = (CanMailbox_t)osal_malloc(tx_mail_boxsz);
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

static void can_txhandler_deinit(CanTxHandler_t tx_handler)
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
 * @brief CAN 鎺ユ敹妯″潡鍙嶅垵濮嬪寲
 * @param RxHandler CAN鎺ユ敹鍙ユ焺
 */
static void can_rxhandler_deinit(CanRxHandler_t rx_handler)
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
 * @brief 灏咰AN娑堟伅閾捐〃椤逛腑鐨勬暟鎹嫹璐濆埌CAN鐢ㄦ埛娑堟伅
 * @param msgList CAN娑堟伅閾捐〃
 * @param pUserRxMsg CAN鐢ㄦ埛娑堟伅鎸囬拡
 */
static inline void can_container_copy_to_usermsg(CanMsgList_t msg_list, CanUserMsg_t p_user_rx_msg)
{
    msg_list->userMsg.userBuf = p_user_rx_msg->userBuf; // 闃叉妗嗘灦灞傜殑userBuf瑕嗙洊鍘熸湁鐨勭敤鎴峰唴瀛樻寚閽?
    *p_user_rx_msg = msg_list->userMsg;
    // 鎷疯礉鏁版嵁鍒扮敤鎴风紦鍐插尯
    memcpy((void *)p_user_rx_msg->userBuf, (void *)msg_list->container, msg_list->userMsg.dsc.dataLen);
    msg_list->userMsg.userBuf = msg_list->container; // 鎭㈠妗嗘灦灞傜殑userBuf鎸囬拡
}

/**
 * @brief 浠嶤AN FIFO涓幏鍙栦竴涓┖闂查摼琛ㄩ」
 * @param Fifo CAN FIFO
 * @return 閿欒
 * @retval 1. CAN_ERR_NONE          鎴愬姛
 * @retval 2. CAN_ERR_SOFT_FIFO_OVERFLOW 鎺ユ敹FIFO婧㈠嚭
 *
 * @note 鍦ㄨ鍐欐ā寮忎笅锛岃繑鍥濩AN_ERR_SOFT_FIFO_OVERFLOW鏃禡sgList灏嗘寚鍚戣鍙栧嚭鐨勯摼琛ㄩ」
 *       鍚﹀垯MsgList灏嗘寚鍚慛ULL
 * @note 璇ュ嚱鏁伴潪绾跨▼瀹夊叏锛岄渶瑕佽皟鐢ㄨ€呰嚜琛屼繚鎶ゆ暟鎹畨鍏ㄣ€?
 */
static CanErrorCode_e can_get_free_msg_list(CanMsgFifo_t fifo, CanMsgList_t *msg_list)
{
    CanErrorCode_e ret = CAN_ERR_NONE;

    // 濡傛灉绌洪棽閾捐〃闈炵┖锛屽垯鍙栧嚭涓€涓摼琛ㄩ」
    if (!list_empty(&fifo->freeList))
    {
        *msg_list = list_first_entry(&fifo->freeList, CanMsgList_s, fifoListNode);
        list_del(&(*msg_list)->fifoListNode); // 灏嗚鑺傜偣浠庣┖闂查摼琛ㄤ腑鍒犻櫎
        fifo->freeCount--;
    }
    // 濡傛灉绌洪棽閾捐〃绌猴紝鍒欎唬琛ㄩ摼琛ㄥ凡婊?
    // 鑻ュ紑鍚鐩栨ā寮忥紝鍒欎粠宸茬敤閾捐〃鍙栧嚭鏈€鏃ф秷鎭紝瀹炵幇鈥滆鐩栨棫娑堟伅鈥濄€?
    // 鑻ユ湭寮€鍚鐩栨ā寮忥紝鍒欒繑鍥濬IFO婧㈠嚭閿欒
    else if (!list_empty(&fifo->usedList) && fifo->isOverwrite)
    {
        ret = CAN_ERR_SOFT_FIFO_OVERFLOW;
        *msg_list = list_first_entry(&fifo->usedList, CanMsgList_s, fifoListNode);
        list_del(&(*msg_list)->fifoListNode); // 灏嗚鑺傜偣浠庡凡鐢ㄩ摼琛ㄤ腑鍒犻櫎
    }
    else if (!fifo->isOverwrite) // 鑻ユ湭寮€鍚鐩栨ā寮忥紝鍒欒繑鍥濬IFO婧㈠嚭閿欒
    {
        *msg_list = NULL;
        ret = CAN_ERR_SOFT_FIFO_OVERFLOW;
    }
    // 鐞嗚涓婄┖闂查摼琛ㄤ笌宸茬敤閾捐〃鐨勮妭鐐规€绘暟搴旀亽瀹氫笖涓烘鏁存暟銆?
    // 鍑虹幇涓よ〃鐨嗙┖鐨勬儏鍐碉紝鍙兘鏄紦瀛樺尯鏈垵濮嬪寲
    else
    {
        while (1)
        {
        }; // TODO: ASSERT "Receive message buffer is not initialized"
    }
    return ret;
}

/**
 * @brief 灏咰AN娑堟伅閾捐〃椤规坊鍔犲洖CAN鎺ユ敹FIFO鐨勭┖闂查摼琛ㄤ腑
 * @param RxFifo CAN鎺ユ敹FIFO
 * @param MsgList CAN娑堟伅閾捐〃
 *
 * @note 璇ュ嚱鏁伴潪绾跨▼瀹夊叏锛岄渶瑕佽皟鐢ㄨ€呰嚜琛屼繚鎶ゆ暟鎹畨鍏ㄣ€?
 */
static inline void can_add_free_msg_list(CanMsgFifo_t fifo, CanMsgList_t msg_list)
{
    list_add_tail(&msg_list->fifoListNode, &fifo->freeList); // 灏嗚鑺傜偣娣诲姞鍒扮┖闂查摼琛ㄤ腑
    fifo->freeCount++;
}

/**
 * @brief 灏咰AN娑堟伅閾捐〃椤规坊鍔犲洖CAN鍙戦€丗IFO鐨勫凡鐢ㄩ摼琛ㄤ腑
 * @param Fifo CANFIFO
 * @param MsgList CAN娑堟伅閾捐〃
 *
 * @note 璇ュ嚱鏁伴潪绾跨▼瀹夊叏锛岄渶瑕佽皟鐢ㄨ€呰嚜琛屼繚鎶ゆ暟鎹畨鍏ㄣ€?
 */
static inline void can_add_used_msg_list(CanMsgFifo_t fifo, CanMsgList_t msg_list)
{
    list_add_tail(&msg_list->fifoListNode, &fifo->usedList); // 灏嗚鑺傜偣娣诲姞鍒板凡鐢ㄩ摼琛ㄤ腑
}

/**
 * @brief 浠嶤AN鎺ユ敹FIFO涓幏鍙栦竴涓┖闂查摼琛ㄩ」
 *
 * @param RxHandler CAN鎺ユ敹鍙ユ焺
 * @param MsgList CAN娑堟伅閾捐〃椤规寚閽?
 * @return CanErrorCode_e 閿欒
 * @retval 1. CAN_ERR_NONE          鎴愬姛
 * @retval 2. CAN_ERR_SOFT_FIFO_OVERFLOW 鎺ユ敹FIFO婧㈠嚭
 */
static CanErrorCode_e canrx_get_free_msg_list(CanRxHandler_t rx_handler, CanMsgList_t *pp_msg_list)
{
    uint32_t int_level;
    CanErrorCode_e ret = CAN_ERR_NONE;
    int_level = can_irq_lock();
    ret = can_get_free_msg_list(&rx_handler->rxFifo, pp_msg_list);
    if (*pp_msg_list == NULL) // 鑻pMsgList鎸囧悜NULL锛岃鏄庡彂鐢熶簡鏌愮閿欒锛岀洿鎺ヨ繑鍥炵粨鏋?
    {
        can_irq_unlock(int_level);
        return ret;
    }
    // 濡傛灉璇ラ摼琛ㄩ」鏈夊尮閰嶇殑婊ゆ尝鍣紝鍒欏皢鍏朵粠婊ゆ尝鍣ㄧ殑鍖归厤閾捐〃涓垹闄?
    if ((*pp_msg_list)->owner != NULL)
    {
        list_del(&(*pp_msg_list)->matchedListNode);
        // 濡傛灉鏄疐IFO婧㈠嚭锛岃鏄庡彇寰楃殑鏄繕鏉ヤ笉鍙婅鍙栫殑鏁版嵁锛屾墍浠ュ叾鍘熸湰鍖归厤婊ゆ尝鍣ㄧ殑娑堟伅鏁伴噺闇€瑕佸噺涓€
        if (ret == CAN_ERR_SOFT_FIFO_OVERFLOW)
            ((CanFilter_t)(*pp_msg_list)->owner)->msgCount--;
        (*pp_msg_list)->owner = NULL;
    }
    can_irq_unlock(int_level);
    return ret;
}

/**
 * @brief 浠嶤AN鍙戦€丗IFO涓幏鍙栦竴涓┖闂查摼琛ㄩ」
 * @param TxHandler CAN鍙戦€佸彞鏌?
 * @param MsgList CAN娑堟伅閾捐〃椤规寚閽?
 * @return CanErrorCode_e 閿欒
 * @retval 1. CAN_ERR_NONE          鎴愬姛
 * @retval 2. CAN_ERR_SOFT_FIFO_OVERFLOW 鍙戦€丗IFO婧㈠嚭
 *
 */
static CanErrorCode_e cantx_get_free_msg_list(CanTxHandler_t tx_handler, CanMsgList_t *pp_msg_list)
{
    uint32_t int_level;
    CanErrorCode_e ret = CAN_ERR_NONE;
    int_level = can_irq_lock();
    ret = can_get_free_msg_list(&tx_handler->txFifo, pp_msg_list);
    if (*pp_msg_list == NULL) // 鑻pMsgList鎸囧悜NULL锛岃鏄庡彂鐢熶簡鏌愮閿欒锛岀洿鎺ヨ繑鍥炵粨鏋?
    {
        can_irq_unlock(int_level);
        return ret;
    }
    while ((*pp_msg_list)->owner != NULL && !tx_handler->txFifo.isOverwrite)
    {
    }; // TODO: 闈炶鍐欐ā寮忎笅涓嶅簲鍑虹幇鈥滃彇鍒版鍦ㄥ彂閫佹姤鏂団€濈殑鎯呭喌
    // 濡傛灉璇ラ摼琛ㄩ」鏄鍦ㄥ彂閫佺殑鎶ユ枃(瑕嗗啓妯″紡涓嬩細瑙﹀彂)锛屽垯灏嗗叾浠庡彂閫侀偖绠辩殑鍖归厤閾捐〃涓垹闄?
    // TODO: 杩欎釜閫昏緫鍙兘瀵艰嚧姝ｅ湪鍙戦€佹垨绛夊緟閲嶄紶鐨勬姤鏂囪閿欒鍦颁粠鍖归厤閾捐〃涓垹闄?
    if ((*pp_msg_list)->owner != NULL)
    {
        list_del(&(*pp_msg_list)->matchedListNode);
        // 濡傛灉鏄疐IFO婧㈠嚭锛岃鏄庡彇寰楃殑鏄繕鏉ヤ笉鍙婂彂閫佺殑鏁版嵁锛屾墍浠ュ叾鍘熸湰鍖归厤鍙戦€侀偖绠辩殑娑堟伅闇€瑕佷腑鏂彂閫?
        if (ret == CAN_ERR_SOFT_FIFO_OVERFLOW)
            ((CanMailbox_t)(*pp_msg_list)->owner)->isBusy = 0;
        (*pp_msg_list)->owner = NULL;
    }
    // TODO: 鏈潵鎷撳睍TxHandler鏁版嵁缁撴瀯鏃讹紝姝ゅ鍙兘闇€瑕佸仛棰濆鎿嶄綔
    can_irq_unlock(int_level);
    return ret;
}

static inline void canrx_add_free_msg_list(CanRxHandler_t rx_handler, CanMsgList_t msg_list)
{
    uint32_t int_level;
    int_level = can_irq_lock();
    can_add_free_msg_list(&rx_handler->rxFifo, msg_list); // 灏嗚鑺傜偣娣诲姞鍒扮┖闂查摼琛ㄤ腑
    can_irq_unlock(int_level);
}

static inline void cantx_add_free_msg_list(CanTxHandler_t tx_handler, CanMailbox_t mailbox)
{
    CanMsgList_t msg_list;
    uint32_t int_level;

    int_level = can_irq_lock();
    msg_list = mailbox->pMsgList;
    list_del(&msg_list->fifoListNode); // 浠庡凡鐢ㄩ摼琛ㄤ腑鍒犻櫎
    list_del(&msg_list->matchedListNode);
    mailbox->isBusy = 0;
    mailbox->pMsgList = NULL;
    msg_list->owner = NULL;
    list_add_tail(&mailbox->list, &tx_handler->mailboxList);
    can_add_free_msg_list(&tx_handler->txFifo, msg_list); // 灏嗚鑺傜偣娣诲姞鍒扮┖闂查摼琛ㄤ腑
    can_irq_unlock(int_level);
}

/**
 * @brief 灏嗗～鍏呭ソ鏁版嵁鐨凜AN娑堟伅閾捐〃锛堝凡鐢ㄩ」锛夋坊鍔犲洖CAN鎺ユ敹FIFO鍜屾护娉㈠櫒鍖归厤閾捐〃
 * @param RxHandler CAN鎺ユ敹鍙ユ焺
 * @param Filter 鎺ユ敹杩囨护鍣?
 * @param msgList CAN娑堟伅閾捐〃
 * @note Filter 鍚堟硶鎬х敱璋冪敤鑰呬繚璇侊紝鏈嚱鏁颁笉鍋氭鏌ャ€?
 */
static inline void canrx_add_used_msg_list(CanRxHandler_t rx_handler, CanFilter_t filter, CanMsgList_t msg_list)
{
    uint32_t int_level;
    int_level = can_irq_lock();
    list_add_tail(&msg_list->matchedListNode, &filter->msgMatchedList); // 灏嗚鑺傜偣娣诲姞鍒版护娉㈠櫒鍖归厤閾捐〃
    msg_list->owner = filter;                                           // 璁板綍璇ユ秷鎭摼琛ㄩ」鎵€灞炴护娉㈠櫒
    filter->msgCount++;                                                 // 澧炲姞婊ゆ尝鍣ㄥ尮閰嶆秷鎭暟閲?
    can_add_used_msg_list(&rx_handler->rxFifo, msg_list);               // 灏嗚鑺傜偣娣诲姞鍒板凡鐢ㄩ摼琛ㄤ腑
    can_irq_unlock(int_level);
}

static inline void cantx_add_used_msg_list(CanTxHandler_t tx_handler, CanMsgList_t p_msg_list)
{
    uint32_t int_level;
    int_level = can_irq_lock();
    can_add_used_msg_list(&tx_handler->txFifo, p_msg_list); // 灏嗚鑺傜偣娣诲姞鍒板凡鐢ㄩ摼琛ㄤ腑
    can_irq_unlock(int_level);
}

/**
 * @brief 浠嶤AN鎺ユ敹FIFO涓幏鍙栦竴涓狢AN娑堟伅閾捐〃椤癸紝鍚屾椂灏嗗叾浠庡凡鐢ㄩ摼琛ㄥ拰婊ゆ尝鍣ㄥ尮閰嶉摼琛ㄤ腑鍒犻櫎
 * @param RxHandler CAN鎺ユ敹鍙ユ焺
 * @param Filter 鎺ユ敹婊ゆ尝鍣?
 * @return CAN娑堟伅閾捐〃椤规寚閽?
 * @note Filter 鍚堟硶鎬х敱璋冪敤鑰呬繚璇侊紝鏈嚱鏁颁笉鍋氭鏌ャ€?
 */
static CanMsgList_t canrx_get_used_msg_list(CanRxHandler_t rx_handler, CanFilter_t filter)
{
    uint32_t int_level;
    CanMsgList_t msg_list;
    int_level = can_irq_lock();
    // 濡傛灉鎸囧畾浜嗘护娉㈠櫒锛屼笖璇ユ护娉㈠櫒鏈夊尮閰嶇殑娑堟伅閾捐〃椤癸紝鍒欏彇鍑虹涓€涓尮閰嶉」
    if (filter != NULL)
    {
        msg_list = list_first_entry(&filter->msgMatchedList, CanMsgList_s, matchedListNode);
        filter->msgCount--;
        msg_list->owner = NULL;
    }
    // 濡傛灉娌℃湁鎸囧畾婊ゆ尝鍣紝鍒欎粠宸茬敤閾捐〃涓彇鍑轰竴涓摼琛ㄩ」
    else
    {
        msg_list = list_first_entry(&rx_handler->rxFifo.usedList, CanMsgList_s, fifoListNode);
    }
    list_del(&msg_list->fifoListNode);    // 灏嗚鑺傜偣浠庡凡鐢ㄩ摼琛ㄤ腑鍒犻櫎
    list_del(&msg_list->matchedListNode); // 灏嗚鑺傜偣浠庢护娉㈠櫒鍖归厤閾捐〃涓垹闄?
    can_irq_unlock(int_level);
    return msg_list;
}

static inline CanMsgList_t cantx_get_used_msg_list(CanTxHandler_t tx_handler)
{
    uint32_t int_level;
    CanMsgList_t msg_list;
    int_level = can_irq_lock();
    msg_list = list_first_entry(&tx_handler->txFifo.usedList, CanMsgList_s, fifoListNode);
    list_del(&msg_list->fifoListNode); // 灏嗚鑺傜偣浠庡凡鐢ㄩ摼琛ㄤ腑鍒犻櫎
    can_irq_unlock(int_level);
    return msg_list;
}

/*
 * @brief 娣诲姞鎺ユ敹娑堟伅鍒癈AN鎺ユ敹FIFO鍜屾护娉㈠櫒鍖归厤閾捐〃
 * @param Can CAN鍙ユ焺
 * @param userRxMsg CAN鐢ㄦ埛鎺ユ敹娑堟伅鎸囬拡
 */
static void canrx_msg_put(HalCanHandler_t can, CanMsgList_t hw_rx_msg_list)
{
    CanRxHandler_t rx_handler;
    CanFilter_t filter;
    CanUserMsg_t hw_rx_msg;
    CanFilterHandle_t filter_handle;
    while (!hw_rx_msg_list)
    {
    }; // TODO: assert
    hw_rx_msg = &hw_rx_msg_list->userMsg;
    // 鍙傛暟妫€鏌ヤ笌鍒濆鍖?
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

    // 灏嗘秷鎭摼琛ㄦ坊鍔犲洖宸茬敤閾捐〃鍜屾护娉㈠櫒鍖归厤閾捐〃
    canrx_add_used_msg_list(rx_handler, filter, hw_rx_msg_list);

    if (filter != NULL && filter->request.rxCallback != NULL)
    {
        filter->request.rxCallback(&can->parent, filter->request.param, filter_handle, filter->msgCount);
    }
    else
    {
        // 鐢变簬褰撳墠CAN鎬荤嚎蹇呴』瑕佽缃竴涓护娉㈠櫒锛岃€岃缃护娉㈠櫒蹇呴』瑕佽缃畆x_callback锛屾墍浠ュぇ姒傜巼涓嶄細璺冲埌杩欓噷
        // 鑰屼笖鏆傛椂涔熸兂涓嶅埌device妯″瀷鐨剅ead_cb鍦–AN涓湁浠€涔堜环鍊硷紝鎵€浠ユ殏鏃朵笉澶勭悊
        while (1)
        {
        }; // TODO: ASSERT "CAN filter bank %d RX callback is NULL"
        // // size_t msgCount = Can->cfg.rxMsgListBufSize - RxHandler->rxFifo.freeCount;
        // // device_read_cb(&Can->parent, msgCount);
    }
}

/**
 * @brief 浠嶤AN鎺ユ敹FIFO涓幏鍙栦竴涓狢AN娑堟伅锛岄潪闃诲
 * @param rxHandler CAN鎺ユ敹澶勭悊鍣?
 * @param pUserRxMsg CAN鐢ㄦ埛鎺ユ敹娑堟伅鎸囬拡
 * @retval  1. CAN_ERR_NONE           鎴愬姛
 * @retval  2. CAN_ERR_FIFO_EMPTY     婊ゆ尝鍣ㄦ棤鍙娑堟伅鎴栨帴鏀?FIFO 涓虹┖
 */
static CanErrorCode_e canrx_msg_get_noblock(CanRxHandler_t rx_handler, CanUserMsg_t p_user_rx_msg)
{
    CanFilter_t filter;
    size_t filter_handle;
    uint32_t int_level;
    CanMsgList_t msg_list;
    filter_handle = p_user_rx_msg->filterHandle;
    msg_list = NULL;
    filter = NULL;

    int_level = can_irq_lock();
    filter = &rx_handler->filterTable[filter_handle];
    if (filter->msgCount == 0) // 闈為樆濉烇紝鐩存帴閫€鍑?
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
    // 寮€濮嬭幏鍙栨姤鏂?
    // 浠巖xFifo涓彇鍑轰竴涓凡鐢ㄩ摼琛ㄩ」
    msg_list = canrx_get_used_msg_list(rx_handler, filter);
    // 浠庤繖閲屼箣鍚庯紝MsgList宸茬粡浠巖xFifo鍜孎ilter涓鍙栧嚭
    // 姝ゆ椂闄や簡褰撳墠鎿嶄綔涔嬪锛屽凡缁忔病鏈変换浣曚唬鐮佽兘璁块棶杩欎釜閾捐〃椤癸紝鎵€浠ラ拡瀵筂sgList鐨勬搷浣滄棤闇€鑰冭檻绔炴€?

    // 灏嗗鍣ㄤ腑鐨勬姤鏂囨嫹璐濆埌鐢ㄦ埛鎺ユ敹娑堟伅鎸囬拡
    can_container_copy_to_usermsg(msg_list, p_user_rx_msg);
    // 灏嗚娑堟伅閾捐〃椤规坊鍔犲洖绌洪棽閾捐〃
    can_add_free_msg_list(&rx_handler->rxFifo, msg_list);
    return CAN_ERR_NONE;
}

/**
 * @brief CAN 鍙戦€佽皟搴﹀櫒
 * @param Can CAN鍙ユ焺
 * @note 鏍稿績閫昏緫锛氫粠寰呭彂 FIFO 鍙栨暟鎹?-> 鍐欏叆绌洪棽纭欢閭 -> 瑙﹀彂鍙戦€併€?
 * @note 璇ュ嚱鏁板彲鍦ㄧ敤鎴风嚎绋嬭Е鍙戯紝涔熷彲鍦?ISR 涓皟鐢ㄣ€?
 */
static void can_tx_scheduler(HalCanHandler_t can)
{
    CanTxHandler_t tx_handler = &can->txHandler;
    CanMailbox_t mailbox;
    CanMsgList_t p_msg_list;
    uint32_t int_level;

    // 杩涘叆涓寸晫鍖猴紝淇濇姢 FIFO 涓?mailbox 鐘舵€?
    int_level = can_irq_lock();

    if (list_empty(&tx_handler->txFifo.usedList))
    {
        // 娌℃湁鏁版嵁瑕佸彂浜嗭紝閫€鍑哄惊鐜?
        can_irq_unlock(int_level);
        return;
    }
    // TODO: 瀹屽杽mailbox鏈哄埗锛屼娇鎴戜滑鑳藉涓诲姩璋冨害mailbox
    // TODO: 褰撻偖绠辨暟閲忓澶氭椂锛屼腑鏂腑寰幆娆℃暟鍙兘杩囧锛?
    // 褰撳墠 CAN 璐熻浇涓庣‖浠堕偖绠辫妯″彲鎺э紝鏆備笉鍋氳繘涓€姝ユ媶鍒嗚皟搴︺€?
    // 鍚勫CAN IP閮戒笉浼氭湁澶鐨勯偖绠憋紝鎵€浠ユ殏鏃朵笉闇€瑕佽€冭檻銆傚嵆渚挎湭鏉ヨ鏀癸紝鐢变簬鏋舵瀯璁捐寰楀綋锛屾敼鍔ㄤ篃浼氳闄愬埗鍦ㄨ繖涓嚱鏁颁腑
    while (!list_empty(&tx_handler->mailboxList))
    {
        p_msg_list = list_first_entry(&tx_handler->txFifo.usedList, CanMsgList_s, fifoListNode);
        CanHwMsg_s hw_msg = {
            .dsc = p_msg_list->userMsg.dsc,
            .hwFilterBank = -1,
            .hwTxMailbox = -1,
            .data = p_msg_list->userMsg.userBuf,
        };
        OmRet_e ret = can->hwInterface->sendMsgMailbox(can, &hw_msg);
        if (ret == OM_OK)
        {
            // 鍙戦€佹垚鍔?
            while (IS_CAN_MAILBOX_INVALID(can, hw_msg.hwTxMailbox))
            {
            }; // TODO: assert
            mailbox = &tx_handler->pMailboxes[hw_msg.hwTxMailbox];
            // 浠庣瓑寰呴槦鍒楋紙usedList锛変腑绉婚櫎娑堟伅
            list_del(&p_msg_list->fifoListNode);
            // 浠庡彲鐢ㄩ偖绠变腑绉婚櫎閭
            list_del(&mailbox->list);
            // 鏍囪閭
            mailbox->isBusy = 1;
            mailbox->pMsgList = p_msg_list;
            p_msg_list->owner = mailbox;
        }
        else
        {
            // TODO: 杩涘叆杩欓噷閫氬父璇存槑杩涘叆 BUS_OFF 鐘舵€佹垨鍏朵粬鏈煡鍘熷洜
            // 杩欓噷绠€鍗曞鐞嗭紝鐩存帴閫€鍑哄惊鐜?
            // TODO: LOG "CAN status: %d锛屽彂閫佸け璐ワ紝bank: %d锛宮sgID: 0x%08x"
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
 * @brief 鍚慍AN鍙戦€丗IFO涓坊鍔犱竴涓狢AN娑堟伅锛岄潪闃诲
 *
 * @param Can CAN鍙ユ焺
 * @param pUserTxMsgBuf CAN鐢ㄦ埛鍙戦€佹秷鎭暟缁勬寚閽?
 * @param msgNum 娑堟伅鏁伴噺
 * @return size_t 瀹為檯鍙戦€佺殑娑堟伅鏁伴噺
 */
static size_t cantx_msg_put_nonblock(HalCanHandler_t can, CanUserMsg_t p_user_tx_msg_buf, size_t msg_num)
{
    uint32_t int_level;
    size_t msg_counter = 0;
    CanMsgList_t p_msg_list = NULL;
    for (msg_counter = 0; msg_counter < msg_num; msg_counter++)
    {
        // 鑾峰彇涓€涓┖闂叉秷鎭摼琛ㄩ」锛岀敤浜庡瓨鍌ㄤ俊鎭?
        if (cantx_get_free_msg_list(&can->txHandler, &p_msg_list) == CAN_ERR_SOFT_FIFO_OVERFLOW)
        {
            if (!can->txHandler.txFifo.isOverwrite)
            {
                int_level = can_irq_lock();
                can->statusManager.errCounter.txSoftOverFlowCnt += (msg_num - msg_counter);
                can->statusManager.errCounter.txFailCnt += (msg_num - msg_counter); // 琚鐩栫殑娑堟伅瀹氫箟涓哄彂閫佸け璐ョ殑娑堟伅
                can_irq_unlock(int_level);
                break; // 璺冲嚭寰幆锛岀粨鏉熷啓鍏?
            }
            else // 瑕嗗啓
            {
                int_level = can_irq_lock();
                can->statusManager.errCounter.txFailCnt++; // 鏈鍙戦€佺殑娑堟伅瀹氫箟涓哄彂閫佸け璐ョ殑娑堟伅
                can->statusManager.errCounter.txSoftOverFlowCnt++;
                can_irq_unlock(int_level);
            }
        }
        // 浠庤繖閲屼箣鍚庯紝pMsgList宸茬粡浠嶧ifo涓鍙栧嚭
        // 姝ゆ椂闄や簡褰撳墠鎿嶄綔涔嬪锛屽凡缁忔病鏈変换浣曚唬鐮佽兘璁块棶杩欎釜閾捐〃椤癸紝鎵€浠ラ拡瀵筽MsgList鐨勬搷浣滄棤闇€鑰冭檻绔炴€?

        // 濉厖鐢ㄦ埛娑堟伅鎸囬拡
        p_msg_list->userMsg = p_user_tx_msg_buf[msg_counter];
        // 濉厖CAN娑堟伅瀹瑰櫒
        memcpy((void *)p_msg_list->container, (void *)p_user_tx_msg_buf[msg_counter].userBuf, p_user_tx_msg_buf[msg_counter].dsc.dataLen);
        p_msg_list->userMsg.userBuf = p_msg_list->container;

        int_level = can_irq_lock();
        can_add_used_msg_list(&can->txHandler.txFifo, p_msg_list);
        can_irq_unlock(int_level);
    }
    // 鍦ㄤ腑鏂笂涓嬫枃涓笉闇€瑕佷富鍔ㄨ皟搴?
    if (!osal_is_in_isr())
        can_tx_scheduler(can);
    return msg_counter;
}

/**
 * @brief CAN杞欢閲嶄紶鏈哄埗
 *
 * @param Can CAN璁惧鎸囬拡
 * @param mailbox 閭鎸囬拡
 */
static void cantx_soft_retransmit(HalCanHandler_t can, uint32_t mailbox_bank)
{
    // 鐩稿叧閭涓庢秷鎭妭鐐逛細鍏堜粠鍏变韩閾捐〃鎽橀櫎锛屽啀鎵ц閲嶄紶娴佺▼锛?
    // 鍥犳璇ユ祦绋嬪唴瀵硅繖涓ょ被瀵硅薄鐨勮闂笉涓庡閮ㄥ苟鍙戝啿绐侊紙浠呴檺璇ラ偖绠变笌璇ユ秷鎭妭鐐癸級銆?
    CanMailbox_t mailbox = &can->txHandler.pMailboxes[mailbox_bank];
    CanMsgList_t p_msg_list = mailbox->pMsgList;
    while (!p_msg_list)
    {
    }; // TODO: assert
    p_msg_list->owner = NULL;
    uint32_t int_level;
    int_level = can_irq_lock();
    list_add(&p_msg_list->fifoListNode, &can->txHandler.txFifo.usedList); // 澶存彃锛岀‘淇濆厛鍙戦€佺殑娑堟伅浼樺厛閲嶅彂
    can_irq_unlock(int_level);
    mailbox->isBusy = 0;
    mailbox->pMsgList = NULL;
    list_add_tail(&mailbox->list, &can->txHandler.mailboxList);
    can_tx_scheduler(can);
}

/*
 * @brief 鍒濆鍖朇AN璁惧
 * @param Dev CAN璁惧鎸囬拡
 * @retval  1. OM_ERROR_PARAM 鍙傛暟閿欒
 *          2. OM_ERROR_HW 纭欢閿欒
 *          3. OM_ERROR_NONE 鎴愬姛
 */
static OmRet_e can_init(Device_t dev)
{
    OmRet_e ret;
    if (!dev)
        return OM_ERROR_PARAM;
    HalCanHandler_t can = (HalCanHandler_t)dev;
    if (!can->adapterInterface || !can->hwInterface)
        return OM_ERROR_PARAM;
    can_filter_resmgr_deinit(can);
    can_rxhandler_deinit(&can->rxHandler);
    can_txhandler_deinit(&can->txHandler);
    ret = can->hwInterface->configure(can, &can->cfg);
    return ret;
}

static OmRet_e can_open(Device_t dev, uint32_t oparam)
{
    OmRet_e ret;
    uint32_t iotype;
    if (!dev)
        return OM_ERROR_PARAM;
    HalCanHandler_t can = (HalCanHandler_t)dev;

    // 鍏堝垵濮嬪寲杩囨护鍣ㄨ祫婧愮鐞嗗櫒锛屽啀鍒濆鍖?RX/TX 瀛愭ā鍧椼€?
    // 杩欐牱鍚庣画妯″潡鍙互鐩存帴浣跨敤绋冲畾鐨?slot <-> hwBank 鏄犲皠銆?
    ret = can_filter_resmgr_init(can);
    if (ret != OM_OK)
        return ret;
    // filterNum 鐢?capability 椹卞姩锛屼笉鍐嶄緷璧栭潤鎬侀厤缃€笺€?
    can->cfg.filterNum = can->filterResMgr.slotCount;

    iotype = oparam & DEVICE_O_RXTYPE_MASK;

    ret = can_rxhandler_init(can, iotype, can->filterResMgr.slotCount, can->cfg.rxMsgListBufSize);
    if (ret != OM_OK)
    {
        // 鍥炴粴 filter_resmgr锛岄伩鍏?open 澶辫触鍚庢畫鐣欒祫婧愬崰鐢ㄣ€?
        can_filter_resmgr_deinit(can);
        return ret;
    }

    iotype = oparam & DEVICE_O_TXTYPE_MASK;
    ret = can_txhandler_init(can, iotype, can->cfg.mailboxNum, can->cfg.txMsgListBufSize);
    if (ret != OM_OK)
    {
        // TODO: LOG ERR
        can_rxhandler_deinit(&can->rxHandler);
        // TX 鍒濆鍖栧け璐ュ悓鏍烽渶瑕侀噴鏀?filter_resmgr銆?
        can_filter_resmgr_deinit(can);
        return ret;
    }
    ret = can_status_manager_init(can);
    if (ret != OM_OK)
    {
        // TODO: LOG ERR
        can_rxhandler_deinit(&can->rxHandler);
        can_txhandler_deinit(&can->txHandler);
        // 鐘舵€佺鐞嗗櫒澶辫触鏃讹紝淇濇寔 open 杩囩▼鍏ㄩ噺鍥炴粴銆?
        can_filter_resmgr_deinit(can);
        return ret;
    }
    return ret;
}

static size_t can_write(Device_t dev, void *pos, void *data, size_t len)
{
    size_t msg_num = 0;
    HalCanHandler_t can = (HalCanHandler_t)dev;
    // 鐩墠鍙敮鎸侀潪闃诲鍙戦€佺瓥鐣?
    msg_num = cantx_msg_put_nonblock(can, (CanUserMsg_t)data, len);
    return msg_num;
}

/**
 * @brief 浠嶤AN鎺ユ敹FIFO涓鍙栧涓狢AN娑堟伅
 *
 * @param Dev CAN璁惧鎸囬拡
 * @param pos CAN 涓嶉渶瑕佽鍙傛暟锛屼繚鎸丯ULL鍗冲彲
 * @param buf 鎺ユ敹娑堟伅缂撳啿鍖烘寚閽?
 * @param len 鎺ユ敹娑堟伅缂撳啿鍖洪暱搴?
 * @return size_t 瀹為檯璇诲彇鐨勬秷鎭暟閲?
 */
static size_t can_read(Device_t dev, void *pos, void *buf, size_t len)
{
    size_t cnt = 0;
    HalCanHandler_t can = (HalCanHandler_t)dev;
    CanRxHandler_t rx_handler = &can->rxHandler;
    CanErrorCode_e ret = CAN_ERR_NONE;
    CanUserMsg_t p_user_rx_msg_buf = (CanUserMsg_t)buf;

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
 * @brief 鎺у埗CAN璁惧
 * @param Dev CAN璁惧鎸囬拡
 * @param cmd 鍛戒护 @ref CAN_CMD_DEF
 * @param args 鍙傛暟鎸囬拡
 * @retval  1. OM_ERROR_PARAM 鍙傛暟閿欒
 *          2. OM_ERROR_NONE 鎴愬姛
 */
static OmRet_e can_ctrl(Device_t dev, size_t cmd, void *args)
{
    OmRet_e ret = OM_OK;
    if (!dev)
        return OM_ERROR_PARAM;
    HalCanHandler_t can = (HalCanHandler_t)dev;
    switch (cmd)
    {
    case CAN_CMD_SET_IOTYPE:
        ret = can->hwInterface->control(can, CAN_CMD_SET_IOTYPE, args);
        break;

    case CAN_CMD_CLR_IOTYPE:
        ret = can->hwInterface->control(can, CAN_CMD_CLR_IOTYPE, args);
        break;

    // 瀛︿範鑰呴渶娉ㄦ剰杩欓噷鐨勭▼搴忚璁★紝妫€鏌ュ弬鏁板悎娉曟€э紝骞朵笖鎿嶄綔澶辫触鍚庝篃涓嶄細浜х敓鍓綔鐢紙鍗冲鏋滈厤缃け璐ワ紝CAN璁惧鐨勭姸鎬佹垨鏁版嵁涓嶄細鏀瑰彉锛夛紝杩欏湪鏋舵瀯璁捐涓槸蹇呰鐨?
    case CAN_CMD_CFG:
        if (!args)
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        ret = can->hwInterface->control(can, CAN_CMD_CFG, args);
        if (ret == OM_OK)
            can->cfg = *(CanCfg_t)args;
        break;

    // 瀛︿範鑰呴渶娉ㄦ剰杩欓噷鐨勭▼搴忚璁★紝妫€鏌ュ弬鏁板悎娉曟€э紝骞朵笖鎿嶄綔澶辫触鍚庝篃涓嶄細浜х敓鍓綔鐢紙鍗冲鏋滈厤缃け璐ワ紝CAN璁惧鐨勭姸鎬佹垨鏁版嵁涓嶄細鏀瑰彉锛夛紝杩欏湪鏋舵瀯璁捐涓槸蹇呰鐨?
    case CAN_CMD_FILTER_ALLOC: {
        CanFilterAllocArg_t alloc_arg = (CanFilterAllocArg_t)args;
        if (alloc_arg == NULL || alloc_arg->request.rxCallback == NULL)
        {
            ret = OM_ERROR_PARAM;
            break;
        }

        uint16_t slot = 0;
        int16_t hw_bank = -1;
        // 鍏堝湪 filter_resmgr 涓崰鐢?slot/bank锛屽啀涓嬪彂纭欢閰嶇疆銆?
        ret = can_reserve_slot(can, &slot, &hw_bank);
        if (ret != OM_OK)
            break;

        CanHwFilterCfg_s hw_cfg = {
            .bank = (size_t)hw_bank,
            .workMode = alloc_arg->request.workMode,
            .idType = alloc_arg->request.idType,
            .id = alloc_arg->request.id,
            .mask = alloc_arg->request.mask,
        };
        ret = can->hwInterface->control(can, CAN_CMD_FILTER_ALLOC, &hw_cfg);
        if (ret != OM_OK)
        {
            // 纭欢閰嶇疆澶辫触蹇呴』鍥炴粴璧勬簮绠＄悊鍣紝閬垮厤鈥滈€昏緫宸插崰鐢?纭欢鏈敓鏁堚€濅笉涓€鑷淬€?
            can_release_slot(can, slot);
            break;
        }

        CanFilter_t filter = &can->rxHandler.filterTable[slot];
        // 纭欢閰嶇疆鎴愬姛鍚庡啀鍙戝竷妗嗘灦鎬?filter 淇℃伅銆?
        filter->request = alloc_arg->request;
        filter->isActived = 1;
        filter->msgCount = 0;
        alloc_arg->handle = (CanFilterHandle_t)slot;
    }
    break;

    case CAN_CMD_FILTER_FREE: {
        if (args == NULL)
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        CanFilterHandle_t handle = *(CanFilterHandle_t *)args;
        if (IS_CAN_FILTER_INVALID(can, handle))
        {
            ret = OM_ERROR_PARAM;
            break;
        }

        CanFilter_t filter = &can->rxHandler.filterTable[handle];
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

        CanHwFilterCfg_s hw_cfg = {
            .bank = (size_t)hw_bank,
            .workMode = filter->request.workMode,
            .idType = filter->request.idType,
            .id = filter->request.id,
            .mask = filter->request.mask,
        };
        ret = can->hwInterface->control(can, CAN_CMD_FILTER_FREE, &hw_cfg);
        if (ret != OM_OK)
            break;

        // 鍏堟竻绌烘鏋舵€?filter锛屽啀閲婃斁 slot/bank 鍗犵敤浣嶃€?
        memset(filter, 0, sizeof(CanFilter_s));
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

static OmRet_e can_close(Device_t dev)
{
    if (!dev)
        return OM_ERROR_PARAM;
    HalCanHandler_t can = (HalCanHandler_t)dev;
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
    // close 闃舵蹇呴』鍥炴敹 filter_resmgr锛岄伩鍏嶄笅娆?open 鐪嬪埌闄堟棫鏄犲皠銆?
    can_filter_resmgr_deinit(can);
    return OM_OK;
}

/*
 * @brief CAN閿欒涓柇澶勭悊鍑芥暟
 * @param Can CAN璁惧鎸囬拡
 * @param errorEvent 閿欒浜嬩欢
 * @note 璇ュ嚱鏁颁富瑕佹洿鏂?CAN 璁惧鐘舵€侊紱鏇寸粏绮掑害鐨勬仮澶嶇瓥鐣ョ敱鐘舵€佺鐞嗙嚎绋嬪鐞嗐€?
 */
void can_error_isr(HalCanHandler_t can, uint32_t err_event, size_t param)
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
    case CAN_ERR_EVENT_BUS_STATUS: // TODO: 澶勭悊鎬荤嚎閿欒
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

void hal_can_isr(HalCanHandler_t can, CanIsrEvent_e event, size_t param)
{
    switch (event)
    {
    case CAN_ISR_EVENT_INT_RX_DONE: // 鎺ユ敹瀹屾垚涓柇锛宲aram鏄帴鏀舵秷鎭殑FIFO绱㈠紩
    {
        OmRet_e ret;
        CanMsgList_t msg_list = NULL;
        CanHwMsg_s hw_msg = {0};
        uint8_t hw_data[64];
        hw_msg.data = hw_data;
        if (canrx_get_free_msg_list(&can->rxHandler, &msg_list) == CAN_ERR_SOFT_FIFO_OVERFLOW)
        {
            can->statusManager.errCounter.rxSoftOverFlowCnt++; // 鎺ユ敹杞欢FIFO婧㈠嚭璁℃暟鍣ㄥ鍔?
            if (!can->rxHandler.rxFifo.isOverwrite)            // 闈炶鍐欑瓥鐣ョ洿鎺ラ€€鍑?
            {
                can->hwInterface->recvMsg(can, NULL, param); // 涓㈠純褰撳墠甯э紙娓呬腑鏂級
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
            msg_list->userMsg.filterHandle = (CanFilterHandle_t)slot;
            memcpy(msg_list->container, hw_msg.data, hw_msg.dsc.dataLen);
            canrx_msg_put(can, msg_list);
            can->statusManager.errCounter.rxMsgCount++; // 鎺ユ敹娑堟伅璁℃暟鍣ㄥ鍔?
        }
        else
        {
            can->statusManager.errCounter.rxFailCnt++; // 鎺ユ敹閿欒璁℃暟鍣ㄥ鍔?
        }
    }
    break;
    case CAN_ISR_EVENT_INT_TX_DONE: // 鍙戦€佸畬鎴愪腑鏂紝param 鏄偖绠辩紪鍙?
        while (IS_CAN_MAILBOX_INVALID(can, (int32_t)param))
        {
        }; // TODO: assert
        CanMailbox_t mailbox = &can->txHandler.pMailboxes[param];

        // 璧勬簮鍥炴敹
        uint32_t int_level = can_irq_lock();
        cantx_add_free_msg_list(&can->txHandler, mailbox);
        can->statusManager.errCounter.txMsgCount++; // 鍙戦€佹秷鎭鏁板櫒澧炲姞
        device_write_cb(&can->parent, param);
        can_tx_scheduler(can);
        can_irq_unlock(int_level);
        break;
    default:
        break;
    }
}

static DevInterface_s can_dev_interface = {
    .init = can_init,
    .open = can_open,
    .write = can_write,
    .read = can_read,
    .control = can_ctrl,
    .close = can_close,
};

/**
 * @brief CAN璁惧娉ㄥ唽锛堝吋瀹圭粡鍏?CAN 鍜?CANFD锛?
 * @param can CAN璁惧鍙ユ焺
 * @param name 璁惧鍚嶇О
 * @param handle 纭欢鍙ユ焺
 * @param regparams 娉ㄥ唽鍙傛暟 @ref CAN_REG_DEF
 * @return OmRet_e 娉ㄥ唽缁撴灉
 * @retval OM_OK 鎴愬姛
 * @retval OM_ERROR_PARAM 鍙傛暟閿欒
 * @retval OM_ERR_CONFLICT 璁惧鍚嶇О鍐茬獊
 */
OmRet_e hal_can_register(HalCanHandler_t can, char *name, void *handle, uint32_t regparams)
{
    OmRet_e ret;
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

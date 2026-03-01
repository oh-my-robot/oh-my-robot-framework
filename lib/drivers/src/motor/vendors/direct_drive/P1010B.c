/**
 * @file P1010B.c
 * @brief P1010B 鐩撮┍鐢垫満 CAN 棣栫増椹卞姩瀹炵幇
 * @note 棣栫増鍙疄鐜?CAN 杩愯璺緞锛屼笉瀹炵幇 RS485銆?
 */

#include "drivers/motor/vendors/direct_drive/P1010B.h"
#include "osal/osal_core.h"
#include "osal/osal_time.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* 绉佹湁甯搁噺涓庝綅鍩熸槧灏?                                                        */
/* -------------------------------------------------------------------------- */

/** 姣忎釜 0x32/0x33 鍒嗙粍甯у寘鍚?4 涓數鏈烘Ы浣?*/
#define P1010B_GROUP_SLOT_COUNT (4U)
/** reply_base_id 鎺╃爜锛堥珮 4 浣嶏級 */
#define P1010B_REPLY_ID_MASK (0xF0U)
/** motor_id 鎺╃爜锛堜綆 4 浣嶏級 */
#define P1010B_REPLY_ID_LOW_MASK (0x0FU)
/** 鍗忚搴旂瓟鍩哄湴鍧€鏈€灏?nibble锛?x50锛?*/
#define P1010B_REPLY_BASE_MIN_NIBBLE (0x5U)
/** 鍗忚搴旂瓟鍩哄湴鍧€鏈€澶?nibble锛?xB0锛?*/
#define P1010B_REPLY_BASE_MAX_NIBBLE (0xBU)
/** reply_base_id 绾挎€у垎鍙戣〃澶у皬 */
#define P1010B_REPLY_DISPATCH_ENTRY_COUNT (P1010B_REPLY_BASE_MAX_NIBBLE - P1010B_REPLY_BASE_MIN_NIBBLE + 1U)
/** 榛樿鍚屾浜嬪姟瓒呮椂锛坢s锛?*/
#define P1010B_DEFAULT_SYNC_TIMEOUT_MS (20U)
/** 闈炴硶妯″紡鐩爣 scale 鍊?*/
#define P1010B_TARGET_SCALE_INVALID (0.0f)
/** 寮€鐜?鐢垫祦/浣嶇疆妯″紡鐩爣 scale */
#define P1010B_TARGET_SCALE_X100 (100.0f)
/** 閫熷害妯″紡鐩爣 scale */
#define P1010B_TARGET_SCALE_X10 (10.0f)
/** fault signature: calibrated 浣嶇Щ */
#define P1010B_FAULT_SIGNATURE_CALIBRATED_SHIFT (0U)
/** fault signature: faultCode 璧峰浣?*/
#define P1010B_FAULT_SIGNATURE_FAULT_SHIFT (1U)
/** fault signature: faultCode 鏈夋晥鎺╃爜 */
#define P1010B_FAULT_SIGNATURE_FAULT_MASK (0x0FU)
/** fault signature: alarmCode 璧峰浣?*/
#define P1010B_FAULT_SIGNATURE_ALARM_SHIFT (5U)
/** fault signature: alarmCode 鏈夋晥鎺╃爜 */
#define P1010B_FAULT_SIGNATURE_ALARM_MASK (0x7FU)
/** reply_base_id -> 鍒嗗彂琛ㄧ储寮?*/
#define P1010B_REPLY_DISPATCH_INDEX(_replyBaseId) \
    ((uint8_t)((((uint8_t)(_replyBaseId)) >> 4U) - P1010B_REPLY_BASE_MIN_NIBBLE))

static const P1010BCommandDescriptor_s *p1010b_internal_find_command_descriptor(P1010BCommand_e command);
static const P1010BRxDispatchDescriptor_s *p1010b_internal_find_rx_dispatch_descriptor(uint8_t reply_base_id);

/* -------------------------------------------------------------------------- */
/* 鍩虹宸ュ叿鍑芥暟                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief 鏍￠獙鐢垫満 ID 鏄惁鍦ㄥ崗璁敮鎸佽寖鍥村唴
 */
static inline bool p1010b_internal_is_valid_motor_id(uint8_t motor_id)
{
    return (motor_id >= P1010B_MOTOR_ID_MIN && motor_id <= P1010B_MOTOR_ID_MAX);
}

/**
 * @brief 鎸夊ぇ绔鍙?16 浣嶆湁绗﹀彿鏁?
 */
static inline int16_t p1010b_internal_read_be_i16(const uint8_t *buffer)
{
    return (int16_t)(((uint16_t)buffer[0] << 8U) | (uint16_t)buffer[1]);
}

/**
 * @brief 鎸夊ぇ绔鍙?32 浣嶆湁绗﹀彿鏁?
 */
static inline int32_t p1010b_internal_read_be_i32(const uint8_t *buffer)
{
    return (int32_t)(((uint32_t)buffer[0] << 24U) | ((uint32_t)buffer[1] << 16U) | ((uint32_t)buffer[2] << 8U) |
                     (uint32_t)buffer[3]);
}

/**
 * @brief int32 -> int16 楗卞拰鎴柇
 */
static inline int16_t p1010b_internal_clamp_i16(int32_t value)
{
    if (value > INT16_MAX)
    {
        return INT16_MAX;
    }
    if (value < INT16_MIN)
    {
        return INT16_MIN;
    }
    return (int16_t)value;
}

/**
 * @brief 鎸夊綋鍓嶆ā寮忔妸鐗╃悊閲忕洰鏍囧€兼崲绠椾负鍗忚鍘熷閲?
 * @details
 * - 寮€鐜?鐢垫祦/浣嶇疆锛?100
 * - 閫熷害锛?10
 * - 浣跨敤鍥涜垗浜斿叆鍚庡啀鍋?int16 楗卞拰銆?
 */
static inline int16_t p1010b_internal_scale_target(float scale, float target_value)
{
    float scaled = target_value * scale;
    if (scaled >= 0.0f)
    {
        scaled += 0.5f;
    }
    else
    {
        scaled -= 0.5f;
    }
    return p1010b_internal_clamp_i16((int32_t)scaled);
}

/**
 * @brief 灏嗙數鏈?ID 鏄犲皠鍒?0x32/0x33 鍒嗙粍
 * @return 0 琛ㄧず 1~4锛? 琛ㄧず 5~8
 */
static inline uint8_t p1010b_internal_get_group_index(uint8_t motor_id)
{
    return (motor_id <= 4U) ? 0U : 1U;
}

/**
 * @brief 璁＄畻鐢垫満 ID 鍦ㄥ垎缁勫抚涓殑妲戒綅绱㈠紩锛?~3锛?
 */
static inline uint8_t p1010b_internal_get_slot_index(uint8_t motor_id)
{
    return (uint8_t)((motor_id - 1U) % P1010B_GROUP_SLOT_COUNT);
}

/**
 * @brief 鍙傛暟鐧藉悕鍗曞垽瀹?
 * @note 浠呯櫧鍚嶅崟鍙傛暟鍏佽鍐欏叆锛岄槻姝㈤珮椋庨櫓鍙傛暟琚鍐欍€?
 */
static inline bool p1010b_internal_is_parameter_whitelisted(uint8_t parameter_id)
{
    switch (parameter_id)
    {
    case P1010B_PARAM_FAULT_MASK:
    case P1010B_PARAM_HEARTBEAT_ENABLE:
    case P1010B_PARAM_WORK_MODE:
    case P1010B_PARAM_MOTOR_ID:
    case P1010B_PARAM_CAN_BAUD_MODE:
    case P1010B_PARAM_HEARTBEAT_TIME:
        return true;
    default:
        return false;
    }
}

/**
 * @brief 鎵撳寘鍒嗙粍鎺у埗甯ц浇鑽?
 * @details
 * 姣忎釜鐢垫満鍗?2 瀛楄妭锛岄『搴忓搴旂粍鍐呮Ы浣?0~3銆?
 */
static void p1010b_internal_pack_group_payload(const P1010BBus_s *bus, uint8_t group_index, uint8_t payload[P1010B_CAN_DLC])
{
    for (uint8_t slot = 0; slot < P1010B_GROUP_SLOT_COUNT; slot++)
    {
        int16_t raw_value = bus->groupTargetsRaw[group_index][slot];
        payload[(slot * 2U)] = (uint8_t)(((uint16_t)raw_value >> 8U) & 0xFFU);
        payload[(slot * 2U) + 1U] = (uint8_t)((uint16_t)raw_value & 0xFFU);
    }
}

/**
 * @brief 鍙戦€?1 甯ф爣鍑?CAN 鏁版嵁甯?
 * @param bus 鎬荤嚎涓婁笅鏂?
 * @param can_id 鏍囧噯甯?ID
 * @param payload 8 瀛楄妭杞借嵎
 * @return `OM_OK` 鍙戦€佹垚鍔燂紱澶辫触杩斿洖閿欒鐮?
 */
static OmRet_e p1010b_internal_send_can_frame(P1010BBus_s *bus, uint16_t can_id, const uint8_t payload[P1010B_CAN_DLC])
{
    CanUserMsg_s message;

    if (!bus || !bus->canDevice || !payload)
        return OM_ERROR_PARAM;
    message.dsc = CAN_DATA_MSG_DSC_INIT(can_id, CAN_IDE_STD, P1010B_CAN_DLC);
    message.filterHandle = 0;
    message.userBuf = (uint8_t *)payload;
    if (device_write(bus->canDevice, 0, &message, 1) == 0U)
        return OM_ERROR;
    return OM_OK;
}

/**
 * @brief 璁＄畻鐩爣缂╂斁绯绘暟锛岃皟鐢ㄦ柟璐熻矗鍥炲啓鍒扮紦瀛?
 */
static float p1010b_internal_update_target_scale_cache(P1010BMode_e mode)
{
    switch (mode)
    {
    case P1010B_MODE_OPEN_LOOP:
    case P1010B_MODE_CURRENT:
    case P1010B_MODE_POSITION:
        return P1010B_TARGET_SCALE_X100;
    case P1010B_MODE_SPEED:
        return P1010B_TARGET_SCALE_X10;
    default:
        return P1010B_TARGET_SCALE_INVALID;
    }
}

/**
 * @brief 鏇存柊鍦ㄧ嚎鐘舵€佸苟瑙﹀彂杈规部鍥炶皟
 */
static inline void p1010b_internal_mark_online(P1010BDriver_s *driver, bool online)
{
    if (!driver)
    {
        return;
    }
    if (driver->telemetry.online == online)
    {
        return;
    }
    driver->telemetry.online = online;
    if (driver->callbacks.onOnlineChanged)
    {
        driver->callbacks.onOnlineChanged(driver, online);
    }
}

/**
 * @brief 瑙ｆ瀽鍚屾浜嬪姟绛夊緟瓒呮椂
 */
static inline uint32_t p1010b_internal_resolve_sync_timeout_ms(const P1010BDriver_s *driver, uint32_t timeout_ms)
{
    if (timeout_ms > 0U)
    {
        return timeout_ms;
    }
    if (driver && driver->config.requestTimeoutMs > 0U)
    {
        return driver->config.requestTimeoutMs;
    }
    return P1010B_DEFAULT_SYNC_TIMEOUT_MS;
}

/**
 * @brief 鍦ㄩ攣淇濇姢涓嬫竻绌哄悓姝ヤ簨鍔＄姸鎬?
 */
static inline void p1010b_internal_clear_sync_transaction_locked(P1010BDriver_s *driver)
{
    if (!driver)
    {
        return;
    }
    driver->sync.pending = false;
    driver->sync.pendingCommand = P1010B_COMMAND_NONE;
    driver->sync.expectedReplyBaseId = 0U;
    driver->sync.expectedParameterId = 0U;
    driver->sync.expectedStateCommand = 0U;
}

/**
 * @brief 鍒濆鍖栧搷搴斿璞?
 */
static inline void p1010b_internal_init_response(P1010BResponse_s *response, P1010BCommand_e command, OmRet_e result)
{
    if (!response)
    {
        return;
    }

    memset(response, 0, sizeof(P1010BResponse_s));
    response->command = command;
    response->result = result;
}

/**
 * @brief 鍙栨秷褰撳墠鍚屾浜嬪姟锛堢嚎绋嬩笂涓嬫枃锛?
 */
static void p1010b_internal_cancel_sync_transaction(P1010BDriver_s *driver)
{
    if (!driver)
        return;
    osal_irq_lock_task();
    p1010b_internal_clear_sync_transaction_locked(driver);
    osal_irq_unlock_task();
}

/**
 * @brief 鍚姩鍚屾浜嬪姟骞剁櫥璁板尮閰嶆潯浠?
 */
static OmRet_e p1010b_internal_begin_sync_transaction(P1010BDriver_s *driver, P1010BCommand_e command, const P1010BEncodedRequest_s *encoded)
{
    if (!driver || !encoded)
    {
        return OM_ERROR_PARAM;
    }

    /* 娓呯┖鍙兘閬楃暀鐨勫畬鎴愰噺淇″彿锛岀‘淇濇湰娆?wait 鍙搴斿綋鍓嶄簨鍔°€?*/
    while (completion_wait(&driver->sync.completion, 0U) == OM_OK)
    {
    }

    osal_irq_lock_task();
    if (driver->sync.pending)
    {
        osal_irq_unlock_task();
        return OM_ERROR_BUSY;
    }
    driver->sync.pending = true;
    driver->sync.pendingCommand = command;
    driver->sync.expectedReplyBaseId = encoded->ackBaseId;
    driver->sync.expectedParameterId = encoded->expectedParameterId;
    driver->sync.expectedStateCommand = encoded->expectedStateCommand;
    driver->sync.result = OM_ERROR_TIMEOUT;
    driver->sync.timestampMs = 0U;
    p1010b_internal_init_response(&driver->sync.response, command, OM_ERROR_TIMEOUT);
    osal_irq_unlock_task();
    return OM_OK;
}

/**
 * @brief 绛夊緟鍚屾浜嬪姟瀹屾垚
 */
static OmRet_e p1010b_internal_wait_sync_transaction(P1010BDriver_s *driver, uint32_t timeout_ms, P1010BResponse_s *response)
{
    OmRet_e ret;

    ret = completion_wait(&driver->sync.completion, p1010b_internal_resolve_sync_timeout_ms(driver, timeout_ms));
    if (ret != OM_OK)
    {
        p1010b_internal_cancel_sync_transaction(driver);
        if (ret == OM_ERROR_TIMEOUT)
            /* 瓒呮椂鍚庢秷璐瑰彲鑳借繜鍒扮殑 done 淇″彿锛岄伩鍏嶆薄鏌撲笅涓€娆″悓姝ヤ簨鍔°€?*/
            while (completion_wait(&driver->sync.completion, 0U) == OM_OK)
            {
            }
        return ret;
    }

    osal_irq_lock_task();
    ret = driver->sync.result;
    if (response)
        *response = driver->sync.response;
    osal_irq_unlock_task();
    return ret;
}

/**
 * @brief 鍙戦€佸悓姝ヤ簨鍔¤姹傦紙鏀寔閲嶈瘯锛?
 */
static OmRet_e p1010b_internal_send_sync_transaction(P1010BDriver_s *driver, const P1010BRequest_s *request, const P1010BEncodedRequest_s *encoded, P1010BResponse_s *response)
{
    OmRet_e ret = OM_ERROR;
    uint32_t attempt_count;

    attempt_count = (uint32_t)driver->config.maxRetryCount + 1U;
    for (uint32_t attempt_index = 0U; attempt_index < attempt_count; attempt_index++)
    {
        ret = p1010b_internal_begin_sync_transaction(driver, request->command, encoded);
        if (ret != OM_OK)
            break;

        ret = p1010b_internal_send_can_frame(driver->bus, encoded->requestCanId, encoded->payload);
        if (ret != OM_OK)
        {
            p1010b_internal_cancel_sync_transaction(driver);
            break;
        }
        driver->telemetry.lastSuccessTxTimestampMs = osal_time_now_monotonic();

        ret = p1010b_internal_wait_sync_transaction(driver, request->timeoutMs, response);
        if (ret == OM_OK)
            break;
    }
    return ret;
}

/**
 * @brief 閫氳繃鎻忚堪绗﹀垽瀹氬悓姝ュ簲绛旀槸鍚﹀尮閰?
 */
static bool p1010b_internal_is_sync_reply_matched_by_descriptor(const P1010BCommandDescriptor_s *descriptor,
                                                                const P1010BDriver_s *driver, const P1010BRawFrame_s *frame)
{
    uint8_t reply_base_id;

    if (!descriptor || !driver || !frame)
    {
        return false;
    }
    if (descriptor->ackBaseId == P1010B_CAN_ACK_NONE)
    {
        return false;
    }

    reply_base_id = (uint8_t)(frame->canId & P1010B_REPLY_ID_MASK);
    if (reply_base_id != descriptor->ackBaseId)
    {
        return false;
    }

    if (!descriptor->ackMatchFn)
    {
        return true;
    }
    return descriptor->ackMatchFn(driver, frame);
}

/**
 * @brief 鐢辨弿杩扮鍦?ISR 鍐呰В鐮佸悓姝ュ簲绛?
 */
static void p1010b_internal_decode_sync_reply_by_descriptor_in_isr(const P1010BCommandDescriptor_s *descriptor, P1010BDriver_s *driver, const P1010BRawFrame_s *frame, P1010BIsrCallbackContext_s *callback_context)
{
    if (!descriptor || !driver || !frame)
    {
        return;
    }

    p1010b_internal_init_response(&driver->sync.response, descriptor->command, OM_OK);
    driver->sync.response.timestampMs = frame->timestampMs;
    memcpy(driver->sync.response.ackPayload, frame->payload, sizeof(frame->payload));
    if (descriptor->decodeAckFn)
    {
        descriptor->decodeAckFn(driver, frame, &driver->sync.response, callback_context);
    }
}

/**
 * @brief 鍦?ISR 涓皾璇曞尮閰嶅苟瀹屾垚鍚屾浜嬪姟
 */
static void p1010b_internal_try_complete_sync_transaction(P1010BBus_s *bus, P1010BDriver_s *driver, const P1010BRawFrame_s *frame)
{
    OsalIrqIsrState_t irq_state;
    const P1010BCommandDescriptor_s *descriptor = NULL;
    bool matched = false;
    P1010BIsrCallbackContext_s callback_context = {0};

    if (!bus || !driver || !frame)
    {
        return;
    }

    irq_state = osal_irq_lock_from_isr();
    if (!driver->sync.pending)
    {
        osal_irq_unlock_from_isr(irq_state);
        return;
    }

    descriptor = p1010b_internal_find_command_descriptor(driver->sync.pendingCommand);
    matched = p1010b_internal_is_sync_reply_matched_by_descriptor(descriptor, driver, frame);
    if (matched)
    {
        p1010b_internal_decode_sync_reply_by_descriptor_in_isr(descriptor, driver, frame, &callback_context);
        driver->sync.result = OM_OK;
        driver->sync.timestampMs = frame->timestampMs;
        /* 鍏堟竻 pending锛屽啀 done锛岄伩鍏嶇嚎绋嬩晶鍞ら啋鍚庤鍒版湭鏀舵暃鐘舵€併€?*/
        p1010b_internal_clear_sync_transaction_locked(driver);
        (void)completion_done(&driver->sync.completion);
    }
    else
    {
        bus->rxLateReadParamCount++;
    }
    osal_irq_unlock_from_isr(irq_state);

    if (callback_context.triggerParamReadCallback && driver->callbacks.onParamRead)
    {
        /* 鍥炶皟鏀惧湪瑙ｉ攣鍚庢墽琛岋紝缂╃煭 ISR 涓寸晫鍖恒€?*/
        driver->callbacks.onParamRead(driver, callback_context.callbackParameterId, callback_context.callbackParameterValue, OM_OK,
                                      frame->timestampMs);
    }
}

/**
 * @brief 瑙ｆ瀽缁欏畾鍙嶉甯э紙0x50 + id锛?
 * @note 杈撳嚭涓鸿涔夊寲閲忥紝涓嶅悜涓婂眰鏆撮湶鍘熷瀛楄妭銆?
 */
static void p1010b_internal_update_feedback(P1010BDriver_s *driver, const P1010BRawFrame_s *frame)
{
    driver->telemetry.feedback.speedRpm = (float)p1010b_internal_read_be_i16(&frame->payload[0]) / 10.0f;
    driver->telemetry.feedback.iqAmpere = (float)p1010b_internal_read_be_i16(&frame->payload[2]) / 100.0f;
    driver->telemetry.feedback.absolutePosition = (uint16_t)p1010b_internal_read_be_i16(&frame->payload[4]);
    driver->telemetry.feedback.busVoltage = (float)p1010b_internal_read_be_i16(&frame->payload[6]) / 10.0f;
    driver->telemetry.feedback.timestampMs = frame->timestampMs;

    if (driver->callbacks.onFeedback)
    {
        driver->callbacks.onFeedback(driver, &driver->telemetry.feedback);
    }
}

/**
 * @brief 灏嗘湁鏁堟晠闅滃瓧娈垫墦鍖呬负 32 浣嶇鍚?
 * @details
 * 浠呮墦鍖呰鏍间功瀹氫箟鐨勬湁鏁堝瓧娈碉細
 * - bit0: calibrated
 * - bit1..4: faultCode
 * - bit5..11: alarmCode
 * `statusBits` 涓轰繚鐣欎綅锛屼笉鍙備笌鍥炶皟瑙﹀彂鍒ゅ畾銆?
 */
static inline uint32_t p1010b_internal_pack_fault_signature(const P1010BFaultState_s *fault_state)
{
    uint32_t signature = 0U;

    if (!fault_state)
    {
        return 0U;
    }

    signature |= ((uint32_t)(fault_state->calibrated ? 1U : 0U) << P1010B_FAULT_SIGNATURE_CALIBRATED_SHIFT);
    signature |= (((uint32_t)fault_state->faultCode & P1010B_FAULT_SIGNATURE_FAULT_MASK) << P1010B_FAULT_SIGNATURE_FAULT_SHIFT);
    signature |= (((uint32_t)fault_state->alarmCode & P1010B_FAULT_SIGNATURE_ALARM_MASK) << P1010B_FAULT_SIGNATURE_ALARM_SHIFT);
    return signature;
}

/**
 * @brief 瑙ｆ瀽鐘舵€佸簲绛斿抚锛?xA0 + id锛?
 * @details
 * 棣栫増淇濈暀鈥滄晠闅?鎶ヨ/鐘舵€佷綅鈥濊涔夛紱鏁呴殰鐮侀潪 0 鏃惰繘鍏ラ棴閿佹€併€?
 */
static void p1010b_internal_update_fault_state(P1010BDriver_s *driver, const P1010BRawFrame_s *frame)
{
    P1010BFaultState_s old_fault_state = driver->telemetry.faultState;
    uint32_t old_fault_signature;
    uint32_t new_fault_signature;

    /*
     * 鎸夈€奝1010B_111 鐢垫満瑙勬牸涔?V1.2銆嬫槧灏?0xA0+id 搴旂瓟瀛楁锛?
     * DATA[2]=CMD锛孌ATA[3]=鏍″噯鐘舵€侊紝DATA[4]=鏁呴殰鐮侊紝DATA[5]=鎶ヨ鐮侊紝DATA[6..7]=淇濈暀銆?
     */
    driver->telemetry.faultState.calibrated = ((frame->payload[3] & 0x01U) != 0U);
    driver->telemetry.faultState.faultCode = (uint16_t)frame->payload[4];
    driver->telemetry.faultState.alarmCode = (uint16_t)frame->payload[5];
    driver->telemetry.faultState.statusBits = (uint32_t)(((uint32_t)frame->payload[6] << 8U) | frame->payload[7]);

    if (driver->telemetry.faultState.faultCode != 0U)
    {
        driver->runtime.state = P1010B_STATE_FAULT_LOCKED;
        driver->runtime.lastRejectReason = P1010B_REJECT_FAULT_LOCKED;
    }

    old_fault_signature = p1010b_internal_pack_fault_signature(&old_fault_state);
    new_fault_signature = p1010b_internal_pack_fault_signature(&driver->telemetry.faultState);
    if (driver->callbacks.onFaultChanged && ((old_fault_signature ^ new_fault_signature) != 0U))
    {
        driver->callbacks.onFaultChanged(driver, &driver->telemetry.faultState);
    }
}

/**
 * @brief 璁板綍鏈€杩戞帴鏀舵椂闂村苟鏇存柊鍦ㄧ嚎鎬?
 */
static void p1010b_internal_mark_rx_seen(P1010BDriver_s *driver, uint32_t timestamp_ms)
{
    if (!driver)
    {
        return;
    }
    driver->telemetry.lastSuccessRxTimestampMs = timestamp_ms;
    p1010b_internal_mark_online(driver, true);
}

/**
 * @brief 鎶?reply_base_id 鏄犲皠鍒板垎鍙戣〃绱㈠紩
 * @return 闈炶礋绱㈠紩鏈夋晥锛?1 琛ㄧず涓嶅湪鏄犲皠鑼冨洿
 */
static int32_t p1010b_internal_reply_base_to_dispatch_index(uint8_t reply_base_id)
{
    uint8_t reply_nibble;

    if ((reply_base_id & P1010B_REPLY_ID_LOW_MASK) != 0U)
    {
        return -1;
    }

    reply_nibble = (uint8_t)(reply_base_id >> 4U);
    if (reply_nibble < P1010B_REPLY_BASE_MIN_NIBBLE || reply_nibble > P1010B_REPLY_BASE_MAX_NIBBLE)
    {
        return -1;
    }

    return (int32_t)(reply_nibble - P1010B_REPLY_BASE_MIN_NIBBLE);
}

/**
 * @brief 鎺ユ敹鍒嗗彂琛?
 * @details
 * 鎸?reply_base_id 绾挎€ф槧灏勫埌鍓綔鐢ㄥ鐞嗕笌鍚屾浜嬪姟瀹屾垚绛栫暐锛岄伩鍏?ISR 鍐呭垎鏀壂鎻忋€?
 */
static const P1010BRxDispatchDescriptor_s g_p1010b_rx_dispatch_table[P1010B_REPLY_DISPATCH_ENTRY_COUNT] = {
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_DRIVE_BASE)] = {
        .replyBaseId = P1010B_CAN_ACK_DRIVE_BASE,
        .mayCompleteSync = false,
        .sideEffectFn = p1010b_internal_update_feedback,
    },
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_REPORT_MODE)] = {
        .replyBaseId = P1010B_CAN_ACK_REPORT_MODE,
        .mayCompleteSync = true,
        .sideEffectFn = NULL,
    },
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_ACTIVE_QUERY)] = {
        .replyBaseId = P1010B_CAN_ACK_ACTIVE_QUERY,
        .mayCompleteSync = true,
        .sideEffectFn = NULL,
    },
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_WRITE_PARAM)] = {
        .replyBaseId = P1010B_CAN_ACK_WRITE_PARAM,
        .mayCompleteSync = true,
        .sideEffectFn = NULL,
    },
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_READ_PARAM)] = {
        .replyBaseId = P1010B_CAN_ACK_READ_PARAM,
        .mayCompleteSync = true,
        .sideEffectFn = NULL,
    },
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_STATE_CONTROL)] = {
        .replyBaseId = P1010B_CAN_ACK_STATE_CONTROL,
        .mayCompleteSync = true,
        .sideEffectFn = p1010b_internal_update_fault_state,
    },
    [P1010B_REPLY_DISPATCH_INDEX(P1010B_CAN_ACK_SAVE_PARAM)] = {
        .replyBaseId = P1010B_CAN_ACK_SAVE_PARAM,
        .mayCompleteSync = true,
        .sideEffectFn = NULL,
    },
};

/**
 * @brief 鎸?reply_base_id 鏌ユ壘鎺ユ敹鍒嗗彂琛ㄩ」
 * @return 鏈夋晥鍒嗗彂琛ㄩ」鎸囬拡锛涙湭鍛戒腑杩斿洖 `NULL`
 */
static const P1010BRxDispatchDescriptor_s *p1010b_internal_find_rx_dispatch_descriptor(uint8_t reply_base_id)
{
    int32_t dispatch_index;
    const P1010BRxDispatchDescriptor_s *dispatch_descriptor;

    dispatch_index = p1010b_internal_reply_base_to_dispatch_index(reply_base_id);
    if (dispatch_index < 0)
    {
        return NULL;
    }

    dispatch_descriptor = &g_p1010b_rx_dispatch_table[(uint32_t)dispatch_index];
    if (dispatch_descriptor->replyBaseId != reply_base_id)
    {
        return NULL;
    }
    return dispatch_descriptor;
}

/**
 * @brief CAN 杩囨护鍣ㄥ洖璋冿紙ISR 涓婁笅鏂囷級
 * @details
 * - ISR 璇诲嚭杩囨护鍣ㄦ秷鎭悗鐩存帴瀹屾垚鍗忚瑙ｆ瀽涓庣數鏈哄疄渚嬭矾鐢憋紱
 * - 鍙嶉/鏁呴殰/璇诲弬搴旂瓟鍧囧湪 ISR 鍐呮洿鏂扮姸鎬佸苟瑙﹀彂鍥炶皟銆?
 */
static void p1010b_internal_rx_callback(Device_t device, void *param, CanFilterHandle_t filter_handle, size_t message_count)
{
    P1010BBus_s *bus = (P1010BBus_s *)param;
    CanUserMsg_s rx_message;
    uint8_t payload[P1010B_CAN_DLC];

    if (!bus || !device || !bus->canDevice)
    {
        return;
    }

    rx_message.filterHandle = filter_handle;
    rx_message.userBuf = payload;
    for (size_t message_index = 0U; message_index < message_count; message_index++)
    {
        P1010BRawFrame_s frame;
        uint8_t motor_id;
        uint8_t reply_base_id;
        P1010BDriver_s *target_driver;

        if (device_read(device, 0, &rx_message, 1) == 0U)
        {
            break;
        }
        if (rx_message.dsc.idType != CAN_IDE_STD || rx_message.dsc.dataLen != P1010B_CAN_DLC)
        {
            bus->rxDroppedCount++;
            continue;
        }

        frame.canId = rx_message.dsc.id;
        memcpy(frame.payload, payload, sizeof(frame.payload));
        frame.dataLength = (uint8_t)rx_message.dsc.dataLen;
        frame.timestampMs = rx_message.dsc.timeStamp;

        motor_id = (uint8_t)(frame.canId & P1010B_REPLY_ID_LOW_MASK);
        if (!p1010b_internal_is_valid_motor_id(motor_id))
        {
            continue;
        }
        target_driver = bus->driverTable[motor_id];
        if (!target_driver)
        {
            continue;
        }
        reply_base_id = (uint8_t)(frame.canId & P1010B_REPLY_ID_MASK);
        {
            const P1010BRxDispatchDescriptor_s *dispatch_descriptor = p1010b_internal_find_rx_dispatch_descriptor(reply_base_id);
            if (dispatch_descriptor)
            {
                if (dispatch_descriptor->sideEffectFn)
                {
                    dispatch_descriptor->sideEffectFn(target_driver, &frame);
                }
                if (dispatch_descriptor->mayCompleteSync)
                {
                    p1010b_internal_try_complete_sync_transaction(bus, target_driver, &frame);
                }
            }
        }
        p1010b_internal_mark_rx_seen(target_driver, frame.timestampMs);
    }
}

/**
 * @brief 鍒濆鍖?P1010B 鎬荤嚎涓婁笅鏂?
 * @details
 * - 鍒嗛厤涓€涓繃婊ゅ櫒鐢ㄤ簬鏀跺寘锛?
 * - 棣栫増閲囩敤鎬荤嚎绾у叏鍖归厤锛岀敱 ISR 鎸?`motor_id` 鍋氫簩娆¤矾鐢便€?
 */
OmRet_e p1010b_bus_init(P1010BBus_s *bus, Device_t can_device)
{
    OmRet_e ret;
    CanFilterAllocArg_s filter_alloc_arg;

    if (!bus || !can_device)
    {
        return OM_ERROR_PARAM;
    }

    memset(bus, 0, sizeof(P1010BBus_s));
    bus->canDevice = can_device;

    memset(&filter_alloc_arg, 0, sizeof(filter_alloc_arg));
    /*
     * 棣栫増閲囩敤鎬荤嚎绾х粺涓€鏀跺寘锛岄┍鍔ㄤ晶鎸?low nibble 璺敱鍒板叿浣撶數鏈哄疄渚嬨€?
     * 鍥犳浣跨敤鍏ㄥ尮閰嶈繃婊ゅ櫒锛岄伩鍏嶅崟鐢垫満杩囨护瀵艰嚧鍚屾€荤嚎澶氱數鏈哄疄渚嬩笉鍙銆?
     */
    filter_alloc_arg.request =
        CAN_FILTER_REQUEST_INIT(CAN_FILTER_MODE_MASK, CAN_FILTER_ID_STD, 0U, 0U, p1010b_internal_rx_callback, bus);
    ret = device_ctrl(can_device, CAN_CMD_FILTER_ALLOC, &filter_alloc_arg);
    if (ret != OM_OK)
    {
        return ret;
    }
    bus->filterHandle = filter_alloc_arg.handle;
    bus->filterAllocated = true;

    return OM_OK;
}

/**
 * @brief 閲婃斁鎬荤嚎涓婁笅鏂囪祫婧?
 */
OmRet_e p1010b_bus_deinit(P1010BBus_s *bus)
{
    OmRet_e ret = OM_OK;

    if (!bus)
    {
        return OM_ERROR_PARAM;
    }
    for (uint8_t motor_id = P1010B_MOTOR_ID_MIN; motor_id <= P1010B_MOTOR_ID_MAX; motor_id++)
    {
        P1010BDriver_s *driver = bus->driverTable[motor_id];
        if (!driver)
        {
            continue;
        }
        completion_deinit(&driver->sync.completion);
    }
    if (bus->filterAllocated && bus->canDevice)
    {
        ret = device_ctrl(bus->canDevice, CAN_CMD_FILTER_FREE, &bus->filterHandle);
    }
    memset(bus, 0, sizeof(P1010BBus_s));
    return ret;
}

/**
 * @brief 娉ㄥ唽鐢垫満椹卞姩瀹炰緥鍒版€荤嚎
 * @note 鍚屼竴 `motor_id` 鍙厑璁哥粦瀹氫竴涓疄渚嬨€?
 */
OmRet_e p1010b_register(P1010BBus_s *bus, P1010BDriver_s *driver, const P1010BConfig_s *config)
{
    OmRet_e ret;
    uint8_t motor_id;

    if (!bus || !driver || !bus->canDevice || !bus->filterAllocated)
    {
        return OM_ERROR_PARAM;
    }

    memset(driver, 0, sizeof(P1010BDriver_s));
    driver->bus = bus;
    driver->config = P1010B_DEFAULT_CONFIG(P1010B_MOTOR_ID_MIN);
    if (config)
    {
        driver->config = *config;
    }
    if (!p1010b_internal_is_valid_motor_id(driver->config.motorId))
    {
        return OM_ERROR_PARAM;
    }
    motor_id = driver->config.motorId;
    if (bus->driverTable[motor_id] && bus->driverTable[motor_id] != driver)
    {
        return OM_ERR_CONFLICT;
    }

    driver->runtime.state = P1010B_STATE_DISABLED;
    driver->runtime.currentMode = driver->config.defaultMode;
    driver->runtime.targetScale = p1010b_internal_update_target_scale_cache(driver->runtime.currentMode);
    driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
    driver->runtime.activeReport = driver->config.activeReport;
    ret = completion_init(&driver->sync.completion);
    if (ret != OM_OK)
    {
        return ret;
    }
    bus->driverTable[motor_id] = driver;
    return OM_OK;
}

/**
 * @brief 璁剧疆浜嬩欢鍥炶皟鍑芥暟
 */
void p1010b_set_callbacks(P1010BDriver_s *driver, P1010BFeedbackCallback_t feedback_cb, P1010BFaultCallback_t fault_cb,
                          P1010BOnlineCallback_t online_cb, P1010BParamReadCallback_t param_read_cb)
{
    if (!driver)
    {
        return;
    }
    driver->callbacks.onFeedback = feedback_cb;
    driver->callbacks.onFaultChanged = fault_cb;
    driver->callbacks.onOnlineChanged = online_cb;
    driver->callbacks.onParamRead = param_read_cb;
}

/**
 * @brief 鍒ゆ柇杩愯妯″紡鏄惁鍚堟硶
 */
static bool p1010b_internal_is_mode_supported(P1010BMode_e mode)
{
    switch (mode)
    {
    case P1010B_MODE_OPEN_LOOP:
    case P1010B_MODE_CURRENT:
    case P1010B_MODE_SPEED:
    case P1010B_MODE_POSITION:
        return true;
    default:
        return false;
    }
}

/**
 * @brief 鏍￠獙璇锋眰 flags 鏄惁涓哄悎娉曚簩閫変竴妯″紡
 * @param flags 璇锋眰鏍囧織
 * @return `true` 鍚堟硶锛沗false` 闈炴硶
 */
static bool p1010b_internal_is_request_flags_valid(uint8_t flags)
{
    return (flags == (uint8_t)P1010B_REQUEST_FLAG_SYNC || flags == (uint8_t)P1010B_REQUEST_FLAG_ASYNC);
}

/**
 * @brief 瑙ｆ瀽璇锋眰瀹為檯鎵ц妯″紡锛圫YNC/ASYNC锛?
 * @details
 * 褰?`request->flags == NONE` 鏃讹紝鍥為€€浣跨敤鍛戒护鎻忚堪绗﹂粯璁?flags銆?
 */
static OmRet_e p1010b_internal_resolve_request_flags(const P1010BCommandDescriptor_s *descriptor, const P1010BRequest_s *request, uint8_t *resolved_flags)
{
    uint8_t request_flags;

    request_flags = request->flags;
    if (request_flags == (uint8_t)P1010B_REQUEST_FLAG_NONE)
    {
        request_flags = descriptor->defaultRequestFlags;
    }
    if (!p1010b_internal_is_request_flags_valid(request_flags))
    {
        return OM_ERROR_PARAM;
    }

    *resolved_flags = request_flags;
    return OM_OK;
}

/**
 * @brief 鎺у埗缁欏畾鍛戒护瀹堝崼
 * @details
 * 浠呭湪 `ENABLED` 涓旀棤鏁呴殰闂攣鏃跺厑璁哥粰瀹氥€?
 */
static OmRet_e p1010b_internal_guard_target(P1010BDriver_s *driver, const P1010BRequest_s *request)
{
    if (driver->runtime.state == P1010B_STATE_FAULT_LOCKED || driver->telemetry.faultState.faultCode != 0U)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_FAULT_LOCKED;
        return OM_ERROR;
    }
    if (driver->runtime.state != P1010B_STATE_ENABLED)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_NOT_ENABLED;
        return OM_ERROR_BUSY;
    }
    if (request->command == P1010B_COMMAND_SET_TARGET && driver->runtime.targetScale <= 0.0f)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_INVALID_TARGET;
        return OM_ERROR_PARAM;
    }
    return OM_OK;
}

/**
 * @brief 涓诲姩涓婃姤閰嶇疆鍛戒护瀹堝崼
 * @details
 * 浠呭厑璁稿湪 `DISABLED` 閰嶇疆鎬佷笅涓嬪彂锛屽苟瑕佹眰鍛ㄦ湡闈為浂銆?
 */
static OmRet_e p1010b_internal_guard_set_active_report(P1010BDriver_s *driver, const P1010BRequest_s *request)
{
    if (request->args.setActiveReport.config.periodMs == 0U)
    {
        return OM_ERROR_PARAM;
    }
    if (driver->runtime.state != P1010B_STATE_DISABLED)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_CONFIG_ONLY_WHEN_DISABLED;
        return OM_ERROR_BUSY;
    }
    return OM_OK;
}

/**
 * @brief 鍙傛暟鍐欏懡浠ゅ畧鍗?
 * @details
 * 棣栫増闄愬埗涓?Disabled 鎬佷笖鍙傛暟鍦ㄧ櫧鍚嶅崟涓€?
 */
static OmRet_e p1010b_internal_guard_write_parameter(P1010BDriver_s *driver, const P1010BRequest_s *request)
{
    if (driver->runtime.state != P1010B_STATE_DISABLED)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_CONFIG_ONLY_WHEN_DISABLED;
        return OM_ERROR_BUSY;
    }
    if (!p1010b_internal_is_parameter_whitelisted(request->args.writeParameter.parameterId))
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_PARAM_NOT_WHITELISTED;
        return OM_ERROR_NOT_SUPPORT;
    }
    return OM_OK;
}

/**
 * @brief 鍙傛暟璇诲懡浠ゅ畧鍗?
 * @note 鍙傛暟 ID 涓?0 瑙嗕负鏃犳晥璇锋眰銆?
 */
static OmRet_e p1010b_internal_guard_read_parameter(P1010BDriver_s *driver, const P1010BRequest_s *request)
{
    (void)driver;
    if (request->args.readParameter.parameterId == 0U)
    {
        return OM_ERROR_PARAM;
    }
    return OM_OK;
}

/**
 * @brief 鐘舵€佹帶鍒跺懡浠ゅ畧鍗?
 * @details
 * - 浠呮帴鍙?ENABLE/DISABLE锛?
 * - 鏁呴殰闂攣涓嬬姝?ENABLE銆?
 */
static OmRet_e p1010b_internal_guard_state_control(P1010BDriver_s *driver, const P1010BRequest_s *request)
{
    uint8_t state_command;

    state_command = request->args.stateControl.command;
    if (state_command != P1010B_STATE_CMD_ENABLE && state_command != P1010B_STATE_CMD_DISABLE)
    {
        return OM_ERROR_PARAM;
    }
    if (state_command == P1010B_STATE_CMD_ENABLE &&
        (driver->runtime.state == P1010B_STATE_FAULT_LOCKED || driver->telemetry.faultState.faultCode != 0U))
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_FAULT_LOCKED;
        return OM_ERROR;
    }
    return OM_OK;
}

/**
 * @brief 鍙傛暟淇濆瓨鍛戒护瀹堝崼
 * @note 浠呭厑璁?Disabled 鎬佹墽琛屻€?
 */
static OmRet_e p1010b_internal_guard_save_parameters(P1010BDriver_s *driver, const P1010BRequest_s *request)
{
    (void)request;
    if (driver->runtime.state != P1010B_STATE_DISABLED)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_CONFIG_ONLY_WHEN_DISABLED;
        return OM_ERROR_BUSY;
    }
    return OM_OK;
}

/**
 * @brief 妯″紡璁剧疆鍛戒护瀹堝崼
 * @note 浠呮敮鎸佸彈鎺фā寮忛泦鍚堬紝涓斾粎鍏佽 Disabled 鎬侀厤缃€?
 */
static OmRet_e p1010b_internal_guard_set_mode(P1010BDriver_s *driver, const P1010BRequest_s *request)
{
    if (!p1010b_internal_is_mode_supported(request->args.setMode.mode))
    {
        return OM_ERROR_PARAM;
    }
    if (driver->runtime.state != P1010B_STATE_DISABLED)
    {
        driver->runtime.lastRejectReason = P1010B_REJECT_CONFIG_ONLY_WHEN_DISABLED;
        return OM_ERROR_BUSY;
    }
    return OM_OK;
}

/**
 * @brief 鎵撳寘鍙傛暟鍐欒浇鑽凤紙0x36锛?
 * @details 32 浣嶅弬鏁板€兼寜鍗忚瀹氫箟浠ヤ綆瀛楄妭鍦ㄥ墠鏂瑰紡缂栫爜鍒?payload[2..5]銆?
 */
static void p1010b_internal_encode_parameter_write_payload(uint8_t motor_id, uint8_t parameter_id, int32_t parameter_value,
                                                           uint8_t payload[P1010B_CAN_DLC])
{
    payload[0] = motor_id;
    payload[1] = parameter_id;
    payload[2] = (uint8_t)((uint32_t)parameter_value & 0xFFU);
    payload[3] = (uint8_t)(((uint32_t)parameter_value >> 8U) & 0xFFU);
    payload[4] = (uint8_t)(((uint32_t)parameter_value >> 16U) & 0xFFU);
    payload[5] = (uint8_t)(((uint32_t)parameter_value >> 24U) & 0xFFU);
}

/**
 * @brief 缂栫爜鍘熷缁欏畾甯э紙0x32/0x33锛?
 * @details
 * 鍏堝啓鍏?group 缂撳瓨锛屽啀閲嶆柊鎵撳寘鏁村抚锛屼繚璇佸悓缁勫鐢垫満缁欏畾涓€鑷淬€?
 */
static OmRet_e p1010b_internal_encode_target_raw_value(P1010BDriver_s *driver, int16_t raw_target, P1010BEncodedRequest_s *encoded)
{
    uint8_t group_index;
    uint8_t slot_index;

    group_index = p1010b_internal_get_group_index(driver->config.motorId);
    slot_index = p1010b_internal_get_slot_index(driver->config.motorId);
    encoded->requestCanId = (group_index == 0U) ? P1010B_CAN_CMD_DRIVE_GROUP_1_4 : P1010B_CAN_CMD_DRIVE_GROUP_5_8;
    driver->bus->groupTargetsRaw[group_index][slot_index] = raw_target;
    p1010b_internal_pack_group_payload(driver->bus, group_index, encoded->payload);
    return OM_OK;
}

/**
 * @brief 缂栫爜璇箟缁欏畾鍛戒护锛堣嚜鍔?scale锛?
 */
static OmRet_e p1010b_internal_encode_set_target(P1010BDriver_s *driver, const P1010BRequest_s *request,
                                                 P1010BEncodedRequest_s *encoded)
{
    int16_t raw_target;

    raw_target = p1010b_internal_scale_target(driver->runtime.targetScale, request->args.setTarget.targetValue);
    return p1010b_internal_encode_target_raw_value(driver, raw_target, encoded);
}

/**
 * @brief 缂栫爜杞欢澶嶄綅鍛戒护锛?x40锛?
 */
static OmRet_e p1010b_internal_encode_software_reset(P1010BDriver_s *driver, const P1010BRequest_s *request,
                                                     P1010BEncodedRequest_s *encoded)
{
    (void)driver;
    (void)request;
    encoded->payload[0] = 0x01U;
    return OM_OK;
}

/**
 * @brief 缂栫爜涓诲姩涓婃姤閰嶇疆鍛戒护锛?x34锛?
 */
static OmRet_e p1010b_internal_encode_set_active_report(P1010BDriver_s *driver, const P1010BRequest_s *request,
                                                        P1010BEncodedRequest_s *encoded)
{
    const P1010BActiveReportConfig_s *report_config;

    report_config = &request->args.setActiveReport.config;
    encoded->payload[0] = driver->config.motorId;
    encoded->payload[1] = report_config->enable ? 1U : 0U;
    encoded->payload[2] = report_config->periodMs;
    encoded->payload[3] = report_config->dataTypeSlots[0];
    encoded->payload[4] = report_config->dataTypeSlots[1];
    encoded->payload[5] = report_config->dataTypeSlots[2];
    encoded->payload[6] = report_config->dataTypeSlots[3];
    return OM_OK;
}

/**
 * @brief 缂栫爜涓诲姩鏌ヨ鍛戒护锛?x35锛?
 */
static OmRet_e p1010b_internal_encode_active_query(P1010BDriver_s *driver, const P1010BRequest_s *request,
                                                   P1010BEncodedRequest_s *encoded)
{
    (void)driver;
    encoded->payload[0] = request->args.activeQuery.dataTypeSlots[0];
    encoded->payload[1] = request->args.activeQuery.dataTypeSlots[1];
    encoded->payload[2] = request->args.activeQuery.dataTypeSlots[2];
    encoded->payload[3] = request->args.activeQuery.dataTypeSlots[3];
    return OM_OK;
}

/**
 * @brief 缂栫爜鍙傛暟鍐欏懡浠わ紙0x36锛?
 */
static OmRet_e p1010b_internal_encode_write_parameter(P1010BDriver_s *driver, const P1010BRequest_s *request,
                                                      P1010BEncodedRequest_s *encoded)
{
    p1010b_internal_encode_parameter_write_payload(driver->config.motorId, request->args.writeParameter.parameterId, request->args.writeParameter.parameterValue, encoded->payload);
    encoded->expectedParameterId = request->args.writeParameter.parameterId;
    return OM_OK;
}

/**
 * @brief 缂栫爜鍙傛暟璇诲懡浠わ紙0x37锛?
 */
static OmRet_e p1010b_internal_encode_read_parameter(P1010BDriver_s *driver, const P1010BRequest_s *request,
                                                     P1010BEncodedRequest_s *encoded)
{
    encoded->payload[0] = driver->config.motorId;
    encoded->payload[1] = request->args.readParameter.parameterId;
    encoded->expectedParameterId = request->args.readParameter.parameterId;
    return OM_OK;
}

/**
 * @brief 缂栫爜鐘舵€佹帶鍒跺懡浠わ紙0x38锛?
 * @details 鐘舵€佸懡浠ゅ啓鍏ヤ笌 motor_id 瀵瑰簲鐨勭粍妲戒綅銆?
 */
static OmRet_e p1010b_internal_encode_state_control(P1010BDriver_s *driver, const P1010BRequest_s *request,
                                                    P1010BEncodedRequest_s *encoded)
{
    uint8_t command_slot_index;

    command_slot_index = (uint8_t)(driver->config.motorId - 1U);
    encoded->payload[command_slot_index] = request->args.stateControl.command;
    encoded->expectedStateCommand = request->args.stateControl.command;
    return OM_OK;
}

/**
 * @brief 缂栫爜鍙傛暟淇濆瓨鍛戒护锛?x39锛?
 * @details
 * `saveParameters` 閲囩敤鈥滃懡浠?+ 鏁版嵁鈥濆弻灞傛ā鍨嬶細
 * - payload[0]锛氫繚瀛樺懡浠わ紱
 * - payload[1]锛氬綋鍓嶆暟鎹綅锛堢粷瀵归浂浣嶏級锛?
 * - payload[2..7]锛氫繚鐣欎綅锛堥鐣欐湭鏉ユ墿灞曪級銆?
 */
static OmRet_e p1010b_internal_encode_save_parameters(P1010BDriver_s *driver, const P1010BRequest_s *request,
                                                      P1010BEncodedRequest_s *encoded)
{
    uint8_t index;

    (void)driver;
    encoded->payload[0] = request->args.saveParameters.command;
    encoded->payload[1] = request->args.saveParameters.data.absoluteZero ? 1U : 0U;

    for (index = 0U; index < P1010B_SAVE_PARAM_RESERVED_BYTES; index++)
    {
        encoded->payload[index + 2U] = request->args.saveParameters.data.reservedPayload[index];
    }
    return OM_OK;
}

/**
 * @brief 缂栫爜妯″紡璁剧疆鍛戒护
 * @details 閫昏緫鍛戒护 `SET_MODE` 鏄犲皠涓哄啓鍙傛暟 `P1010B_PARAM_WORK_MODE`銆?
 */
static OmRet_e p1010b_internal_encode_set_mode(P1010BDriver_s *driver, const P1010BRequest_s *request, P1010BEncodedRequest_s *encoded)
{
    p1010b_internal_encode_parameter_write_payload(driver->config.motorId, P1010B_PARAM_WORK_MODE, (int32_t)request->args.setMode.mode, encoded->payload);
    encoded->expectedParameterId = P1010B_PARAM_WORK_MODE;
    return OM_OK;
}

/**
 * @brief 鍐欏弬搴旂瓟鍖归厤鏉′欢
 * @details 浠呯敤浜?`0x80` 搴旂瓟璺緞锛屾寜 parameter_id 绮剧‘鍖归厤銆?
 */
static bool p1010b_internal_ack_match_parameter(const P1010BDriver_s *driver, const P1010BRawFrame_s *frame)
{
    /* 浠呯敤浜?0x80 鍐欏弬搴旂瓟璺緞锛?x90 璇诲弬搴旂瓟涓嶆惡甯?parameter_id銆?*/
    return (frame->payload[1] == driver->sync.expectedParameterId);
}

/**
 * @brief 鐘舵€佹帶鍒跺簲绛斿尮閰嶆潯浠?
 * @details 鎸夊簲绛斿唴鍥炴樉鐨勭姸鎬佸懡浠ゅ瓧鍖归厤銆?
 */
static bool p1010b_internal_ack_match_state_control(const P1010BDriver_s *driver, const P1010BRawFrame_s *frame)
{
    return (frame->payload[2] == driver->sync.expectedStateCommand);
}

/**
 * @brief 瑙ｇ爜涓诲姩鏌ヨ搴旂瓟锛?x70锛?
 */
static void p1010b_internal_decode_ack_active_query(P1010BDriver_s *driver, const P1010BRawFrame_s *frame, P1010BResponse_s *response,
                                                    P1010BIsrCallbackContext_s *callback_context)
{
    (void)driver;
    (void)callback_context;
    for (uint8_t index = 0U; index < 4U; index++)
        response->data.activeQueryValues[index] = p1010b_internal_read_be_i16(&frame->payload[index * 2U]);
}

/**
 * @brief 瑙ｇ爜鍙傛暟璇诲彇搴旂瓟锛?x90锛?
 * @details
 * 璇诲弬搴旂瓟涓嶆惡甯?parameter_id锛屼娇鐢ㄥ悓姝ヤ簨鍔′笂涓嬫枃鍥炲～ parameter_id銆?
 */
static void p1010b_internal_decode_ack_read_parameter(P1010BDriver_s *driver, const P1010BRawFrame_s *frame, P1010BResponse_s *response,
                                                      P1010BIsrCallbackContext_s *callback_context)
{
    int32_t parameter_value;

    parameter_value = p1010b_internal_read_be_i32(&frame->payload[1]);
    response->data.readParameter.parameterId = driver->sync.expectedParameterId;
    response->data.readParameter.parameterValue = parameter_value;
    callback_context->triggerParamReadCallback = true;
    callback_context->callbackParameterId = driver->sync.expectedParameterId;
    callback_context->callbackParameterValue = parameter_value;
}

/**
 * @brief 瑙ｇ爜鐘舵€佹帶鍒跺簲绛旓紙0xA0锛?
 */
static void p1010b_internal_decode_ack_state_control(P1010BDriver_s *driver, const P1010BRawFrame_s *frame, P1010BResponse_s *response,
                                                     P1010BIsrCallbackContext_s *callback_context)
{
    (void)callback_context;
    response->data.stateControl.stateCommand = frame->payload[2];
    response->data.stateControl.faultState = driver->telemetry.faultState;
}

/**
 * @brief 閫氱敤鍚庡鐞嗭細鎴愬姛鍚庢竻闄ゆ嫆缁濆師鍥?
 */
static void p1010b_internal_post_commit_clear_reject_on_success(P1010BDriver_s *driver, const P1010BRequest_s *request, OmRet_e result,
                                                                const P1010BResponse_s *response)
{
    (void)request;
    (void)response;
    if (result == OM_OK)
        driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
}

/**
 * @brief 涓诲姩涓婃姤閰嶇疆鍚庡鐞?
 * @details 鍚屾浜嬪姟鎴愬姛鍚庡埛鏂?driver 渚х敓鏁堥厤缃紦瀛樸€?
 */
static void p1010b_internal_post_commit_set_active_report(P1010BDriver_s *driver, const P1010BRequest_s *request, OmRet_e result,
                                                          const P1010BResponse_s *response)
{
    (void)response;
    if (result != OM_OK)
        return;
    driver->runtime.activeReport = request->args.setActiveReport.config;
    driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
}

/**
 * @brief 妯″紡璁剧疆鍚庡鐞?
 * @details 鎴愬姛鍚庢洿鏂?`currentMode` 涓?`targetScale` 缂撳瓨銆?
 */
static void p1010b_internal_post_commit_set_mode(P1010BDriver_s *driver, const P1010BRequest_s *request, OmRet_e result,
                                                 const P1010BResponse_s *response)
{
    (void)response;
    if (result != OM_OK)
        return;
    driver->runtime.currentMode = request->args.setMode.mode;
    driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
    driver->runtime.targetScale = p1010b_internal_update_target_scale_cache(driver->runtime.currentMode);
}

/**
 * @brief 鐘舵€佹帶鍒跺悗澶勭悊
 * @details
 * - ENABLE 鎴愬姛鍚庤嫢浠嶆湁 faultCode锛屽垯缁存寔鏁呴殰闂攣锛?
 * - DISABLE 鎴愬姛鍚庝緷鎹?faultCode 鍐冲畾 Disabled 鎴?FaultLocked銆?
 */
static void p1010b_internal_post_commit_state_control(P1010BDriver_s *driver, const P1010BRequest_s *request, OmRet_e result,
                                                      const P1010BResponse_s *response)
{
    uint8_t state_command;

    (void)response;
    if (result != OM_OK)
        return;

    state_command = request->args.stateControl.command;
    if (state_command == P1010B_STATE_CMD_ENABLE)
    {
        if (driver->telemetry.faultState.faultCode != 0U)
        {
            driver->runtime.state = P1010B_STATE_FAULT_LOCKED;
            driver->runtime.lastRejectReason = P1010B_REJECT_FAULT_LOCKED;
            return;
        }
        driver->runtime.state = P1010B_STATE_ENABLED;
        driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
        return;
    }

    driver->runtime.state = (driver->telemetry.faultState.faultCode == 0U) ? P1010B_STATE_DISABLED : P1010B_STATE_FAULT_LOCKED;
    driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
}

/**
 * @brief 杞欢澶嶄綅鍚庡鐞?
 * @details 鎴愬姛鍚庡洖鍒?Disabled锛屽苟娓呴櫎鍦ㄧ嚎鏍囪绛夊緟鍚庣画鎶ユ枃閲嶅缓鍦ㄧ嚎鎬併€?
 */
static void p1010b_internal_post_commit_software_reset(P1010BDriver_s *driver, const P1010BRequest_s *request, OmRet_e result,
                                                       const P1010BResponse_s *response)
{
    (void)request;
    (void)response;
    if (result != OM_OK)
        return;
    driver->runtime.state = P1010B_STATE_DISABLED;
    driver->runtime.lastRejectReason = P1010B_REJECT_NONE;
    p1010b_internal_mark_online(driver, false);
}

/**
 * @brief 鍛戒护鎻忚堪绗﹁〃
 * @details
 * 浠?`P1010BCommand_e` 浣滀负绾挎€х储寮曪紝缁熶竴缁存姢瀹堝崼/缂栫爜/鍖归厤/瑙ｇ爜/鍚庡鐞嗙瓥鐣ャ€?
 */
static const P1010BCommandDescriptor_s g_p1010b_command_descriptors[P1010B_COMMAND_SOFTWARE_RESET + 1U] = {
    [P1010B_COMMAND_SET_ACTIVE_REPORT] = {
        .command = P1010B_COMMAND_SET_ACTIVE_REPORT,
        .requestCanId = P1010B_CAN_CMD_SET_REPORT_MODE,
        .ackBaseId = P1010B_CAN_ACK_REPORT_MODE,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = false,
        .stateGuardFn = p1010b_internal_guard_set_active_report,
        .encodeFn = p1010b_internal_encode_set_active_report,
        .ackMatchFn = NULL,
        .decodeAckFn = NULL,
        .postCommitFn = p1010b_internal_post_commit_set_active_report,
    },
    [P1010B_COMMAND_ACTIVE_QUERY] = {
        .command = P1010B_COMMAND_ACTIVE_QUERY,
        .requestCanId = P1010B_CAN_CMD_ACTIVE_QUERY,
        .ackBaseId = P1010B_CAN_ACK_ACTIVE_QUERY,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = false,
        .stateGuardFn = NULL,
        .encodeFn = p1010b_internal_encode_active_query,
        .ackMatchFn = NULL,
        .decodeAckFn = p1010b_internal_decode_ack_active_query,
        .postCommitFn = NULL,
    },
    [P1010B_COMMAND_WRITE_PARAMETER] = {
        .command = P1010B_COMMAND_WRITE_PARAMETER,
        .requestCanId = P1010B_CAN_CMD_WRITE_PARAM,
        .ackBaseId = P1010B_CAN_ACK_WRITE_PARAM,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = true,
        .stateGuardFn = p1010b_internal_guard_write_parameter,
        .encodeFn = p1010b_internal_encode_write_parameter,
        .ackMatchFn = p1010b_internal_ack_match_parameter,
        .decodeAckFn = NULL,
        .postCommitFn = p1010b_internal_post_commit_clear_reject_on_success,
    },
    [P1010B_COMMAND_READ_PARAMETER] = {
        .command = P1010B_COMMAND_READ_PARAMETER,
        .requestCanId = P1010B_CAN_CMD_READ_PARAM,
        .ackBaseId = P1010B_CAN_ACK_READ_PARAM,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = false,
        .stateGuardFn = p1010b_internal_guard_read_parameter,
        .encodeFn = p1010b_internal_encode_read_parameter,
        /*
         * 瑙勬牸涔?0x90 搴旂瓟 payload[1..4] 涓哄弬鏁板€奸珮鍒颁綆瀛楄妭锛?
         * 涓嶆惡甯?parameter_id锛屽洜姝よ鍙傚尮閰嶄粎渚濊禆 pendingCommand + ackBaseId銆?
         */
        .ackMatchFn = NULL,
        .decodeAckFn = p1010b_internal_decode_ack_read_parameter,
        .postCommitFn = NULL,
    },
    [P1010B_COMMAND_STATE_CONTROL] = {
        .command = P1010B_COMMAND_STATE_CONTROL,
        .requestCanId = P1010B_CAN_CMD_STATE_CONTROL,
        .ackBaseId = P1010B_CAN_ACK_STATE_CONTROL,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = false,
        .stateGuardFn = p1010b_internal_guard_state_control,
        .encodeFn = p1010b_internal_encode_state_control,
        .ackMatchFn = p1010b_internal_ack_match_state_control,
        .decodeAckFn = p1010b_internal_decode_ack_state_control,
        .postCommitFn = p1010b_internal_post_commit_state_control,
    },
    [P1010B_COMMAND_SAVE_PARAMETERS] = {
        .command = P1010B_COMMAND_SAVE_PARAMETERS,
        .requestCanId = P1010B_CAN_CMD_SAVE_PARAM,
        .ackBaseId = P1010B_CAN_ACK_SAVE_PARAM,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = false,
        .stateGuardFn = p1010b_internal_guard_save_parameters,
        .encodeFn = p1010b_internal_encode_save_parameters,
        .ackMatchFn = NULL,
        .decodeAckFn = NULL,
        .postCommitFn = p1010b_internal_post_commit_clear_reject_on_success,
    },
    [P1010B_COMMAND_SET_MODE] = {
        .command = P1010B_COMMAND_SET_MODE,
        .requestCanId = P1010B_CAN_CMD_WRITE_PARAM,
        .ackBaseId = P1010B_CAN_ACK_WRITE_PARAM,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_SYNC,
        .useConfiguringState = true,
        .stateGuardFn = p1010b_internal_guard_set_mode,
        .encodeFn = p1010b_internal_encode_set_mode,
        .ackMatchFn = p1010b_internal_ack_match_parameter,
        .decodeAckFn = NULL,
        .postCommitFn = p1010b_internal_post_commit_set_mode,
    },
    [P1010B_COMMAND_SET_TARGET] = {
        .command = P1010B_COMMAND_SET_TARGET,
        .requestCanId = 0U,
        .ackBaseId = P1010B_CAN_ACK_NONE,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_ASYNC,
        .useConfiguringState = false,
        .stateGuardFn = p1010b_internal_guard_target,
        .encodeFn = p1010b_internal_encode_set_target,
        .ackMatchFn = NULL,
        .decodeAckFn = NULL,
        .postCommitFn = p1010b_internal_post_commit_clear_reject_on_success,
    },
    [P1010B_COMMAND_SOFTWARE_RESET] = {
        .command = P1010B_COMMAND_SOFTWARE_RESET,
        .requestCanId = P1010B_CAN_CMD_SOFTWARE_RESET,
        .ackBaseId = P1010B_CAN_ACK_NONE,
        .defaultRequestFlags = (uint8_t)P1010B_REQUEST_FLAG_ASYNC,
        .useConfiguringState = false,
        .stateGuardFn = NULL,
        .encodeFn = p1010b_internal_encode_software_reset,
        .ackMatchFn = NULL,
        .decodeAckFn = NULL,
        .postCommitFn = p1010b_internal_post_commit_software_reset,
    },
};

/**
 * @brief 鎸夊懡浠ゅ瓧鏌ユ壘鎻忚堪绗?
 * @return 鏈夋晥鎻忚堪绗︽寚閽堬紱鍛戒护闈炴硶鎴栨弿杩扮鏈厤缃繑鍥?`NULL`
 */
static const P1010BCommandDescriptor_s *p1010b_internal_find_command_descriptor(P1010BCommand_e command)
{
    const P1010BCommandDescriptor_s *descriptor;
    if (command <= P1010B_COMMAND_NONE || command > P1010B_COMMAND_SOFTWARE_RESET)
        return NULL;
    descriptor = &g_p1010b_command_descriptors[(uint32_t)command];
    if (descriptor->command != command)
        return NULL;
    if (!descriptor->encodeFn)
        return NULL;
    return descriptor;
}

/**
 * @brief 缁熶竴璇锋眰鎵ц鍏ュ彛
 * @details
 * 鎵ц閾捐矾锛氬弬鏁版鏌?-> flags 瑙ｆ瀽 -> 鐘舵€佸畧鍗?-> 缂栫爜 -> 鍙戦€?->
 * 锛堝悓姝ヨ矾寰勶級绛夊緟搴旂瓟 -> 鍚庡鐞嗐€?
 */
static OmRet_e p1010b_internal_execute_request(P1010BDriver_s *driver, const P1010BRequest_s *request, P1010BResponse_s *response, P1010BWaitMode_e wait_mode)
{
    const P1010BCommandDescriptor_s *descriptor = NULL;
    P1010BEncodedRequest_s encoded = {0};
    OmRet_e ret = OM_ERROR_PARAM;
    uint8_t request_flags = (uint8_t)P1010B_REQUEST_FLAG_NONE;
    bool config_state_entered = false;
    P1010BState_e previous_state = P1010B_STATE_DISABLED;

    if (!driver || !request || !driver->bus || !driver->bus->canDevice)
        return OM_ERROR_PARAM;

    if (response)
        p1010b_internal_init_response(response, request->command, OM_ERROR_PARAM);

    descriptor = p1010b_internal_find_command_descriptor(request->command);
    if (!descriptor)
    {
        ret = OM_ERROR_NOT_SUPPORT;
        goto execute_finish;
    }

    ret = p1010b_internal_resolve_request_flags(descriptor, request, &request_flags);
    if (ret != OM_OK)
        goto execute_finish;
    if (wait_mode == P1010B_WAIT_MODE_SYNC && request_flags != (uint8_t)P1010B_REQUEST_FLAG_SYNC)
    {
        ret = OM_ERROR_NOT_SUPPORT;
        goto execute_finish;
    }
    if (wait_mode == P1010B_WAIT_MODE_ASYNC && request_flags != (uint8_t)P1010B_REQUEST_FLAG_ASYNC)
    {
        ret = OM_ERROR_NOT_SUPPORT;
        goto execute_finish;
    }

    if (descriptor->stateGuardFn)
    {
        ret = descriptor->stateGuardFn(driver, request);
        if (ret != OM_OK)
            goto execute_finish;
    }

    encoded.requestCanId = descriptor->requestCanId;
    encoded.ackBaseId = descriptor->ackBaseId;
    ret = descriptor->encodeFn(driver, request, &encoded);
    if (ret != OM_OK)
        goto execute_finish;

    /* 寮傛璺緞锛氫粎鍙戦€佷笉绛夊緟銆?*/
    if (request_flags == (uint8_t)P1010B_REQUEST_FLAG_ASYNC)
    {
        ret = p1010b_internal_send_can_frame(driver->bus, encoded.requestCanId, encoded.payload);
        if (ret == OM_OK)
            driver->telemetry.lastSuccessTxTimestampMs = osal_time_now_monotonic();
        goto execute_finish;
    }

    /* 鍚屾璺緞锛氬彲閫夎繘鍏?CONFIGURING 鐬€侊紝绛夊緟 ISR 瀹屾垚浜嬪姟銆?*/
    if (descriptor->useConfiguringState)
    {
        previous_state = driver->runtime.state;
        driver->runtime.state = P1010B_STATE_CONFIGURING;
        config_state_entered = true;
    }

    ret = p1010b_internal_send_sync_transaction(driver, request, &encoded, response);
    if (config_state_entered)
        driver->runtime.state = previous_state;

execute_finish:
    /* 缁熶竴鍐欏洖缁撴灉锛岄伩鍏嶄笉鍚岃繑鍥炲垎鏀紡濉?response銆?*/
    if (response)
        response->result = ret;
    if (descriptor && descriptor->postCommitFn)
        descriptor->postCommitFn(driver, request, ret, response);
    return ret;
}

/**
 * @brief 寮傛鎻愪氦璇锋眰锛堝彂閫佸嵆杩斿洖锛?
 */
OmRet_e p1010b_submit(P1010BDriver_s *driver, const P1010BRequest_s *request)
{
    return p1010b_internal_execute_request(driver, request, NULL, P1010B_WAIT_MODE_ASYNC);
}

/**
 * @brief 鍚屾鎻愪氦璇锋眰锛堢瓑寰呭簲绛旓級
 */
OmRet_e p1010b_request_sync(P1010BDriver_s *driver, const P1010BRequest_s *request, P1010BResponse_s *response)
{
    if (!response)
        return OM_ERROR_PARAM;
    return p1010b_internal_execute_request(driver, request, response, P1010B_WAIT_MODE_SYNC);
}

/**
 * @brief 琛屼负 API锛氭墽琛屽悓姝ヨ姹傦紙鍏佽 response 涓虹┖锛?
 */
static OmRet_e p1010b_internal_execute_sync_behavior(P1010BDriver_s *driver, P1010BRequest_s request, uint32_t timeout_ms,
                                                     P1010BResponse_s *response)
{
    P1010BResponse_s local_response = {0};

    request.timeoutMs = timeout_ms;
    if (response != NULL)
    {
        return p1010b_request_sync(driver, &request, response);
    }
    return p1010b_request_sync(driver, &request, &local_response);
}

/**
 * @brief 琛屼负 API锛氭墽琛屽紓姝ヨ姹?
 */
static OmRet_e p1010b_internal_execute_async_behavior(P1010BDriver_s *driver, P1010BRequest_s request)
{
    return p1010b_submit(driver, &request);
}

/**
 * @brief 澶辫兘鐢垫満锛堟瀯閫?鎵ц涓€浣擄紝鍚屾锛?
 */
OmRet_e p1010b_disable(P1010BDriver_s *driver, uint32_t timeout_ms, P1010BResponse_s *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_disable(), timeout_ms, response);
}

/**
 * @brief 浣胯兘鐢垫満锛堟瀯閫?鎵ц涓€浣擄紝鍚屾锛?
 */
OmRet_e p1010b_enable(P1010BDriver_s *driver, uint32_t timeout_ms, P1010BResponse_s *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_enable(), timeout_ms, response);
}

/**
 * @brief 璁剧疆宸ヤ綔妯″紡锛堟瀯閫?鎵ц涓€浣擄紝鍚屾锛?
 */
OmRet_e p1010b_set_mode(P1010BDriver_s *driver, P1010BMode_e mode, uint32_t timeout_ms, P1010BResponse_s *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_set_mode(mode), timeout_ms, response);
}

/**
 * @brief 璁剧疆涓诲姩涓婃姤閰嶇疆锛堟瀯閫?鎵ц涓€浣擄紝鍚屾锛?
 */
OmRet_e p1010b_set_active_report(P1010BDriver_s *driver, const P1010BActiveReportConfig_s *config, uint32_t timeout_ms, P1010BResponse_s *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_set_active_report(config), timeout_ms, response);
}

/**
 * @brief 涓诲姩鏌ヨ鎸囧畾 4 妲芥暟鎹紙鏋勯€?鎵ц涓€浣擄紝鍚屾锛?
 */
OmRet_e p1010b_active_query_slots(P1010BDriver_s *driver, P1010BReportDataType_e slot0, P1010BReportDataType_e slot1,
                                  P1010BReportDataType_e slot2, P1010BReportDataType_e slot3, uint32_t timeout_ms,
                                  P1010BResponse_s *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_active_query_slots(slot0, slot1, slot2, slot3), timeout_ms,
                                                 response);
}

/**
 * @brief 鍐欏弬鏁帮紙鏋勯€?鎵ц涓€浣擄紝鍚屾锛?
 */
OmRet_e p1010b_write_parameter(P1010BDriver_s *driver, uint8_t parameter_id, int32_t parameter_value, uint32_t timeout_ms,
                               P1010BResponse_s *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_write_parameter(parameter_id, parameter_value), timeout_ms,
                                                 response);
}

/**
 * @brief 璇诲弬鏁帮紙鏋勯€?鎵ц涓€浣擄紝鍚屾锛?
 */
OmRet_e p1010b_read_parameter(P1010BDriver_s *driver, uint8_t parameter_id, uint32_t timeout_ms, P1010BResponse_s *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_read_parameter(parameter_id), timeout_ms, response);
}

/**
 * @brief 淇濆瓨鍙傛暟锛堟瀯閫?鎵ц涓€浣擄紝鍚屾锛?
 * @note 鏍规嵁瑙勬牸涔︼紝褰撳墠淇濆瓨鏁版嵁涓粎缁濆闆朵綅鍙€夈€?
 */
OmRet_e p1010b_save_parameters(P1010BDriver_s *driver, bool set_absolute_zero, uint32_t timeout_ms, P1010BResponse_s *response)
{
    return p1010b_internal_execute_sync_behavior(driver, p1010b_req_save_parameters(set_absolute_zero), timeout_ms, response);
}

/**
 * @brief 涓嬪彂鐩爣鍊硷紙鏋勯€?鎵ц涓€浣擄紝寮傛锛?
 */
OmRet_e p1010b_set_target(P1010BDriver_s *driver, float target_value)
{
    return p1010b_internal_execute_async_behavior(driver, p1010b_req_set_target(target_value));
}

/**
 * @brief 杞欢澶嶄綅锛堟瀯閫?鎵ц涓€浣擄紝寮傛锛?
 */
OmRet_e p1010b_software_reset(P1010BDriver_s *driver)
{
    return p1010b_internal_execute_async_behavior(driver, p1010b_req_software_reset());
}

/**
 * @brief 鑾峰彇鍙嶉蹇収
 */
const P1010BFeedback_s *p1010b_get_feedback(const P1010BDriver_s *driver)
{
    return driver ? &driver->telemetry.feedback : NULL;
}

/**
 * @brief 鑾峰彇鏁呴殰蹇収
 */
const P1010BFaultState_s *p1010b_get_fault_state(const P1010BDriver_s *driver)
{
    return driver ? &driver->telemetry.faultState : NULL;
}

/**
 * @brief 鑾峰彇褰撳墠鐘舵€佹満鐘舵€?
 */
P1010BState_e p1010b_get_state(const P1010BDriver_s *driver)
{
    if (!driver)
        return P1010B_STATE_DISABLED;
    return driver->runtime.state;
}

/**
 * @brief 鑾峰彇鏈€杩戜竴娆℃嫆缁濆師鍥?
 */
P1010BRejectReason_e p1010b_get_last_reject_reason(const P1010BDriver_s *driver)
{
    if (!driver)
        return P1010B_REJECT_NONE;
    return driver->runtime.lastRejectReason;
}

/**
 * @brief 鏌ヨ鍦ㄧ嚎鐘舵€?
 */
bool p1010b_is_online(const P1010BDriver_s *driver)
{
    if (!driver)
        return false;
    return driver->telemetry.online;
}

/**
 * @brief 鑾峰彇绾跨▼瑙ｆ瀽鏍煎紡涓㈠純璁℃暟锛堥潪鏍囧噯甯ф垨 DLC!=8锛?
 */
uint32_t p1010b_get_rx_drop_count(const P1010BBus_s *bus)
{
    if (!bus)
        return 0U;
    return bus->rxDroppedCount;
}

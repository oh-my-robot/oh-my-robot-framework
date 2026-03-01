#include "drivers/peripheral/serial/pal_serial_dev.h"
#include "osal/osal_core.h"
/*************************** PRIVATE MACRO ********************************************************/
#define SERIAL_IS_O_BLCK_TX(dev) (device_get_oparams(dev) & SERIAL_O_BLCK_TX)
#define SERIAL_IS_O_NBLCK_TX(dev) (device_get_oparams(dev) & SERIAL_O_NBLCK_TX)
#define SERIAL_IS_O_BLCK_RX(dev) (device_get_oparams(dev) & SERIAL_O_BLCK_RX)
#define SERIAL_IS_O_NBLCK_RX(dev) (device_get_oparams(dev) & SERIAL_O_NBLCK_RX)
/************************** PRIVATE FUNCTION *********************************************************/
static OmRet_e serial_ctrl(Device_t dev, size_t cmd, void *arg);

static void serial_fifo_set_wait_reason(SerialFifo_t fifo, SerialWaitWakeReason_e reason)
{
    if (!fifo)
        return;
    osal_irq_lock_task();
    fifo->waitReason = reason;
    osal_irq_unlock_task();
}

static SerialWaitWakeReason_e serial_fifo_get_and_clear_wait_reason(SerialFifo_t fifo)
{
    SerialWaitWakeReason_e reason = SERIAL_WAIT_WAKE_NONE;

    if (!fifo)
        return reason;

    osal_irq_lock_task();
    reason = fifo->waitReason;
    fifo->waitReason = SERIAL_WAIT_WAKE_NONE;
    osal_irq_unlock_task();
    return reason;
}

static OmRet_e serial_wait_completion_abortable(Device_t dev, SerialFifo_t fifo, uint32_t timeout_ms)
{
    OmRet_e wait_ret;
    SerialWaitWakeReason_e wake_reason;

    if (!dev || !fifo)
        return OM_ERROR_PARAM;

    wait_ret = completion_wait(&fifo->cpt, timeout_ms);
    if (wait_ret == OM_ERROR_TIMEOUT)
        return OM_ERROR_TIMEOUT;
    if (wait_ret != OM_OK)
        return wait_ret;

    if (device_check_status(dev, DEV_STATUS_SUSPEND))
        return OM_ERROR_BUSY;

    wake_reason = serial_fifo_get_and_clear_wait_reason(fifo);
    if (wake_reason == SERIAL_WAIT_WAKE_ABORT)
        return OM_ERROR_BUSY;
    if (wake_reason == SERIAL_WAIT_WAKE_ERROR)
        return OM_ERROR;
    return OM_OK;
}

static void serial_abort_blocking_waiters(HalSerial_t serial, SerialWaitWakeReason_e reason)
{
    SerialFifo_t tx_fifo;
    SerialFifo_t rx_fifo;

    if (!serial)
        return;

    tx_fifo = serial_get_txfifo(serial);
    rx_fifo = serial_get_rxfifo(serial);

    if (tx_fifo && SERIAL_IS_O_BLCK_TX(&serial->parent))
    {
        osal_irq_lock_task();
        tx_fifo->status = SERIAL_FIFO_IDLE;
        tx_fifo->loadSize = 0;
        tx_fifo->waitReason = reason;
        osal_irq_unlock_task();
        (void)completion_done(&tx_fifo->cpt);
        device_clr_status(&serial->parent, DEV_STATUS_BUSY_TX);
    }

    if (rx_fifo && SERIAL_IS_O_BLCK_RX(&serial->parent))
    {
        osal_irq_lock_task();
        rx_fifo->status = SERIAL_FIFO_IDLE;
        rx_fifo->loadSize = 0;
        rx_fifo->waitReason = reason;
        osal_irq_unlock_task();
        (void)completion_done(&rx_fifo->cpt);
        device_clr_status(&serial->parent, DEV_STATUS_BUSY_RX);
    }
}
/* 鏃燜IFO鎯呭喌涓嬬殑 TX 鍑芥暟*/
static size_t serial_tx_poll(Device_t dev, void *data, size_t len)
{
    size_t length;
    HalSerial_t serial = (HalSerial_t)dev;
    uint8_t *tx_data = (uint8_t *)data;
    if (device_check_status(dev, DEV_STATUS_BUSY_TX))
    {
        // TODO: log txbusy
        return 0;
    }
    device_set_status(dev, DEV_STATUS_BUSY_TX);
    for (length = 0; length < len; length++)
    {
        if (serial->interface->putByte(serial, tx_data[length]) != OM_OK) // 鍥哄畾璇箟/鍔熻兘
            break;
    }
    // if(length < len)
    // TODO: LOG 鍙戦€佽秴鏃讹紝鎵撳嵃瀹為檯鍙戦€佸瓧鑺?
    device_clr_status(dev, DEV_STATUS_BUSY_TX);
    return length;
}

static size_t serial_rx_poll(Device_t dev, uint8_t *buf, size_t len)
{
    size_t length;
    OmRet_e ret = OM_ERROR;
    HalSerial_t serial = (HalSerial_t)dev;
    if (device_check_status(dev, DEV_STATUS_BUSY_RX))
    {
        // TODO: log txbusy
        return 0;
    }
    device_set_status(dev, DEV_STATUS_BUSY_RX);
    for (length = 0; length < len; length++)
    {
        ret = serial->interface->getByte(serial, (buf++));
        if (ret != OM_OK)
            break;
    }
    // if(length < len)
    // TODO: LOG 鎺ユ敹瓒呮椂锛屾墦鍗板疄闄呮帴鏀跺瓧鑺?
    device_clr_status(dev, DEV_STATUS_BUSY_RX);
    return length;
}

/**
 * @brief 涓插彛闃诲鍙戦€?鍐呴儴妗嗘灦瀹炵幇
 *
 * @param dev 涓插彛璁惧
 * @param data 搴旂敤灞傛暟鎹寘鎸囬拡
 * @param len  搴旂敤灞傛暟鎹寘闀垮害
 * @return size_t 瀹為檯鍙戦€侀暱搴?
 * @note   璋冪敤鑰呴渶鑷纭繚data鐨勫唴瀛樺畨鍏ㄥ拰鏁版嵁瀹屾暣鎬?
 */
static size_t serial_tx_block(Device_t dev, void *data, size_t len)
{
    HalSerial_t serial;
    SerialFifo_t tx_fifo;
    OmRet_e wait_ret;
    size_t tx_start_len;
    size_t txlen;
    size_t linear_space_len;
    void *tx_linear_buf = 0; // 涓插彛绾挎€х紦瀛樺尯
    size_t sended = 0;       // 宸茬粡鍙戦€佺殑闀垮害

    serial = (HalSerial_t)dev;
    tx_fifo = serial_get_txfifo(serial);

    // 鐞嗚涓婂浜庝覆鍙ｏ紝瀵逛簬闃诲鍙戦€佹潵璇翠笉搴旇鍑虹幇BUSY_TX鐨勬儏鍐?
    // 鍥犱负鍚屼竴涓覆鍙ｏ紝鍚屼竴鏃堕棿浠呭厑璁歌涓€涓嚎绋嬫墦寮€
    device_set_status(dev, DEV_STATUS_BUSY_TX);
    do
    {
        if (device_check_status(dev, DEV_STATUS_SUSPEND))
            break;

        if (tx_fifo->status == SERIAL_FIFO_IDLE)
        {
            txlen = ringbuf_in(&tx_fifo->rb, data + sended, len - sended);
            if (txlen == 0U)
                break;

            linear_space_len = ringbuf_get_item_linear_space(&tx_fifo->rb, &tx_linear_buf);
            if (linear_space_len == 0U)
                break;

            tx_fifo->loadSize = linear_space_len;
            tx_fifo->status = SERIAL_FIFO_BUSY;
            serial_fifo_set_wait_reason(tx_fifo, SERIAL_WAIT_WAKE_NONE);
            tx_start_len = serial->interface->transmit(serial, tx_linear_buf, linear_space_len);
            if (tx_start_len == 0U)
            {
                tx_fifo->status = SERIAL_FIFO_IDLE;
                tx_fifo->loadSize = 0;
                serial_fifo_set_wait_reason(tx_fifo, SERIAL_WAIT_WAKE_ERROR);
                device_err_cb(dev, ERR_SERIAL_TX_TIMEOUT, linear_space_len);
                break;
            }

            wait_ret = serial_wait_completion_abortable(dev, tx_fifo, SERIAL_BLOCK_TX_WAIT_TIMEOUT_MS);
            if (wait_ret != OM_OK)
            {
                tx_fifo->status = SERIAL_FIFO_IDLE;
                tx_fifo->loadSize = 0;
                if (wait_ret == OM_ERROR_TIMEOUT)
                    device_err_cb(dev, ERR_SERIAL_TX_TIMEOUT, linear_space_len);
                break;
            }
            sended += txlen;
        }
    } while (sended < len);

    device_clr_status(dev, DEV_STATUS_BUSY_TX);
    return sended;
}

/* 涓插彛闈為樆濉炲彂閫?*/
static size_t serial_tx_nonblock(Device_t dev, void *data, size_t len)
{
    HalSerial_t serial;
    SerialFifo_t tx_fifo;
    size_t tx_len;
    size_t tx_start_len;
    size_t sended = 0;
    void *tx_ptr = NULL;

    serial = (HalSerial_t)dev;
    tx_fifo = serial_get_txfifo(serial);
    sended = ringbuf_in(&tx_fifo->rb, data, len);

    device_set_status(dev, DEV_STATUS_BUSY_TX);
    /* 鍙戦€佹暟鎹?*/
    if (tx_fifo->status == SERIAL_FIFO_IDLE && sended)
    {
        tx_len = ringbuf_get_item_linear_space(&tx_fifo->rb, &tx_ptr);
        tx_fifo->status = SERIAL_FIFO_BUSY;
        tx_fifo->loadSize = tx_len;
        if (tx_len > 0)
        {
            tx_start_len = serial->interface->transmit(serial, tx_ptr, tx_len);
            if (tx_start_len == 0U)
            {
                tx_fifo->status = SERIAL_FIFO_IDLE;
                tx_fifo->loadSize = 0;
                device_clr_status(dev, DEV_STATUS_BUSY_TX);
                device_err_cb(dev, ERR_SERIAL_TX_TIMEOUT, tx_len);
            }
        }
    }
    // 闈為樆濉炵殑serial_busy_tx鐘舵€佸湪涓柇涓竻闄?
    return sended;
}

static size_t serial_rx_block(Device_t dev, void *buf, size_t len)
{
    HalSerial_t serial;
    SerialFifo_t rx_fifo;
    OmRet_e wait_ret;
    size_t rd_remain;      // 鍓╀綑鏈鍙栫殑闀垮害
    size_t rd_len;         // 鏈疆璇诲彇鐨勯暱搴︼紝鐪熷疄鍙嶅簲纭欢瀹為檯鎺ユ敹瀛楄妭鏁帮紝涔熷氨鏄€冭檻rxfifo鍙兘婧㈠嚭鐨勬儏鍐?
    size_t rd_from_rb = 0; // 搴旂敤灞傞€氳繃read鎺ュ彛鑾峰緱鐨勬€婚暱搴︼紝涔熷氨鏄笉鍖呮嫭鎺ユ敹杩囩▼涓璻xfifo鍙兘婧㈠嚭鐨勬儏鍐?

    serial = (HalSerial_t)dev;
    rx_fifo = serial_get_rxfifo(serial);
    device_set_status(dev, DEV_STATUS_BUSY_RX);
    rd_len = ringbuf_len(&rx_fifo->rb);

    /* rd_len < len */
    rd_remain = len - rd_len; // 鍑忓幓宸茬粡鏈夌殑鏁版嵁闀垮害鎵嶆槸鍓╀綑闇€瑕佽鍙栫殑闀垮害
    rd_from_rb += ringbuf_out(&rx_fifo->rb, buf, rd_len);
    do
    {
        if (device_check_status(dev, DEV_STATUS_SUSPEND))
        {
            wait_ret = OM_ERROR_BUSY;
            break;
        }

        osal_irq_lock_task();
        rx_fifo->loadSize = (rd_remain > ringbuf_avail(&rx_fifo->rb)) ? ringbuf_avail(&rx_fifo->rb) : rd_remain;
        rd_len = rx_fifo->loadSize;
        rx_fifo->status = SERIAL_FIFO_BUSY; // FIFO寮€濮嬬瓑寰呮暟鎹?
        rx_fifo->waitReason = SERIAL_WAIT_WAKE_NONE;
        osal_irq_unlock_task();
        wait_ret = serial_wait_completion_abortable(dev, rx_fifo, SERIAL_BLOCK_RX_WAIT_TIMEOUT_MS); // 绛夊緟鏁版嵁鍒版潵
        rx_fifo->status = SERIAL_FIFO_IDLE;                                                         // FIFO缁撴潫鏁版嵁绛夊緟
        if (wait_ret != OM_OK)
        {
            if (wait_ret == OM_ERROR_TIMEOUT)
                device_err_cb(dev, ERR_SERIAL_RX_TIMEOUT, rd_remain);
            break;
        }

        rd_len = (ringbuf_len(&rx_fifo->rb) < rd_len) ? ringbuf_len(&rx_fifo->rb) : rd_len;
        if (rd_len == 0u)
        {
            wait_ret = OM_ERROR_TIMEOUT;
            device_err_cb(dev, ERR_SERIAL_RX_TIMEOUT, rd_remain);
            break;
        }
        // rd_total浠呰€冭檻閫氳繃API璋冪敤鑾峰緱鐨勬暟鎹紝杩欐槸涓轰簡璁╁簲鐢ㄥ眰鐭ラ亾閫氳繃API瀹為檯璇诲彇鐨勫瓧鑺傛暟
        rd_from_rb += ringbuf_out(&rx_fifo->rb, buf + rd_from_rb, rd_len);

        if (rx_fifo->loadSize < 0) // loadSize < 0 璇存槑鍑虹幇浜咶IFO婧㈠嚭閿欒
        {
            osal_irq_lock_task();
            // 璐熻礋寰楁锛屽疄闄呬笂鏄畻涓婁簡婧㈠嚭鐨勬暟鎹暱搴?妗嗘灦灞備笉鍏冲績搴旂敤灞傛槸鍚﹀鐞嗘孩鍑猴紝鍙槸璇氬疄鍦拌褰曠‖浠跺疄闄呮帴鏀惰繃鐨勫瓧鑺傛暟)
            rd_len -= rx_fifo->loadSize;
            // 鑷充簬婧㈠嚭鐨勬暟鎹€庝箞鍔烇紵閭ｆ槸API浣跨敤鑰呰鑰冭檻鐨勪簨鎯咃紝瑕佷笉鎶妑b寮€澶т竴鐐癸紝瑕佷笉灏卞湪閿欒鍥炶皟涓仛鐗规畩澶勭悊
            rx_fifo->loadSize = 0;
            osal_irq_unlock_task();
        }
        /*
            loadSize鍖呭惈rxfifo婧㈠嚭鐨勪俊鎭紝娌℃硶纭畾绌剁珶婧㈠嚭浜嗗灏戯紝鍥犳rd_len鍙兘浼氬ぇ浜巖d_remain
            褰搑d_len>=rd_remain鏃讹紝rd_remain鐩存帴褰掗浂鍗冲彲锛?
        */
        rd_remain = (rd_remain > rd_len) ? (rd_remain - rd_len) : 0;
    } while (rd_remain);
    device_clr_status(dev, DEV_STATUS_BUSY_RX);
    return rd_from_rb;
}

static size_t serial_rx_nonblock(Device_t dev, void *buf, size_t len)
{
    HalSerial_t serial;
    SerialFifo_t rx_fifo;
    size_t rd_len;

    serial = (HalSerial_t)dev;
    rx_fifo = serial_get_rxfifo(serial);

    if (ringbuf_len(&rx_fifo->rb) >= len)
        rd_len = ringbuf_out(&rx_fifo->rb, buf, len);
    else
        rd_len = 0;
    return rd_len;
}

static OmRet_e serial_set_cfg(HalSerial_t serial, SerialCfg_t cfg)
{
    OmRet_e ret;

    if (!serial || !cfg)
        return OM_ERROR_PARAM;

    if (cfg->txBufSize != serial->cfg.txBufSize || cfg->rxBufSize != serial->cfg.rxBufSize)
        return OM_ERROR_PARAM;

    ret = serial->interface->configure(serial, cfg);
    if (ret == OM_OK)
        serial->cfg = *cfg;
    // TODO: LOG
    return ret;
}

// TODO: 寰呭疄鐜?
static OmRet_e serial_flush(HalSerial_t serial, uint32_t fifo_selector)
{
    switch (fifo_selector)
    {
    case SERIAL_TXFLUSH: {
    }
    break;
    case SERIAL_RXFLUSH: {
    }
    break;
    case SERIAL_TXRXFLUSH:
        serial_flush(serial, SERIAL_TXFLUSH);
        serial_flush(serial, SERIAL_RXFLUSH);
        break;
    }
    return OM_OK;
}

static OmRet_e serial_tx_enable(Device_t dev)
{
    HalSerial_t serial;
    SerialFifo_t tx_fifo;
    size_t ctrl_arg = 0;

    serial = (HalSerial_t)dev;
    tx_fifo = serial_get_txfifo(serial);
    // 鏃犵紦瀛?
    if (serial->cfg.txBufSize == 0)
    {
        // 涓插彛鏃犵紦瀛樺彧鑳戒娇鐢╬oll鏂瑰紡
        return OM_OK;
    }

    /* 搴曞眰鏈～鍏?FIFO */
    if (!tx_fifo)
    {
        /*  褰撳墠妗嗘灦涓嬶紝闄や簡poll涔嬪鎵€鏈夊彂閫佹柟寮忛兘浼氶厤缃畆b锛屾棤璁洪樆濉為潪闃诲銆?
            鍚庣画鑻ユ槸鍙戠幇涓嶅悎閫傜殑璇濆彲浠ュ啀缁嗗垎锛岀洰鍓嶆殏涓嶆敮鎸併€?
            浣嗘槸鏃犺濡備綍锛屽缓璁潪闃诲鍙戦€侀兘浣跨敤rb銆?
        */
        if (serial->cfg.txBufSize < SERIAL_MIN_TX_BUFSZ) // TODO: log
            serial->cfg.txBufSize = SERIAL_MIN_TX_BUFSZ;

        tx_fifo = (SerialFifo_t)osal_malloc(sizeof(SerialFifo_s));
        if (!tx_fifo)
            return OM_ERROR_MEMORY;
        if (!ringbuf_alloc(&tx_fifo->rb, sizeof(uint8_t), serial->cfg.txBufSize, osal_malloc) || !tx_fifo->rb.buf)
        {
            osal_free(tx_fifo);
            return OM_ERROR_MEMORY;
        }
        tx_fifo->status = SERIAL_FIFO_IDLE;
        tx_fifo->loadSize = 0;
        tx_fifo->waitReason = SERIAL_WAIT_WAKE_NONE;
        serial->priv.txFifo = tx_fifo;
    }

    if (device_get_oparams(dev) & SERIAL_O_BLCK_TX)
    {
        if (completion_init(&tx_fifo->cpt) != OM_OK)
            return OM_ERROR_MEMORY;
    }

    ctrl_arg = device_get_regparams(dev) & DEVICE_REG_TXTYPE_MASK;

    serial->interface->control(serial, SERIAL_CMD_SET_IOTPYE, (void *)ctrl_arg);

    return OM_OK;
}

static OmRet_e serial_rx_enable(Device_t dev)
{
    HalSerial_t serial;
    SerialFifo_t rx_fifo;
    uint32_t ctrl_arg;

    ctrl_arg = device_get_regparams(dev) & DEVICE_REG_RXTYPE_MASK;

    serial = (HalSerial_t)dev;
    rx_fifo = serial_get_rxfifo(serial);

    if (serial->cfg.rxBufSize == 0)
    {
        // 鏃犵紦瀛橈紝涓插彛鍙兘杞璇诲彇
        return OM_OK;
    }

    if (!rx_fifo)
    {
        if (serial->cfg.rxBufSize < SERIAL_MIN_RX_BUFSZ)
            serial->cfg.rxBufSize = SERIAL_MIN_RX_BUFSZ;

        rx_fifo = (SerialFifo_t)osal_malloc(sizeof(SerialFifo_s));
        if (!rx_fifo)
            return OM_ERROR_MEMORY;
        if (!ringbuf_alloc(&rx_fifo->rb, sizeof(uint8_t), serial->cfg.rxBufSize, osal_malloc) || !rx_fifo->rb.buf)
        {
            osal_free(rx_fifo);
            return OM_ERROR_MEMORY;
        }
        rx_fifo->status = SERIAL_FIFO_IDLE;
        rx_fifo->loadSize = 0;
        rx_fifo->waitReason = SERIAL_WAIT_WAKE_NONE;
        serial->priv.rxFifo = rx_fifo;
    }
    if (device_get_oparams(dev) & SERIAL_O_BLCK_RX)
    {
        if (completion_init(&rx_fifo->cpt) != OM_OK)
            return OM_ERROR_MEMORY;
    }

    serial->interface->control(serial, SERIAL_CMD_SET_IOTPYE, (void *)ctrl_arg);
    return OM_OK;
}

/******************************************* DEVICE INTERFACE
 * ********************************************/
static OmRet_e serial_init(Device_t dev)
{
    HalSerial_t serial = (HalSerial_t)dev;
    if (!serial || !serial->interface || !serial->interface->configure)
        return OM_ERROR_PARAM;
    serial->priv.txFifo = NULL;
    serial->priv.rxFifo = NULL;
    return serial->interface->configure(serial, &serial->cfg);
}

static OmRet_e serial_open(Device_t dev, uint32_t otype)
{
    HalSerial_t serial = (HalSerial_t)dev;
    OmRet_e ret;
    if (!serial || !serial->interface || !serial->interface->control)
        return OM_ERROR_PARAM;

    /* 涓插彛鍐欐柟寮?*/
    dev->priv.oparams |= (otype & SERIAL_O_BLCK_TX) ? SERIAL_O_BLCK_TX : (otype & SERIAL_O_NBLCK_TX) ? SERIAL_O_NBLCK_TX
                                                                                                     : 0U;
    /* 涓插彛璇绘柟寮?*/
    dev->priv.oparams |= (otype & SERIAL_O_BLCK_RX) ? SERIAL_O_BLCK_RX : (otype & SERIAL_O_NBLCK_RX) ? SERIAL_O_NBLCK_RX
                                                                                                     : 0U;

    ret = serial_tx_enable(dev);
    if (ret != OM_OK)
        return ret;

    ret = serial_rx_enable(dev);
    if (ret != OM_OK)
        return ret;
    return OM_OK;
}

static OmRet_e serial_ctrl(Device_t dev, size_t cmd, void *arg)
{
    OmRet_e ret;
    HalSerial_t serial = (HalSerial_t)dev;
    if (!serial || !serial->interface || !serial->interface->control) // TODO: assert
        return OM_ERROR_PARAM;
    ret = OM_OK;
    switch (cmd)
    {
    case SERIAL_CMD_FLUSH: {
        uint32_t arg = (uint32_t)arg;
        ret = serial_flush(serial, arg);
    }
    break;

    case SERIAL_CMD_SET_CFG: {
        SerialCfg_t cfg;
        if (!arg)
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        cfg = (SerialCfg_t)arg;
        ret = serial_set_cfg(serial, cfg);
    }
    break;

    case SERIAL_CMD_SUSPEND:
        /*
         * 鍏堟爣璁版寕璧凤紝鍐嶄笅鍙戝簳灞傚仠姝紝鏈€鍚庡敜閱掗樆濉炵瓑寰呰€咃紝
         * 淇濊瘉闃诲璺緞涓嶄細姘镐箙绛夊緟銆?
         */
        device_set_status(&serial->parent, DEV_STATUS_SUSPEND);
        (void)serial->interface->control(serial, SERIAL_CMD_SUSPEND, arg);
        serial_abort_blocking_waiters(serial, SERIAL_WAIT_WAKE_ABORT);
        break;

    case SERIAL_CMD_RESUME: {
        uint32_t tx_type;
        uint32_t rx_type;

        ret = serial->interface->control(serial, SERIAL_CMD_RESUME, arg);
        if (ret != OM_OK)
            break;

        tx_type = device_get_regparams(dev) & DEVICE_REG_TXTYPE_MASK;
        rx_type = device_get_regparams(dev) & DEVICE_REG_RXTYPE_MASK;

        if (tx_type != DEVICE_REG_POLL_TX)
            (void)serial->interface->control(serial, SERIAL_CMD_SET_IOTPYE, (void *)tx_type);
        if (rx_type != DEVICE_REG_POLL_RX)
            (void)serial->interface->control(serial, SERIAL_CMD_SET_IOTPYE, (void *)rx_type);

        device_clr_status(&serial->parent, DEV_STATUS_SUSPEND);
    }
    break;

    default:
        ret = serial->interface->control(serial, cmd, arg);
    }

    return ret;
}

static size_t serial_write(Device_t dev, void *pos, void *data, size_t len)
{
    SerialFifo_t txfifo;
    HalSerial_t serial;
    uint32_t oparams;
    size_t ret_len = 0;
    oparams = device_get_oparams(dev);
    serial = (HalSerial_t)dev;
    txfifo = serial_get_txfifo(serial);

    if (serial->cfg.txBufSize == 0 || ((device_get_regparams(dev) & DEVICE_REG_TXTYPE_MASK) == DEVICE_REG_POLL_TX))
    {
        return serial_tx_poll(dev, data, len);
    }
    else if (!txfifo || !txfifo->rb.buf)
    {
        // TODO: log no fifo
        return 0;
    }

    // 鑻ヤ箣鍓嶇殑鏁版嵁娌℃湁鍙戦€佸畬锛岀洿鎺ユ斁鍏IFO绛夊緟鍙戦€?
    if (device_check_status(dev, DEV_STATUS_BUSY_TX))
    {
        ret_len = ringbuf_in(&txfifo->rb, data, len);
    }
    else
    {
        if ((oparams & SERIAL_O_NBLCK_TX) || osal_is_in_isr()) // 鍦ㄤ腑鏂腑鍙兘浣跨敤闈為樆濉炲彂閫?
            ret_len = serial_tx_nonblock(dev, data, len);
        else if (oparams & SERIAL_O_BLCK_TX)
            ret_len = serial_tx_block(dev, data, len);
    }
    // 鑻ユ槸鍙戦€佹暟鎹皬浜庢€婚暱搴︼紝鍒欒涓篢XFIFO婧㈠嚭锛岃Е鍙戝洖璋冨嚱鏁?
    // 浠庢満鍒朵笂鏉ヨ锛岄樆濉炲彂閫佷笉浼氬嚭鐜癟XFIFO婧㈠嚭鐨勬儏鍐碉紝闈為樆濉炲彂閫佸垯鍙兘鐢变簬FIFO涓嶅鑰岀洿鎺ラ€€鍑哄彂閫?
    if (ret_len < len)
        device_err_cb(dev, ERR_SERIAL_TXFIFO_OVERFLOW, len - ret_len);
    return ret_len;
}

static size_t serial_read(Device_t dev, void *pos, void *buf, size_t len)
{
    HalSerial_t serial;
    SerialFifo_t rx_fifo;
    size_t recv_len = 0;
    serial = (HalSerial_t)dev;
    rx_fifo = serial_get_rxfifo(serial);
    if (serial->cfg.rxBufSize == 0 || ((device_get_regparams(dev) & DEVICE_REG_RXTYPE_MASK) == DEVICE_REG_POLL_RX))
    {
        return serial_rx_poll(dev, (uint8_t *)buf, len);
    }
    if (!rx_fifo || !rx_fifo->rb.buf)
        return 0;

    if (ringbuf_len(&rx_fifo->rb) >= len)
        recv_len = ringbuf_out(&rx_fifo->rb, buf, len);
    else if (SERIAL_IS_O_NBLCK_RX(dev) || osal_is_in_isr()) // 鍦ㄤ腑鏂腑鍙兘浣跨敤闈為樆濉炴帴鏀?
        recv_len = serial_rx_nonblock(dev, buf, len);
    else if (SERIAL_IS_O_BLCK_RX(dev))
        recv_len = serial_rx_block(dev, buf, len);

    return recv_len;
}

static DevInterface_s serial_interface = {
    .init = serial_init,
    .open = serial_open,
    .close = OM_NULL,       // 寰呭畬鎴?
    .control = serial_ctrl, // 寰呭畬鍠?
    .read = serial_read,
    .write = serial_write,
};

/******************************************* HW API *********************************************/
OmRet_e serial_register(HalSerial_t serial, char *name, void *handle, uint32_t regparams)
{
    if (!serial || !name)
        return OM_ERROR_PARAM;
    serial->parent.handle = handle;
    serial->parent.interface = &serial_interface;
    serial->cfg = SERIAL_DEFAULT_CFG;
    return device_register(&serial->parent, name, regparams | DEVICE_REG_STANDALONG | DEVICE_REG_RDWR);
}

// 涓插彛涓柇鏈嶅姟鍑芥暟锛屼紭鍖栨柟鍚戯細閲囩敤绫讳技Linux鐨勬柟娉曪紝灏嗕腑鏂垎涓轰笂涓嬮儴鐨勫紓姝ュ鐞嗭紝涓婇儴涓虹‖涓柇锛屽揩杩涘揩鍑猴紝璐熻矗璁板綍鐘舵€侊紱涓嬮儴涓哄伐浣滈槦鍒楋紝璐熻矗澶勭悊鏁版嵁鍜屼笟鍔￠€昏緫
OmRet_e serial_hw_isr(HalSerial_t serial, SerialEvent_e event, void *arg, size_t arg_size)
{
    if (!serial || !serial->interface ||
        (!device_check_status(&serial->parent, DEV_STATUS_OPENED) || !device_check_status(&serial->parent, DEV_STATUS_INITED) ||
         device_check_status(&serial->parent,
                             DEV_STATUS_SUSPEND))) // TODO: assert
        return OM_ERROR_PARAM;

    switch (event)
    {
    /* arg = 鎺ユ敹buffer锛宎rg_size = 鏈鎺ユ敹鏁版嵁鐨勫ぇ灏?*/
    case SERIAL_EVENT_INT_RXDONE: /* 涓柇鎺ユ敹瀹屾垚 */
    case SERIAL_EVENT_DMA_RXDONE: /* DMA鎺ユ敹瀹屾垚 */
    {
        SerialFifo_t rx_fifo;
        size_t rx_len;   // 鏈鎺ユ敹瀹為檯鍐欏叆rb鐨勬暟鎹暱搴?
        size_t data_len; // rb鏁版嵁鎬婚暱搴?
        if (!arg || arg_size == 0)
            return OM_ERROR_PARAM;
        rx_fifo = serial_get_rxfifo(serial);
        if (!rx_fifo || !rx_fifo->rb.buf)
            return OM_ERROR_PARAM;

        /* 1. 鐘舵€佹鏌ヤ笌鏇存柊 */
        if (rx_fifo->status == SERIAL_FIFO_BUSY) // 鍙湁鍦‵ifo澶勪簬绛夊緟鐘舵€佹墠澶勬洿鏂發oadSize
            rx_fifo->loadSize -= arg_size;
        rx_len = ringbuf_in(&rx_fifo->rb, arg, arg_size);

        // 閫氱煡搴旂敤灞俁XFIFO婧㈠嚭锛屽弬鏁版槸rb锛屽ぇ灏忔槸婧㈠嚭鐨勯噺锛屼簤鍙栧湪err_cb涓鐞嗕竴閮ㄥ垎rb涓殑鏁版嵁锛岃婧㈠嚭鐨勯噺鑳藉缁х画鍐欏叆FIFO
        if (!rx_len || rx_len < arg_size)
        {
            device_err_cb(&serial->parent, ERR_SERIAL_RXFIFO_OVERFLOW, arg_size - rx_len);
            // 鍋囪搴旂敤灞傚凡缁忓鐞嗕簡FIFO閿欒锛岄偅涔堣繖閲屽皾璇曞啀鍐欏叆涓€娆?
            (void)ringbuf_in(&rx_fifo->rb, (const unsigned char *)arg + rx_len, arg_size - rx_len);
            // 濡傛灉搴旂敤灞傛病鏈夊仛浠讳綍浜嬫儏锛岃繖閲屼篃涓嶇敤鍐嶉€氱煡涓€娆★紝娌℃湁鎰忎箟锛宔rr_cb宸茬粡瀹屾垚瀹冮€氱煡搴旂敤灞傜殑浣垮懡浜?
        }
        data_len = ringbuf_len(&rx_fifo->rb);
        if (SERIAL_IS_O_BLCK_RX(&serial->parent) && rx_fifo->loadSize <= 0)
        {
            /* 闃诲璇诲彲鑳藉湪鍚屼竴杞?ISR 涓噸澶嶈Е鍙?done锛孊USY 瑙嗕负鍙帴鍙椼€?*/
            serial_fifo_set_wait_reason(rx_fifo, SERIAL_WAIT_WAKE_DONE);
            OmRet_e cpt_ret = completion_done(&rx_fifo->cpt);
            (void)cpt_ret;
        }
        device_read_cb(&serial->parent, data_len);
    }
    break;

    /* arg = NULL, arg_size = 瀹為檯鍙戦€侀暱搴︼紝鐢卞簳灞傞┍鍔ㄤ紶鍏?*/
    case SERIAL_EVENT_INT_TXDONE: /* 涓柇鍙戦€佸畬鎴?*/
    case SERIAL_EVENT_DMA_TXDONE: /* 鍙戦€佸畬鎴?*/
    {
        SerialFifo_t tx_fifo;
        size_t liner_size;
        void *tx_buf;
        if (arg_size == 0)
            return OM_ERROR_PARAM;
        tx_fifo = serial_get_txfifo(serial);
        if (!tx_fifo || !tx_fifo->rb.buf)
            return OM_ERROR_PARAM;
        /* 1. 鐘舵€佹洿鏂?*/
        ringbuf_update_out(&tx_fifo->rb, arg_size); // 鏇存柊FIFO璇绘寚閽?
        tx_fifo->loadSize -= arg_size;              // 鏇存柊FIFO鍔犺浇鏁版嵁澶у皬
        if (tx_fifo->loadSize == 0)                 // loadSize涓?锛屽垯璇存槑涓€杞暟鎹凡缁忎紶杈撳畬姣?
            tx_fifo->status = SERIAL_FIFO_IDLE;

        /*  2. 鍥炶皟
            杩欓噷灏嗗洖璋冩斁鍦╰ransmit涔嬪墠锛屾槸鑰冭檻鍒板洖璋冧腑鍙兘浼氳皟鐢╳rite鎺ュ彛寰€rb涓鏁版嵁锛屼釜浜鸿涓哄湪鎿嶄綔涔嬪悗鍐嶈繘琛宼ransmit浼氭洿鐏垫椿鍜屽畨鍏?
        */
        device_write_cb(&serial->parent, ringbuf_avail(&tx_fifo->rb));

        /* 3. 澶勭悊鍙戦€佷换鍔?*/
        if (tx_fifo->status != SERIAL_FIFO_IDLE)
        {
            liner_size = ringbuf_get_item_linear_space(&tx_fifo->rb, &tx_buf); // 鑾峰彇FIFO鐨刬tem绾挎€х┖闂?
            tx_fifo->loadSize = liner_size;
            if (liner_size > 0U)
            {
                size_t tx_start_len = serial->interface->transmit(serial, tx_buf, liner_size);
                if (tx_start_len == 0U)
                {
                    tx_fifo->status = SERIAL_FIFO_IDLE;
                    tx_fifo->loadSize = 0;
                    if (SERIAL_IS_O_BLCK_TX(&serial->parent))
                    {
                        serial_fifo_set_wait_reason(tx_fifo, SERIAL_WAIT_WAKE_ERROR);
                        (void)completion_done(&tx_fifo->cpt);
                    }
                    device_clr_status(&serial->parent, DEV_STATUS_BUSY_TX);
                    device_err_cb(&serial->parent, ERR_SERIAL_TX_TIMEOUT, liner_size);
                }
            }
        }
        else
        {
            if (SERIAL_IS_O_BLCK_TX(&serial->parent))
            {
                /* 闃诲鍙戦€佸畬鎴愰€氱煡锛岄噸澶嶈Е鍙戣繑鍥?BUSY 涓嶅奖鍝嶆湰杞涔夈€?*/
                serial_fifo_set_wait_reason(tx_fifo, SERIAL_WAIT_WAKE_DONE);
                OmRet_e cpt_ret = completion_done(&tx_fifo->cpt);
                (void)cpt_ret;
            }
            device_clr_status(&serial->parent, DEV_STATUS_BUSY_TX);
        }
    }
    break;

    case SERIAL_EVENT_ERR_OCCUR: /* 涓插彛閿欒鍥炶皟锛屽緟瀹屽杽 */
        serial_abort_blocking_waiters(serial, SERIAL_WAIT_WAKE_ERROR);
        device_err_cb(&serial->parent, (uint32_t)arg, 0);
        break;
    default:
        return OM_ERROR_PARAM;
    }
    return OM_OK;
}

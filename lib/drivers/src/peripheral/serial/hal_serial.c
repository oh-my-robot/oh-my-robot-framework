#include "drivers/peripheral/serial/pal_serial_dev.h"
#include "osal/osal_core.h"
/*************************** PRIVATE MACRO ********************************************************/
#define SERIAL_IS_O_BLCK_TX(dev) (device_get_oparams(dev) & SERIAL_O_BLCK_TX)
#define SERIAL_IS_O_NBLCK_TX(dev) (device_get_oparams(dev) & SERIAL_O_NBLCK_TX)
#define SERIAL_IS_O_BLCK_RX(dev) (device_get_oparams(dev) & SERIAL_O_BLCK_RX)
#define SERIAL_IS_O_NBLCK_RX(dev) (device_get_oparams(dev) & SERIAL_O_NBLCK_RX)
/************************** PRIVATE FUNCTION *********************************************************/
static OmRet serial_ctrl(Device *dev, size_t cmd, void *arg);

static void serial_fifo_set_wait_reason(SerialFifo *fifo, SerialWaitWakeReason reason)
{
    if (!fifo)
        return;
    osal_irq_lock_task();
    fifo->waitReason = reason;
    osal_irq_unlock_task();
}

static SerialWaitWakeReason serial_fifo_get_and_clear_wait_reason(SerialFifo *fifo)
{
    SerialWaitWakeReason reason = SERIAL_WAIT_WAKE_NONE;

    if (!fifo)
        return reason;

    osal_irq_lock_task();
    reason = fifo->waitReason;
    fifo->waitReason = SERIAL_WAIT_WAKE_NONE;
    osal_irq_unlock_task();
    return reason;
}

static OmRet serial_wait_completion_abortable(Device *dev, SerialFifo *fifo, uint32_t timeout_ms)
{
    OmRet wait_ret;
    SerialWaitWakeReason wake_reason;

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

static void serial_abort_blocking_waiters(HalSerial *serial, SerialWaitWakeReason reason)
{
    SerialFifo *tx_fifo;
    SerialFifo *rx_fifo;

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
/* 无 FIFO 情况下的 TX 函数*/
static size_t serial_tx_poll(Device *dev, void *data, size_t len)
{
    size_t length;
    HalSerial *serial = (HalSerial *)dev;
    uint8_t *tx_data = (uint8_t *)data;
    if (device_check_status(dev, DEV_STATUS_BUSY_TX))
    {
        // TODO: log txbusy
        return 0;
    }
    device_set_status(dev, DEV_STATUS_BUSY_TX);
    for (length = 0; length < len; length++)
    {
        if (serial->interface->putByte(serial, tx_data[length]) != OM_OK) // 固定语义/功能
            break;
    }
    // if(length < len)
    // TODO: LOG 发送超时，打印实际发送字
    device_clr_status(dev, DEV_STATUS_BUSY_TX);
    return length;
}

static size_t serial_rx_poll(Device *dev, uint8_t *buf, size_t len)
{
    size_t length;
    OmRet ret = OM_ERROR;
    HalSerial *serial = (HalSerial *)dev;
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
    // TODO: LOG 接收超时，打印实际接收字
    device_clr_status(dev, DEV_STATUS_BUSY_RX);
    return length;
}

/**
 * @brief 串口阻塞发送内部实现
 *
 * @param dev 串口设备
 * @param data 应用层数据包指针
 * @param len  应用层数据包长度
 * @return size_t 实际发送长度
 * @note   调用者需自行确保 data 的内存安全和数据完整
 */
static size_t serial_tx_block(Device *dev, void *data, size_t len)
{
    HalSerial *serial;
    SerialFifo *tx_fifo;
    OmRet wait_ret;
    size_t tx_start_len;
    size_t txlen;
    size_t linear_space_len;
    void *tx_linear_buf = 0; // 串口线性缓存区
    size_t sended = 0;       // 已经发送的长度

    serial = (HalSerial *)dev;
    tx_fifo = serial_get_txfifo(serial);

    // 理论上对于串口，对于阻塞发送来说不应该出现BUSY_TX 的情况
    // 因为同一个串口，同一时间仅允许被一个线程打开
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

/* 串口非阻塞发送*/
static size_t serial_tx_nonblock(Device *dev, void *data, size_t len)
{
    HalSerial *serial;
    SerialFifo *tx_fifo;
    size_t tx_len;
    size_t tx_start_len;
    size_t sended = 0;
    void *tx_ptr = NULL;

    serial = (HalSerial *)dev;
    tx_fifo = serial_get_txfifo(serial);
    sended = ringbuf_in(&tx_fifo->rb, data, len);

    device_set_status(dev, DEV_STATUS_BUSY_TX);
    /* 发送数据*/
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
    // 非阻塞的serial_busy_tx状态在中断中清理
    return sended;
}

static size_t serial_rx_block(Device *dev, void *buf, size_t len)
{
    HalSerial *serial;
    SerialFifo *rx_fifo;
    OmRet wait_ret;
    size_t rd_remain;      // 剩余未读取的长度
    size_t rd_len;         // 本轮读取的长度，真实反应硬件实际接收字节数，也就是考虑rxfifo可能溢出的情况
    size_t rd_from_rb = 0; // 应用层通过read接口获得的总长度，也就是不包括接收过程中rxfifo可能溢出的情况

    serial = (HalSerial *)dev;
    rx_fifo = serial_get_rxfifo(serial);
    device_set_status(dev, DEV_STATUS_BUSY_RX);
    rd_len = ringbuf_len(&rx_fifo->rb);

    /* rd_len < len */
    rd_remain = len - rd_len; // 减去已经有的数据长度才是剩余需要读取的长度
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
        rx_fifo->status = SERIAL_FIFO_BUSY; // FIFO 开始等待数据
        rx_fifo->waitReason = SERIAL_WAIT_WAKE_NONE;
        osal_irq_unlock_task();
        wait_ret = serial_wait_completion_abortable(dev, rx_fifo, SERIAL_BLOCK_RX_WAIT_TIMEOUT_MS); // 等待数据到来
        rx_fifo->status = SERIAL_FIFO_IDLE;                                                         // FIFO结束数据等待
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
        // rd_total仅考虑通过API调用获得的数据，这是为了让应用层知道通过API实际读取的字节数
        rd_from_rb += ringbuf_out(&rx_fifo->rb, buf + rd_from_rb, rd_len);

        if (rx_fifo->loadSize < 0) // loadSize < 0 说明出现了FIFO溢出错误
        {
            osal_irq_lock_task();
            // 负负得正，实际上是算上了溢出的数据长度；框架层不关心应用层是否处理溢出，只是诚实地记录硬件实际接收过的字节数)
            rd_len -= rx_fifo->loadSize;
            // 至于溢出的数据怎么办？那是API使用者要考虑的事情，要不把rb开大一点，要不就在错误回调中做特殊处理
            rx_fifo->loadSize = 0;
            osal_irq_unlock_task();
        }
        /*
            loadSize包含rxfifo溢出的信息，没法确定究竟溢出了多少，因此rd_len可能会大于rd_remain
            当 rd_len>=rd_remain 时，rd_remain 直接归零即可
        */
        rd_remain = (rd_remain > rd_len) ? (rd_remain - rd_len) : 0;
    } while (rd_remain);
    device_clr_status(dev, DEV_STATUS_BUSY_RX);
    return rd_from_rb;
}

static size_t serial_rx_nonblock(Device *dev, void *buf, size_t len)
{
    HalSerial *serial;
    SerialFifo *rx_fifo;
    size_t rd_len;

    serial = (HalSerial *)dev;
    rx_fifo = serial_get_rxfifo(serial);

    if (ringbuf_len(&rx_fifo->rb) >= len)
        rd_len = ringbuf_out(&rx_fifo->rb, buf, len);
    else
        rd_len = 0;
    return rd_len;
}

static OmRet serial_set_cfg(HalSerial *serial, SerialCfg *cfg)
{
    OmRet ret;

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

// TODO: 待实现
static OmRet serial_flush(HalSerial *serial, uint32_t fifo_selector)
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

static OmRet serial_tx_enable(Device *dev)
{
    HalSerial *serial;
    SerialFifo *tx_fifo;
    size_t ctrl_arg = 0;

    serial = (HalSerial *)dev;
    tx_fifo = serial_get_txfifo(serial);
    // 无缓冲
    if (serial->cfg.txBufSize == 0)
    {
        // 串口无缓冲存只能使用poll方式
        return OM_OK;
    }

    /* 底层未填FIFO */
    if (!tx_fifo)
    {
        /*  当前框架下，除了poll之外所有发送方式都会配置rb，无论阻塞非阻塞
            后续若是发现不合适的话可以再细分，目前暂不支持。
            但是无论如何，建议非阻塞发送都使用 rb。
        */
        if (serial->cfg.txBufSize < SERIAL_MIN_TX_BUFSZ) // TODO: log
            serial->cfg.txBufSize = SERIAL_MIN_TX_BUFSZ;

        tx_fifo = (SerialFifo *)osal_malloc(sizeof(SerialFifo));
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

static OmRet serial_rx_enable(Device *dev)
{
    HalSerial *serial;
    SerialFifo *rx_fifo;
    uint32_t ctrl_arg;

    ctrl_arg = device_get_regparams(dev) & DEVICE_REG_RXTYPE_MASK;

    serial = (HalSerial *)dev;
    rx_fifo = serial_get_rxfifo(serial);

    if (serial->cfg.rxBufSize == 0)
    {
        // 无缓冲存，串口只能轮询读取
        return OM_OK;
    }

    if (!rx_fifo)
    {
        if (serial->cfg.rxBufSize < SERIAL_MIN_RX_BUFSZ)
            serial->cfg.rxBufSize = SERIAL_MIN_RX_BUFSZ;

        rx_fifo = (SerialFifo *)osal_malloc(sizeof(SerialFifo));
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
static OmRet serial_init(Device *dev)
{
    HalSerial *serial = (HalSerial *)dev;
    if (!serial || !serial->interface || !serial->interface->configure)
        return OM_ERROR_PARAM;
    serial->priv.txFifo = NULL;
    serial->priv.rxFifo = NULL;
    return serial->interface->configure(serial, &serial->cfg);
}

static OmRet serial_open(Device *dev, uint32_t otype)
{
    HalSerial *serial = (HalSerial *)dev;
    OmRet ret;
    if (!serial || !serial->interface || !serial->interface->control)
        return OM_ERROR_PARAM;

    /* 串口写方*/
    dev->priv.oparams |= (otype & SERIAL_O_BLCK_TX) ? SERIAL_O_BLCK_TX : (otype & SERIAL_O_NBLCK_TX) ? SERIAL_O_NBLCK_TX
                                                                                                     : 0U;
    /* 串口读方*/
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

static OmRet serial_ctrl(Device *dev, size_t cmd, void *arg)
{
    OmRet ret;
    HalSerial *serial = (HalSerial *)dev;
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
        SerialCfg *cfg;
        if (!arg)
        {
            ret = OM_ERROR_PARAM;
            break;
        }
        cfg = (SerialCfg *)arg;
        ret = serial_set_cfg(serial, cfg);
    }
    break;

    case SERIAL_CMD_SUSPEND:
        /*
         * 先标记挂起，再下发底层停止，最后唤醒阻塞等待者，
         * 保证阻塞路径不会永久等待
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

static size_t serial_write(Device *dev, void *pos, void *data, size_t len)
{
    SerialFifo *txfifo;
    HalSerial *serial;
    uint32_t oparams;
    size_t ret_len = 0;
    oparams = device_get_oparams(dev);
    serial = (HalSerial *)dev;
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

    // 若之前的数据没有发送完，直接放入FIFO等待发
    if (device_check_status(dev, DEV_STATUS_BUSY_TX))
    {
        ret_len = ringbuf_in(&txfifo->rb, data, len);
    }
    else
    {
        if ((oparams & SERIAL_O_NBLCK_TX) || osal_is_in_isr()) // 在中断中只能使用非阻塞发?
            ret_len = serial_tx_nonblock(dev, data, len);
        else if (oparams & SERIAL_O_BLCK_TX)
            ret_len = serial_tx_block(dev, data, len);
    }
    // 若是发送数据据小于总长度，则认为TXFIFO溢出，触发回调函数
    // 从机制上来说，阻塞发送不会出现TXFIFO溢出的情况，非阻塞发送则可能由于FIFO不够而直接退出发送
    if (ret_len < len)
        device_err_cb(dev, ERR_SERIAL_TXFIFO_OVERFLOW, len - ret_len);
    return ret_len;
}

static size_t serial_read(Device *dev, void *pos, void *buf, size_t len)
{
    HalSerial *serial;
    SerialFifo *rx_fifo;
    size_t recv_len = 0;
    serial = (HalSerial *)dev;
    rx_fifo = serial_get_rxfifo(serial);
    if (serial->cfg.rxBufSize == 0 || ((device_get_regparams(dev) & DEVICE_REG_RXTYPE_MASK) == DEVICE_REG_POLL_RX))
    {
        return serial_rx_poll(dev, (uint8_t *)buf, len);
    }
    if (!rx_fifo || !rx_fifo->rb.buf)
        return 0;

    if (ringbuf_len(&rx_fifo->rb) >= len)
        recv_len = ringbuf_out(&rx_fifo->rb, buf, len);
    else if (SERIAL_IS_O_NBLCK_RX(dev) || osal_is_in_isr()) // 在中断中只能使用非阻塞接?
        recv_len = serial_rx_nonblock(dev, buf, len);
    else if (SERIAL_IS_O_BLCK_RX(dev))
        recv_len = serial_rx_block(dev, buf, len);

    return recv_len;
}

static DevInterface serial_interface = {
    .init = serial_init,
    .open = serial_open,
    .close = OM_NULL,       // 待完善
    .control = serial_ctrl, // 待完善
    .read = serial_read,
    .write = serial_write,
};

/******************************************* HW API *********************************************/
OmRet serial_register(HalSerial *serial, char *name, void *handle, uint32_t regparams)
{
    if (!serial || !name)
        return OM_ERROR_PARAM;
    serial->parent.handle = handle;
    serial->parent.interface = &serial_interface;
    serial->cfg = SERIAL_DEFAULT_CFG;
    return device_register(&serial->parent, name, regparams | DEVICE_REG_STANDALONG | DEVICE_REG_RDWR);
}

// 串口中断服务函数，优化方向：采用类似Linux的方法，将中断分为上下部的异步处理，上部为硬中断，快进快出，负责记录状态；下部为工作队列，负责处理数据和业务逻辑
OmRet serial_hw_isr(HalSerial *serial, SerialEvent event, void *arg, size_t arg_size)
{
    if (!serial || !serial->interface ||
        (!device_check_status(&serial->parent, DEV_STATUS_OPENED) || !device_check_status(&serial->parent, DEV_STATUS_INITED) ||
         device_check_status(&serial->parent,
                             DEV_STATUS_SUSPEND))) // TODO: assert
        return OM_ERROR_PARAM;

    switch (event)
    {
    /* arg = 接收buffer，arg_size = 本次接收数据的大小*/
    case SERIAL_EVENT_INT_RXDONE: /* 中断接收完成 */
    case SERIAL_EVENT_DMA_RXDONE: /* DMA接收完成 */
    {
        SerialFifo *rx_fifo;
        size_t rx_len;   // 本次接收实际写入 rb 的数据长度
        size_t data_len; // rb 数据总长度
        if (!arg || arg_size == 0)
            return OM_ERROR_PARAM;
        rx_fifo = serial_get_rxfifo(serial);
        if (!rx_fifo || !rx_fifo->rb.buf)
            return OM_ERROR_PARAM;

        /* 1. 状态检查与更新 */
        if (rx_fifo->status == SERIAL_FIFO_BUSY) // 只有在Fifo处于等待状态才处更新loadSize
            rx_fifo->loadSize -= arg_size;
        rx_len = ringbuf_in(&rx_fifo->rb, arg, arg_size);

        // 通知应用层RXFIFO溢出，参数是rb，大小是溢出的量，争取在err_cb中处理一部分rb中的数据，让溢出的量能够继续写入FIFO
        if (!rx_len || rx_len < arg_size)
        {
            device_err_cb(&serial->parent, ERR_SERIAL_RXFIFO_OVERFLOW, arg_size - rx_len);
            // 假设应用层已经处理了FIFO错误，那么这里尝试再写入一次
            (void)ringbuf_in(&rx_fifo->rb, (const unsigned char *)arg + rx_len, arg_size - rx_len);
            // 如果应用层没有做任何事情，这里也不用再通知一次，没有意义，err_cb已经完成它通知应用层的使命
        }
        data_len = ringbuf_len(&rx_fifo->rb);
        if (SERIAL_IS_O_BLCK_RX(&serial->parent) && rx_fifo->loadSize <= 0)
        {
            /* 阻塞读可能在同一 ISR 中重复触发 done，BUSY 视为可接受*/
            serial_fifo_set_wait_reason(rx_fifo, SERIAL_WAIT_WAKE_DONE);
            OmRet cpt_ret = completion_done(&rx_fifo->cpt);
            (void)cpt_ret;
        }
        device_read_cb(&serial->parent, data_len);
    }
    break;

    /* arg = NULL, arg_size = 实际发送长度，由底层驱动传*/
    case SERIAL_EVENT_INT_TXDONE: /* 中断发送完*/
    case SERIAL_EVENT_DMA_TXDONE: /* 发送完*/
    {
        SerialFifo *tx_fifo;
        size_t liner_size;
        void *tx_buf;
        if (arg_size == 0)
            return OM_ERROR_PARAM;
        tx_fifo = serial_get_txfifo(serial);
        if (!tx_fifo || !tx_fifo->rb.buf)
            return OM_ERROR_PARAM;
        /* 1. 状态更新*/
        ringbuf_update_out(&tx_fifo->rb, arg_size); // 更新 FIFO 读指针
        tx_fifo->loadSize -= arg_size;              // 更新FIFO加载数据大小
        if (tx_fifo->loadSize == 0)                 // loadSize 为 0，则说明一轮数据已经传输完成
            tx_fifo->status = SERIAL_FIFO_IDLE;

        /*  2. 回调
            这里将回调放在transmit之前，是考虑到回调中可能会调用write接口往rb中塞数据，个人认为在操作之后再进行transmit会更灵活和安全
        */
        device_write_cb(&serial->parent, ringbuf_avail(&tx_fifo->rb));

        /* 3. 处理发送任务*/
        if (tx_fifo->status != SERIAL_FIFO_IDLE)
        {
            liner_size = ringbuf_get_item_linear_space(&tx_fifo->rb, &tx_buf); // 获取 FIFO 的 item 线性空间
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
                /* 阻塞发送完成通知，重复触发返BUSY 不影响本轮语义*/
                serial_fifo_set_wait_reason(tx_fifo, SERIAL_WAIT_WAKE_DONE);
                OmRet cpt_ret = completion_done(&tx_fifo->cpt);
                (void)cpt_ret;
            }
            device_clr_status(&serial->parent, DEV_STATUS_BUSY_TX);
        }
    }
    break;

    case SERIAL_EVENT_ERR_OCCUR: /* 串口错误回调，待完善 */
        serial_abort_blocking_waiters(serial, SERIAL_WAIT_WAKE_ERROR);
        device_err_cb(&serial->parent, (uint32_t)arg, 0);
        break;
    default:
        return OM_ERROR_PARAM;
    }
    return OM_OK;
}

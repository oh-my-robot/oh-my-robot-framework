#include "core/om_cpu.h"
#include "core/om_init.h"
#include "drivers/peripheral/serial/pal_serial_dev.h"
#include "osal/osal.h"
#include "osal/osal_time.h"
#include <string.h>

void serial_read_cb(Device *dev, void *param, size_t paramsz)
{
}

void serial_write_cb(Device *dev, void *param, size_t paramsz)
{
}

/*
 * @brief: 串口错误回调函数
 * @param: dev 串口设备句柄
 * @param: errcode 错误码
 * @param: param 错误参数
 * @param: paramsz 错误参数大小
 * @return: 无
 */
void serial_err_cb(Device *dev, uint32_t errcode, void *param, size_t paramsz)
{
    switch (errcode)
    {
    case ERR_SERIAL_INVALID_MEM:
        OM_CPU_ERRHANDLER("ERR_SERIAL_INVALID_MEM", OM_CPU_LOG_LEVEL_FATAL);
        break;

    case ERR_SERIAL_RXFIFO_OVERFLOW:
        // ringbuf_out(param, tx_data, paramsz);
        // device_write(dev, NULL, tx_data, paramsz);
        break;

    case ERR_SERIAL_TXFIFO_OVERFLOW: {
    }
    break;
    default: {
        char *notify = "\r\nserial occurred some error\r\n";
        device_write(dev, NULL, notify, strlen(notify));
        OM_CPU_ERRHANDLER("serial occurred some error", OM_CPU_LOG_LEVEL_FATAL);
    }
    break;
    }
}

void serial_test_task(void *pvParameters)
{
    Device *serial = device_find("usart6");
    device_open(serial, SERIAL_O_BLCK_TX | SERIAL_O_BLCK_RX);
    device_set_read_cb(serial, serial_read_cb); // read done callback
    device_set_err_cb(serial, serial_err_cb);
    device_set_write_cb(serial, serial_write_cb);

    char *notify = "\r\nserial test start\r\n";
    uint8_t buf[128] = {0};
    uint32_t len = 0;
    device_write(serial, NULL, notify, strlen(notify));
    OsalTimeMs last_time = osal_time_now_monotonic();

    while (1)
    {
        len = device_read(serial, NULL, buf, 1);
        if (len > 0)
        {
            device_write(serial, NULL, buf, len);
        }
    }
}

/* app 自身启动设置：经 OM_INIT_APPLICATION 分散加载，init 线程（调度器后）自动调用；
 * 板级自举与硬件初始化由 BOARD 级 initcall 自动完成，无需在此显式调用。 */
static OmRet serial_crc_app_setup(void)
{
    OsalThread *task1 = NULL;
    OsalThreadAttr attr = {0};
    attr.name = "SerialTestTask";
    attr.stackSize = 5120u * OSAL_STACK_WORD_BYTES;
    attr.priority = OSAL_PRIO_LOW_BASE;
    int result1 = osal_thread_create(&task1, &attr, serial_test_task, NULL);
    while (result1 != OSAL_OK)
    {
    }

    return OM_OK;
}
OM_INIT_APPLICATION(serial_crc_app_setup);

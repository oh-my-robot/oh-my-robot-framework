#include "core/om_cpu.h"
#include "drivers/peripheral/serial/pal_serial_dev.h"
#include "osal/osal.h"
#include "osal/osal_time.h"
#include <string.h>

void serial_read_cb(Device* dev, void* param, size_t paramsz)
{
}

void serial_write_cb(Device* dev, void* param, size_t paramsz)
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
void serial_err_cb(Device* dev, uint32_t errcode, void* param, size_t paramsz)
{
    switch (errcode)
    {
    case ERR_SERIAL_INVALID_MEM:
        OM_CPU_ERRHANDLER("ERR_SERIAL_INVALID_MEM", OM_LOG_LEVEL_FATAL);
        break;

    case ERR_SERIAL_RXFIFO_OVERFLOW:
        // ringbuf_out(param, tx_data, paramsz);
        // device_write(dev, NULL, tx_data, paramsz);
        break;

    case ERR_SERIAL_TXFIFO_OVERFLOW:
    {
    }
    break;
    default:
    {
        char* notify = "\r\nserial occurred some error\r\n";
        device_write(dev, NULL, notify, strlen(notify));
        OM_CPU_ERRHANDLER("serial occurred some error", OM_LOG_LEVEL_FATAL);
    }
    break;
    }
}

void serial_test_task(void* pvParameters)
{
    Device* serial = device_find("usart6");
    device_open(serial, SERIAL_O_BLCK_TX | SERIAL_O_BLCK_RX);
    device_set_read_cb(serial, serial_read_cb); // read done callback
    device_set_err_cb(serial, serial_err_cb);
    device_set_write_cb(serial, serial_write_cb);

    char* notify = "\r\nserial test start\r\n";
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

int main(void)
{
    OsalThread* task1 = NULL;
    OsalThreadAttr attr = {0};
    attr.name = "SerialTestTask";
    attr.stackSize = 5120u * OSAL_STACK_WORD_BYTES;
    attr.priority = 4;
    int result1 = osal_thread_create(&task1, &attr, serial_test_task, NULL);
    while (result1 != OSAL_OK)
    {
    }
    om_board_init();
    om_core_init();

    osal_kernel_start();
    // 调度成功后不会运行到这里
    while (1)
    {
    }
    return 0;
}

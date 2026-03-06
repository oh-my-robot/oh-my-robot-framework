#include "core/om_cpu.h"
#include "drivers/peripheral/can/pal_can_dev.h"
#include "osal/osal.h"

#define CAN_MSG_BUF_LEN 8
#define CAN_TX_MSG_BUF_LEN 1000

typedef struct CanInfo CanInfo;
typedef struct CanInfo
{
    CanUserMsg msg[CAN_MSG_BUF_LEN];
    uint8_t data[CAN_MSG_BUF_LEN][8];
} OM_PACKED CanInfo;

typedef struct CanTxInfo CanTxInfo;
typedef struct CanTxInfo
{
    CanUserMsg msg[CAN_TX_MSG_BUF_LEN];
    uint8_t data[CAN_TX_MSG_BUF_LEN][8];
} OM_PACKED CanTxInfo;

static void can_info_init(CanUserMsg* msg, CanFilterHandle filter_handle, uint8_t* data)
{
    msg->filterHandle = filter_handle;
    msg->userBuf = data;
}

static void can_txinfo_init(CanUserMsg* msg, uint8_t* data)
{
    msg->filterHandle = 0;
    msg->userBuf = data;
}

uint8_t cnt = 0;
static void can_filter_callback(Device* dev, void* param, CanFilterHandle filter_handle, size_t msg_count)
{
    (void)filter_handle;
    CanInfo* info = (CanInfo*)param;
    device_read(dev, NULL, &info->msg[cnt], msg_count);
    for (size_t i = 0; i < msg_count; i++)
    {
        info->msg[cnt + i].dsc.id = info->msg[cnt + i].dsc.id + 0x100; // CAN总线下不允许出现相同ID，因此这里加0x100避免和上位机ID冲突
    }
    device_write(dev, NULL, &info->msg[cnt], msg_count);
    cnt++;
    cnt %= CAN_MSG_BUF_LEN;
}

CanInfo can_info = {0};
CanTxInfo can_tx_info;

void can_test_task(void* param)
{
    OmRet ret = OM_OK;
    Device* can = device_find("can1");
    while (!can)
    {
    };

    /* CAN 设备初始化 */
    ret = device_open(can, CAN_O_INT_RX | CAN_O_INT_TX);
    while (ret != OM_OK)
    {
    };

    CanFilterAllocArg filter_alloc_arg = {
        .request = CAN_FILTER_REQUEST_INIT(CAN_FILTER_MODE_MASK, CAN_FILTER_ID_STD_EXT, 0x101, 0x1F0, can_filter_callback, (void*)&can_info),
    };
    ret = device_ctrl(can, CAN_CMD_FILTER_ALLOC, &filter_alloc_arg);
    while (ret != OM_OK)
    {
    };

    for (size_t i = 0; i < CAN_MSG_BUF_LEN; i++)
        can_info_init(&can_info.msg[i], filter_alloc_arg.handle, can_info.data[i]);

    device_ctrl(can, CAN_CMD_START, NULL);

    while (1)
    {
        // while(cnt < CAN_TX_MSG_BUF_LEN)
        // {
        //     device_write(can, NULL, &canTxInfo.msg[cnt], 1);
        //     cnt++;
        // }
    }
}

int main(void)
{
    OsalThread* task1 = NULL;
    OsalThreadAttr attr = {0};
    attr.name = "CanTestTask";
    attr.stackSize = 512u * OSAL_STACK_WORD_BYTES;
    attr.priority = 4;
    int result1 = osal_thread_create(&task1, &attr, can_test_task, NULL);
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

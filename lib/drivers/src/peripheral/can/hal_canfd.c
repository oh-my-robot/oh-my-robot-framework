#include "drivers/peripheral/can/pal_can_dev.h"
#include "osal/osal_core.h"
#include <string.h>

/* CANFd鎶ユ枃瀹瑰櫒 */
typedef struct CanFdMsgContainer CanFdMsgContainer;
typedef struct CanFdMsgContainer
{
    CanMsgList listNode;
    uint8_t container[64];
} OM_PACKED CanFdMsgContainer;

static void *canfd_msgbuffer_alloc(ListHead *free_list_head, uint32_t msg_num)
{
    size_t size = msg_num * sizeof(CanFdMsgContainer);
    void *msg_buffer = osal_malloc(size);
    if (msg_buffer == NULL)
        return NULL;
    memset(msg_buffer, 0, size);

    // Add all CAN FD message containers into the free list.
    CanFdMsgContainer *msg_list = (CanFdMsgContainer *)msg_buffer;
    for (size_t i = 0; i < msg_num; i++)
    {
        INIT_LIST_HEAD(&msg_list[i].listNode.fifoListNode);
        INIT_LIST_HEAD(&msg_list[i].listNode.matchedListNode);
        list_add(&msg_list[i].listNode.fifoListNode, free_list_head);
        msg_list[i].listNode.userMsg.userBuf = msg_list[i].listNode.container; // 鎸囧悜瀹瑰櫒鍐呭瓨
    }
    return msg_buffer;
}

static CanAdapterInterface can_fd_adapter_interface = {
    .msgbufferAlloc = canfd_msgbuffer_alloc,
};

CanAdapterInterface *hal_can_get_canfd_adapter_interface(void)
{
    return &can_fd_adapter_interface;
}

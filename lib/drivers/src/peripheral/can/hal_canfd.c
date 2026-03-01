#include "drivers/peripheral/can/pal_can_dev.h"
#include "osal/osal_core.h"
#include <string.h>

/* CANFd鎶ユ枃瀹瑰櫒 */
typedef struct CanFdMsgContainer *CanFdMsgContainer_t;
typedef struct CanFdMsgContainer
{
    CanMsgList_s listNode;
    uint8_t container[64];
} OM_PACKED CanFdMsgContainer_s;

static void *canfd_msgbuffer_alloc(ListHead_s *free_list_head, uint32_t msg_num)
{
    size_t size = msg_num * sizeof(CanFdMsgContainer_s);
    void *msg_buffer = osal_malloc(size);
    if (msg_buffer == NULL)
        return NULL;
    memset(msg_buffer, 0, size);

    // Add all CAN FD message containers into the free list.
    CanFdMsgContainer_t msg_list = (CanFdMsgContainer_t)msg_buffer;
    for (size_t i = 0; i < msg_num; i++)
    {
        INIT_LIST_HEAD(&msg_list[i].listNode.fifoListNode);
        INIT_LIST_HEAD(&msg_list[i].listNode.matchedListNode);
        list_add(&msg_list[i].listNode.fifoListNode, free_list_head);
        msg_list[i].listNode.userMsg.userBuf = msg_list[i].listNode.container; // 鎸囧悜瀹瑰櫒鍐呭瓨
    }
    return msg_buffer;
}

static CanAdapterInterface_s can_fd_adapter_interface = {
    .msgbufferAlloc = canfd_msgbuffer_alloc,
};

CanAdapterInterface_t hal_can_get_canfd_adapter_interface(void)
{
    return &can_fd_adapter_interface;
}

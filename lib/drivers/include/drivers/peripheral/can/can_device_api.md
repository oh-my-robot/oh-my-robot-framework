# CAN 设备 API 使用指南

## 1. 文档范围
本文说明 OM 中 CAN 设备的应用层使用方式，聚焦以下接口：

```c
device_find
device_open
device_ctrl
device_read
device_write
device_close
```

说明：

```text
1. 应用层不直接操作 BSP，也不直接感知硬件 filter bank。
2. 过滤器由框架分配为 filterHandle，应用只使用 handle。
3. 本文不涉及具体板卡初始化。
```

## 2. 最小使用流程
```c
Device_t canDev = device_find("can1");
device_open(canDev, CAN_O_INT_RX | CAN_O_INT_TX);
device_ctrl(canDev, CAN_CMD_CFG, &cfg);
device_ctrl(canDev, CAN_CMD_FILTER_ALLOC, &allocArg);
device_ctrl(canDev, CAN_CMD_START, NULL);
device_write(canDev, NULL, txMsgs, txCount);
device_read(canDev, NULL, rxMsgs, rxCount);
device_ctrl(canDev, CAN_CMD_FILTER_FREE, &filterHandle);
device_close(canDev);
```

## 3. 关键数据结构
```c
typedef struct CanFilterRequest {
    CanFilterMode_e workMode;
    CanFilterIdType_e idType;
    uint32_t id;
    uint32_t mask;
    void (*rx_callback)(Device_t dev, void *param, CanFilterHandle_t handle, size_t msgCount);
    void *param;
} CanFilterRequest_s;

typedef struct CanFilterAllocArg {
    CanFilterRequest_s request;
    CanFilterHandle_t handle;
} CanFilterAllocArg_s;

typedef struct CanUserMsg {
    CanMsgDsc_s dsc;
    CanFilterHandle_t filterHandle;
    uint8_t *userBuf;
} CanUserMsg_s;
```

## 4. 过滤器申请与回收
### 4.1 申请过滤器
```c
static void can_rx_cb(Device_t dev, void *param, CanFilterHandle_t handle, size_t msgCount)
{
    (void)dev;
    (void)param;
    (void)handle;
    (void)msgCount;
}

CanFilterAllocArg_s allocArg = {
    .request = CAN_FILTER_REQUEST_INIT(
        CAN_FILTER_MODE_MASK,
        CAN_FILTER_ID_STD,
        0x200,
        0x7F0,
        can_rx_cb,
        NULL),
};

OmRet_e ret = device_ctrl(canDev, CAN_CMD_FILTER_ALLOC, &allocArg);
if (ret == OM_OK) {
    CanFilterHandle_t filterHandle = allocArg.handle;
}
```

### 4.2 回收过滤器
```c
OmRet_e ret = device_ctrl(canDev, CAN_CMD_FILTER_FREE, &filterHandle);
```

约束：

```text
1. 过滤器未激活或 handle 非法会返回参数错误。
2. 过滤器仍有未读消息时释放会返回忙。
3. 重复释放同一 handle 会失败。
```

## 5. 发送示例
```c
uint8_t txData[8] = {1,2,3,4,5,6,7,8};
CanUserMsg_s txMsg = {
    .dsc = CAN_DATA_MSG_DSC_INIT(0x201, CAN_IDE_STD, CAN_DLC_8),
    .filterHandle = 0,
    .userBuf = txData,
};

size_t sent = device_write(canDev, NULL, &txMsg, 1);
```

说明：

```text
1. 发送路径不依赖 filterHandle 选路，邮箱由框架和硬件协同调度。
2. filterHandle 建议显式置 0，避免脏值。
```

## 6. 接收示例
```c
uint8_t rxData[8] = {0};
CanUserMsg_s rxMsg = {
    .filterHandle = filterHandle,
    .userBuf = rxData,
};

size_t got = device_read(canDev, NULL, &rxMsg, 1);
if (got == 1) {
    uint32_t id = rxMsg.dsc.id;
    uint32_t len = rxMsg.dsc.dataLen;
    (void)id;
    (void)len;
}
```

## 7. 控制命令一览
| 命令 | 作用 | 参数 |
| --- | --- | --- |
| `CAN_CMD_CFG` | 更新 CAN 配置 | `CanCfg_t` |
| `CAN_CMD_SET_IOTYPE` | 设置 IO 类型 | `uint32_t*` |
| `CAN_CMD_CLR_IOTYPE` | 清除 IO 类型 | `uint32_t*` |
| `CAN_CMD_FILTER_ALLOC` | 申请过滤器资源并激活 | `CanFilterAllocArg_t` |
| `CAN_CMD_FILTER_FREE` | 释放过滤器资源 | `CanFilterHandle_t*` |
| `CAN_CMD_START` | 启动 CAN | `NULL` |
| `CAN_CMD_CLOSE` | 关闭 CAN | `NULL` |
| `CAN_CMD_GET_STATUS` | 读取错误计数 | `CanErrCounter_t` |

## 8. 常见错误
```text
1. OM_ERROR
   设备未打开或设备状态非法。

2. OM_ERROR_PARAM
   参数为空、handle 非法、回调为空。

3. OM_ERROR_BUSY
   资源不足或释放时过滤器仍有待处理消息。
```

## 9. 迁移说明
旧接口：

```c
CAN_CMD_SET_FILTER
CanFilterCfg_s
msg.bank
```

新接口：

```c
CAN_CMD_FILTER_ALLOC
CAN_CMD_FILTER_FREE
CanFilterAllocArg_s
msg.filterHandle
```

迁移原则：

```text
1. 应用不再传入硬件 bank。
2. 应用通过 alloc 返回的 handle 进行 read 与模块内路由。
3. 硬件 bank 到 handle 的映射由 CAN 框架维护。
```

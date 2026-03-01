# OM 串口框架 API 手册

## 概述

OM串口框架是一个基于设备抽象层的串口驱动框架，提供了统一的设备接口来操作串口设备。该框架支持多种工作模式（阻塞/非阻塞、轮询/中断/DMA），具有高度可配置性和易用性。

### 主要特性

- **统一的设备接口**：通过标准的device接口操作串口
- **多种工作模式**：支持阻塞/非阻塞收发，轮询/中断/DMA方式
- **缓冲区管理**：自动管理发送和接收缓冲区
- **事件回调**：支持数据收发完成回调、错误回调
- **硬件无关性**：通过抽象层实现硬件无关设计

## 架构说明

### 核心数据结构

#### 1. 串口设备结构 (hal_serial_s)

```c
typedef struct hal_serial {
    device_s            parent;        // 父设备结构
    serial_interface_t  interface;    // 串口接口函数指针
    serial_cfg_s        cfg;          // 串口配置
    serial_priv_s       __priv;       // 私有数据（内部使用）
} hal_serial_s;
```

#### 2. 串口配置结构 (serial_cfg_s)

```c
typedef struct serial_cfg {
    uint32_t    baudrate;      // 波特率
    data_bits_e dataBits;      // 数据位
    stop_bits_e stopBits : 2;  // 停止位
    parity_e    parity   : 2;  // 校验位
    flow_ctrl_e flowCtrl : 2; // 硬件流控
    oversampling_e overSampling : 1; // 采样率
    uint32_t    reserved : 25; // 保留位
    uint32_t    txBufSize;     // 发送缓冲区大小
    uint32_t    rxBufSize;     // 接收缓冲区大小
} serial_cfg_s;
```

#### 3. 设备接口结构 (DevInterface_s)

```c
typedef struct dev_interface {
    OmRet_e (*init)   (Device_t dev);
    OmRet_e (*open)   (Device_t dev, uint32_t oparam);
    OmRet_e (*close)  (Device_t dev);
    size_t     (*read)   (Device_t dev, void* pos, void *data, size_t len);
    size_t     (*write)  (Device_t dev, void* pos, void *data, size_t len);
    OmRet_e (*control)(Device_t dev, size_t cmd, void* args);
} DevInterface_s;
```

## 设备注册和初始化

### 1. 串口设备注册

```c
OmRet_e serial_register(hal_serial_t serial, char* name, void* handle, uint32_t regparams);
```

**参数说明：**
- `serial`：串口设备指针
- `name`：设备名称（用于查找设备）
- `handle`：底层硬件句柄
- `regparams`：注册参数（定义工作模式）

**注册参数示例：**
```c
// 中断方式收发，读写权限
uint32_t regparams = DEVICE_REG_STANDALONG | DEVICE_REG_RDWR | 
                    DEVICE_REG_INT_TX | DEVICE_REG_INT_RX;

// DMA方式收发
uint32_t regparams = DEVICE_REG_STANDALONG | DEVICE_REG_RDWR | 
                    DEVICE_REG_DMA_TX | DEVICE_REG_DMA_RX;

// 轮询方式
uint32_t regparams = DEVICE_REG_STANDALONG | DEVICE_REG_RDWR | 
                    DEVICE_REG_POLL_TX | DEVICE_REG_POLL_RX;
```

### 2. 设备初始化

```c
OmRet_e device_init(Device_t dev);
```

初始化设备，配置硬件参数。

### 3. 设备打开

```c
OmRet_e device_open(Device_t dev, uint32_t oparams);
```

**打开参数示例：**
```c
// 阻塞收发
uint32_t oparams = SERIAL_O_BLCK_RX | SERIAL_O_BLCK_TX;

// 非阻塞收发
uint32_t oparams = SERIAL_O_NBLCK_RX | SERIAL_O_NBLCK_TX;

// 混合模式
uint32_t oparams = SERIAL_O_BLCK_RX | SERIAL_O_NBLCK_TX;
```

## 数据收发操作

### 1. 数据发送

```c
size_t device_write(Device_t dev, void* pos, void *data, size_t len);
```

**参数说明：**
- `dev`：设备指针
- `pos`：位置参数（串口通常为NULL）
- `data`：发送数据缓冲区
- `len`：发送数据长度

**返回值：**实际发送的字节数

### 2. 数据接收

```c
size_t device_read(Device_t dev, void* pos, void *data, size_t len);
```

**参数说明：**
- `dev`：设备指针
- `pos`：位置参数（串口通常为NULL）
- `data`：接收数据缓冲区
- `len`：期望接收的数据长度

**返回值：**实际接收的字节数

## 配置和控制接口

### 1. 设备控制

```c
OmRet_e device_ctrl(Device_t dev, size_t cmd, void* args);
```

**支持的控制命令：**

| 命令 | 说明 | 参数类型 |
|------|------|----------|
| `SERIAL_CMD_SET_CFG` | 配置串口参数 | `serial_cfg_t` |
| `SERIAL_CMD_FLUSH` | 清空缓冲区 | `uint32_t` |
| `SERIAL_CMD_SUSPEND` | 挂起串口 | `NULL` |
| `SERIAL_CMD_RESUME` | 恢复串口 | `NULL` |

**控制命令语义补充（2026-02）**
- `SERIAL_CMD_SUSPEND`：进入挂起态后会下发底层停止动作，并主动唤醒阻塞收发等待者，防止 `WAIT_FOREVER` 类永久阻塞。
- `SERIAL_CMD_RESUME`：恢复后会重新下发当前设备的 IO 类型配置（中断/DMA），确保链路可继续收发。

### 2. 串口配置参数

**默认配置：**
```c
#define SERIAL_DEFAULT_CFG            \
    (serial_cfg_s)                    \
    {                                 \
        .baudrate  = 115200U,         \
        .dataBits  = DATA_BITS_8,     \
        .stopBits  = STOP_BITS_1,     \
        .parity    = PARITY_NONE,     \
        .flowCtrl  = FLOW_CTRL_NONE,  \
        .overSampling = OVERSAMPLING_16,\
        .txBufSize   = SERIAL_MIN_TX_BUFSZ, \
        .rxBufSize   = SERIAL_MIN_RX_BUFSZ  \
    }
```

**配置选项：**

- **数据位**：`DATA_BITS_5`, `DATA_BITS_6`, `DATA_BITS_7`, `DATA_BITS_8`, `DATA_BITS_9`
- **停止位**：`STOP_BITS_0_5`, `STOP_BITS_1`, `STOP_BITS_1_5`, `STOP_BITS_2`
- **校验位**：`PARITY_NONE`, `PARITY_ODD`, `PARITY_EVEN`
- **硬件流控**：`FLOW_CTRL_NONE`, `FLOW_CTRL_RTS`, `FLOW_CTRL_CTS`, `FLOW_CTRL_CTSRTS`
- **采样率**：`OVERSAMPLING_16`, `OVERSAMPLING_8`

## 回调函数设置

### 1. 读数据回调

```c
void device_set_rd_cb(Device_t dev, void (*callback)(Device_t dev, void* params, size_t paramsz));
```

### 2. 写数据回调

```c
void device_set_wd_cb(Device_t dev, void (*callback)(Device_t dev, void* params, size_t paramsz));
```

### 3. 错误回调

```c
void device_set_err_cb(Device_t dev, void (*callback)(Device_t dev, uint32_t errcode, void* params, size_t paramsz));
```

**错误码定义：**
- `ERR_SERIAL_RXFIFO_OVERFLOW`：接收FIFO溢出
- `ERR_SERIAL_TXFIFO_OVERFLOW`：发送FIFO溢出
- `ERR_SERIAL_INVALID_MEM`：内存非法
- `ERR_SERIAL_RX_TIMEOUT`：接收超时
- `ERR_SERIAL_TX_TIMEOUT`：发送超时

## 使用示例

### 示例2：带回调的异步通信

```c
void serial_rx_callback(Device_t dev, void* params, size_t paramsz)
{
    // 接收数据回调
    ringbuf_s* rb = (ringbuf_s*)params;
    size_t data_len = paramsz;
    
    char buf[64];
    size_t len = ringbuf_out(rb, buf, data_len);
    if (len > 0) {
        // 处理接收到的数据
        buf[len] = '\0';
        printf("Received: %s\n", buf);
    }
}

void serial_err_callback(Device_t dev, uint32_t errcode, void* params, size_t paramsz)
{
    // 错误处理
    switch (errcode) {
        case ERR_SERIAL_RXFIFO_OVERFLOW:
            printf("RX FIFO overflow!\n");
            break;
        case ERR_SERIAL_TXFIFO_OVERFLOW:
            printf("TX FIFO overflow!\n");
            break;
        default:
            printf("Serial error: %d\n", errcode);
    }
}

void serial_async_example(void)
{
    // 设置回调函数
    device_set_rd_cb((Device_t)&serial1, serial_rx_callback);
    device_set_err_cb((Device_t)&serial1, serial_err_callback);
    
    // 非阻塞模式打开
    uint32_t oparams = SERIAL_O_NBLCK_RX | SERIAL_O_NBLCK_TX;
    device_open((Device_t)&serial1, oparams);
}
```

### 示例3：动态配置串口

```c
void serial_reconfigure_example(void)
{
    // 创建新的配置
    serial_cfg_s new_cfg = SERIAL_DEFAULT_CFG;
    new_cfg.baudrate = 9600;
    new_cfg.dataBits = DATA_BITS_8;
    new_cfg.parity = PARITY_EVEN;
    
    // 应用新配置
    device_ctrl((Device_t)&serial1, SERIAL_CMD_SET_CFG, &new_cfg);
}
```

## 工作模式说明

### 1. 阻塞模式 (Blocking Mode)

- **特点**：函数调用会阻塞直到操作完成
- **适用场景**：简单的同步通信，实时性要求不高的应用
- **配置**：使用 `SERIAL_O_BLCK_RX` 和 `SERIAL_O_BLCK_TX`
- **中止语义**：阻塞路径支持“可中止等待”，当出现 `SUSPEND`、底层错误或等待超时时会返回，不再永久阻塞。
- **默认等待上限**：`SERIAL_BLOCK_TX_WAIT_TIMEOUT_MS` / `SERIAL_BLOCK_RX_WAIT_TIMEOUT_MS`（定义于 `pal_serial_dev.h`）。

### 2. 非阻塞模式 (Non-blocking Mode)

- **特点**：函数调用立即返回，通过回调处理数据
- **适用场景**：需要并行处理多个任务，实时性要求高的应用
- **配置**：使用 `SERIAL_O_NBLCK_RX` 和 `SERIAL_O_NBLCK_TX`

### 3. 传输方式

#### 轮询方式 (Polling)
- 简单可靠，占用CPU资源
- 适合低速通信或资源受限的系统

#### 中断方式 (Interrupt)
- 响应及时，CPU占用率低
- 适合中等速度的通信

#### DMA方式 (DMA)
- 最高效，几乎不占用CPU
- 适合高速大数据量通信

## 错误处理和调试

### 常见错误及解决方法

1. **设备注册失败**
   - 检查设备名称是否唯一
   - 确认注册参数是否正确

2. **数据收发异常**
   - 检查缓冲区大小是否足够
   - 确认工作模式配置是否正确
   - 查看错误回调中的错误码

3. **性能问题**
   - 调整缓冲区大小
   - 考虑使用DMA方式
   - 优化回调函数处理逻辑

### 调试技巧

1. **启用调试信息**
   - 在代码中的TODO位置添加日志输出
   - 使用错误回调记录异常情况

2. **性能监控**
   - 监控缓冲区使用情况
   - 统计数据收发速率

## 移植指南

### 需要实现的底层接口

用户需要为具体的硬件平台实现以下接口：

```c
typedef struct serial_interface 
{
    OmRet_e (*configure)(hal_serial_t serial, serial_cfg_t cfg);
    OmRet_e (*putByte)(hal_serial_t serial, uint8_t data);
    OmRet_e (*getByte)(hal_serial_t serial, uint8_t* buf);
    OmRet_e (*control)(hal_serial_t serial, uint32_t cmd, void* arg);
    size_t     (*transmit)(hal_serial_t serial, const uint8_t* data, size_t length);
} serial_interface_s;
```

### 中断处理

在硬件中断服务函数中调用：

```c
OmRet_e serial_hw_isr(hal_serial_t serial, serial_event_e event, void* arg, size_t arg_size);
```

**事件类型：**
- `SERIAL_EVENT_INT_TXDONE`：中断发送完成
- `SERIAL_EVENT_INT_RXDONE`：中断接收完成
- `SERIAL_EVENT_DMA_TXDONE`：DMA发送完成
- `SERIAL_EVENT_DMA_RXDONE`：DMA接收完成
- `SERIAL_EVENT_ERR_OCCUR`：错误中断

## 总结

OM串口框架提供了一个功能完整、易于使用的串口驱动解决方案。通过统一的设备接口，开发者可以快速实现串口通信功能，而无需关心底层硬件细节。框架支持多种工作模式和丰富的配置选项，能够满足不同应用场景的需求。

对于新手开发者，建议从简单的阻塞模式开始，逐步掌握非阻塞和回调机制的使用。在实际项目中，根据具体需求选择合适的工作模式和配置参数，以获得最佳的性能和可靠性。

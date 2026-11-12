# GPIO 子系统设计决策

> 本文档记录 GPIO 驱动子系统在详细设计前的关键决策及其理由。
> 仅供设计参考，不包含具体实现方案。

---

## 一、决策总表

| 编号 | 维度 | 决策 | 参考 |
|------|------|------|------|
| D1 | 职责边界 | GPIO I/O + 电气配置，不含引脚复用 | RT-Thread PIN |
| D2 | 架构集成 | 内部 Device 注册 + 对外专用 API | RT-Thread PIN |
| D3 | 抽象粒度 | 每控制器一 Device，通过控制器名+偏移寻址 | Zephyr |
| D4 | 引脚标识 | GpioPinSpec + GpioPin 双类型（Spec 编译时声明，Pin 运行时句柄） | Linux gpiod |
| D5 | 配置模型 | GpioPinConfig 配置结构体 | STM32 HAL / ESP-IDF |
| D6 | 中断模型 | 直接 ISR 回调 | Zephyr / RT-Thread |
| D7 | 批量操作 | 引脚级 + 端口级批量 | Zephyr |
| D8 | 依赖一致性 | 依赖 core + osal（仅 IRQ 路径），与分层规范一致 | — |
| D9 | 控制器结构 | GpioController 内嵌 Device parent，BSP 内嵌 GpioController parent | OM CAN 模式 |
| D10 | 线程安全 | 仅保护 IRQ 回调表（关中断），读写/配置不加锁 | RT-Thread / Zephyr |
| D11 | 设备类型 | Device 新增 DeviceType 字段，gpio_pin_get 校验类型 | — |
| D12 | IRQ 能力 | 控制器注册时声明 caps 位图，框架层校验请求模式 | Zephyr |
| D13 | 端口级语义 | 端口级 API 使用物理电平，引脚级使用逻辑电平 | Zephyr（提供 raw/logical 双接口） |
| D14 | BSP attach_irq | 仅配置 EXTI 路由和 NVIC，不调用 HAL_GPIO_Init，不修改电气配置 | — |

---

## 二、各项决策详述

### D1: 职责边界 — GPIO I/O + 电气配置

**包含**：
- 方向控制（输入/输出）
- 电平读写、翻转
- 上下拉配置（无/上拉/下拉）
- 驱动模式（推挽/开漏）
- 驱动强度（低/中/高，硬件相关，不支持的 BSP 返回 OM_ERROR_NOT_SUPPORT）
- 输出初始值
- 中断（边沿/电平触发）

**不包含**：
- 引脚复用（alternate function 选择）—— 由各外设 BSP 自行处理，维持现状

**理由**：
引脚复用与具体 SoC 的外设路由机制深度耦合（如 STM32 的 AF 映射表），抽象代价高且收益有限。当前 CAN/Serial 等外设的 BSP 层已通过直接调用厂商 HAL 完成引脚复用，引入新抽象层会破坏已有实现。GPIO 子系统聚焦"通用 GPIO"场景（LED、按键、片选、使能信号等），职责清晰，边界明确。

---

### D2: 架构集成 — 内部 Device + 专用 API

**决策**：
- GPIO 控制器在框架内部通过 Device 模型注册管理（参与设备链表、可 `device_find()`）
- 对外暴露专用 `gpio_pin_xxx` / `gpio_port_xxx` API，用户不直接操作 Device 的 read/write/control

**理由**：
GPIO 的操作语义（按引脚寻址的位操作）与 Device 模型的 read/write（缓冲区语义）不匹配。若强行映射到 `device_read/device_write`，参数编码不直观且增加用户理解成本。专用 API 能提供更自然的调用方式，同时内部复用 Device 基础设施（注册、查找、生命周期）保持框架一致性。

---

### D3: 抽象粒度 — 每控制器一 Device

**决策**：
- 每个 GPIO 控制器（片上端口或外部扩展器）注册为独立 Device
- 各控制器声明 `pin_count`（管理引脚数）
- 用户通过 `GpioPin` 句柄（含已解析的 `ctrl` 指针）操作引脚，框架通过 `ctrl` 直接路由到对应控制器

**理由**：
全局单例模式简单，但无法支持 GPIO 扩展器（I2C/SPI 芯片如 PCF8574、SX1509）。每控制器一 Device 的模式天然支持多控制器，且初始复杂度增加有限（核心仅多一步 pin 号→控制器的查找）。控制器的 ops 函数表按控制器粒度注册，不同控制器可有不同实现（片上 GPIO 直接寄存器操作 vs I2C 扩展器总线操作）。

---

### D4: 引脚标识 — GpioPinSpec + GpioPin 双类型

**决策**：
```c
// 编译时声明（static const，零运行时构建开销）
typedef struct {
    const char *controller;  // 控制器名称，如 "gpio0"
    uint8_t offset;          // 控制器内引脚偏移
    uint32_t flags;          // 引脚属性标志（ACTIVE_LOW 等）
} GpioPinSpec;

// 运行时句柄（gpio_pin_get() 解析一次，后续零查找开销）
typedef struct {
    GpioController *ctrl;    // 已解析的控制器指针
    uint8_t offset;          // 控制器内引脚偏移
    uint32_t flags;          // 引脚属性标志
} GpioPin;
```

使用方式：
```c
static const GpioPinSpec led_spec = { "gpio0", 19, 0 };
GpioPin led;
if (gpio_pin_get(&led_spec, &led) != OM_OK) return OM_ERROR;
gpio_pin_write(led, 1);  // 直接通过 ctrl 指针，无字符串查找
```

**理由**：
- **分离关注点**：`GpioPinSpec` 是平台描述（可 static const），`GpioPin` 是运行时句柄（含已解析指针）
- **ISR 安全**：`GpioPin` 的 `ctrl` 指针在解析时确定，ISR 中高频调用 `gpio_pin_write` 无字符串查找开销
- **跨平台一致性**：结构体通过控制器名 + offset 统一语义，不同平台编码规则差异被 `gpio_pin_get()` 封装
- **可扩展**：`flags` 字段承载 ACTIVE_LOW 等逻辑电平信息

---

### D5: 配置模型 — GpioPinConfig 配置结构体

**决策**：
```c
typedef struct {
    GpioDirection direction;      // INPUT / OUTPUT
    GpioPull pull;                // NONE / UP / DOWN
    GpioDrive drive;              // PUSH_PULL / OPEN_DRAIN
    GpioDriveStrength speed;      // LOW / MEDIUM / HIGH
    bool init_high;               // 输出初始逻辑电平（输出时有效，ACTIVE_LOW 自动反转）
} GpioPinConfig;
```
通过 `gpio_pin_configure(GpioPin pin, const GpioPinConfig *cfg)` 一次性配置。

**init_high 语义**：采用**逻辑电平**（与 `gpio_pin_write` / `gpio_pin_read` 一致）。框架层在传递给 BSP 前对 `GPIO_FLAG_ACTIVE_LOW` 引脚自动反转物理电平，确保用户始终用统一逻辑语义操作引脚。

**理由**：
- 各属性独立字段，自文档化，用户无需记忆位域宏含义
- 与 STM32 HAL 的 `GPIO_InitTypeDef`、ESP-IDF 的 `gpio_config_t` 模式一致，嵌入式开发者熟悉
- 扩展性好：新增属性只需添加字段，不影响已有代码
- 比枚举组合（`PIN_MODE_OUTPUT_OD`）更灵活，不会组合爆炸

---

### D6: 中断模型 — 直接 ISR 回调

**决策**：
```c
OmRet gpio_pin_attach_irq(GpioPin pin, GpioIrqMode mode,
                           void (*callback)(void *arg), void *arg);
OmRet gpio_pin_irq_enable(GpioPin pin, bool enable);
```
- `attach_irq`：注册回调函数和触发模式，不使能硬件中断
- `irq_enable`：使能/禁用硬件中断

回调在 ISR 上下文中执行，用户回调必须遵守 ISR 约束（不可阻塞、不可耗时）。

**理由**：
- 延迟最低，适合机器人场景中的实时响应（编码器脉冲、限位开关、紧急停止）
- attach/enable 分离允许先注册回调再条件使能，灵活控制中断生命周期
- ISR 热路径不依赖 osal/sync/ipc（回调表管理使用 osal_irq_lock，但不在 ISR 中），子系统依赖 core + osal
- 用户若需任务级处理，可在回调中自行使用 osal 原语（信号量 give、队列发送等）

---

### D7: 批量操作 — 引脚级 + 端口级批量

**决策**：
- **引脚级 API**：`gpio_pin_write` / `gpio_pin_read` / `gpio_pin_toggle` 等，操作单个 GpioPinSpec
- **端口级批量 API**：`gpio_port_write_masked` / `gpio_port_read` 等，按控制器名 + 位掩码操作多引脚

**理由**：
引脚级 API 覆盖绝大多数场景，端口级批量操作为并行接口、LED 矩阵、多路片选等高性能场景提供硬件原生效率。端口级 API 直接映射到硬件寄存器的掩码写入，不经过逐引脚循环，利用了 GPIO 控制器的原子多引脚操作能力。

---

### D8: 依赖一致性

**决策**：
- GPIO 子系统位于 `lib/drivers/`，依赖 `core`（类型/错误码）+ `osal`（`osal_irq_lock/unlock`、`osal_malloc/free`，IRQ 管理路径和控制器注册使用）
- API 头文件放入 `tar_awapi_driver`（headeronly target），实现放入 `tar_awdrivers`（static target）
- 不依赖 sync/ipc/services/systems，不 include bsp 头文件

**理由**：
GPIO 是基础驱动，被其他模块使用（如 SPI 驱动的片选引脚），依赖越轻越好。热路径（pin_read/pin_write）仅依赖 core，IRQ 回调表保护需要 `osal_irq_lock/unlock`。`drivers → osal` 依赖方向符合分层规范。

---

### D9: 控制器结构体 — 沿用 OM PAL 模式

**决策**：
框架层 `GpioController` 内嵌 `Device parent`，BSP 层结构体内嵌 `GpioController parent`。与 CAN 的 `HalCanHandler` + `BspCan` 模式完全一致。

```c
// 框架层（lib/drivers/）
typedef struct GpioController {
    Device parent;               // 内嵌 Device，参与设备链表
    const GpioOps *ops;          // BSP 注入的硬件操作函数表
    uint8_t pin_count;           // 管理的引脚数量
    uint32_t caps;               // IRQ 能力位图
    void *priv;                  // BSP 私有数据
    // IRQ 回调表（框架层管理）
    struct GpioIrqHdr *irq_hdrs;
} GpioController;

// BSP 层（platform/bsp/boards/<board>/）
typedef struct BspGpio {
    GPIO_TypeDef *port;          // STM32 HAL 端口指针（放首位方便强转）
    GpioController parent;       // 框架层父结构体
    char *name;
    uint8_t irq_priority;        // NVIC 优先级（板级配置）
} BspGpio;
```

**理由**：
- 与 CAN/Serial 的 PAL 适配模式一致，BSP 开发者无需学习新模式
- `Device.parent` 保证控制器可被 `device_find()` 查找（`gpio_pin_get()` 内部使用）
- BSP 通过 `GpioController.priv` 或 container_of 访问私有数据

---

### D10: 线程安全 — 仅保护 IRQ 回调表

**决策**：
- `gpio_pin_attach_irq` / `gpio_pin_detach_irq` 内部关中断保护回调表，防止与 ISR 并发修改
- `gpio_pin_write` / `gpio_pin_read` / `gpio_pin_toggle` / `gpio_pin_configure` / `gpio_pin_irq_enable` 不加锁
- 端口级批量操作不加锁
- `gpio_controller_register` 仅在初始化阶段调用

```c
// attach_irq 内部
OmRet gpio_pin_attach_irq(GpioPin pin, GpioIrqMode mode,
                           void (*cb)(void *), void *arg) {
    int key = osal_irq_lock();
    irq_hdrs[offset].callback = cb;
    irq_hdrs[offset].arg = arg;
    osal_irq_unlock(key);
    return OM_OK;
}
```

**调用者义务**：
- 同一引脚的 configure 不会与 write/read 并发（方向变更期间不应读写）
- 端口级批量操作不会在多上下文中对同一控制器并发
- `gpio_controller_register` 仅在初始化阶段调用

**理由**：
- 引脚读写为单寄存器操作（MCU 的 BSRR/BRR 等位操作寄存器硬件保证原子），加锁无意义
- IRQ 回调表是唯一真正的竞态风险点——`attach_irq` 修改表时 ISR 可能正在查表调用，必须保护
- 与 RT-Thread（`rt_hw_interrupt_disable`）、Zephyr（`irq_lock`）、ESP-IDF（spinlock）做法一致
- 关中断保护仅覆盖回调表赋值（几条指令），对 ISR 延迟影响可忽略
- 此决策引入对 osal 的 `osal_irq_lock/unlock` 依赖，但仅限于 IRQ 管理路径，不影响 pin_read/pin_write 的零依赖热路径

---

## 三、API 轮廓（非最终接口）

以下为基于上述决策的 API 形态预览，仅供设计参考：

```c
/* ===== 引脚标识（双类型） ===== */

// 编译时声明（static const）
typedef struct {
    const char *controller;      // 控制器名称
    uint8_t offset;              // 控制器内引脚偏移
    uint32_t flags;              // GPIO_FLAG_ACTIVE_LOW 等
} GpioPinSpec;

// 运行时句柄（gpio_pin_get() 解析一次，后续零查找开销）
typedef struct GpioPin {
    struct GpioController *ctrl; // 已解析的控制器指针
    uint8_t offset;              // 控制器内引脚偏移
    uint32_t flags;              // 引脚属性标志
} GpioPin;

// 解析 + 有效性检查
OmRet gpio_pin_get(const GpioPinSpec *spec, GpioPin *pin);
bool  gpio_pin_valid(GpioPin pin);

/* ===== 引脚配置 ===== */
typedef enum { GPIO_DIR_INPUT, GPIO_DIR_OUTPUT } GpioDirection;
typedef enum { GPIO_PULL_NONE, GPIO_PULL_UP, GPIO_PULL_DOWN } GpioPull;
typedef enum { GPIO_DRIVE_PUSH_PULL, GPIO_DRIVE_OPEN_DRAIN } GpioDrive;
typedef enum { GPIO_DRIVE_STRENGTH_LOW, GPIO_DRIVE_STRENGTH_MEDIUM, GPIO_DRIVE_STRENGTH_HIGH } GpioDriveStrength;

typedef struct {
    GpioDirection direction;
    GpioPull pull;
    GpioDrive drive;
    GpioDriveStrength speed;
    bool init_high;               // 输出初始逻辑电平（ACTIVE_LOW 自动反转）
} GpioPinConfig;

/* ===== 中断 ===== */
typedef enum {
    GPIO_IRQ_EDGE_RISING,
    GPIO_IRQ_EDGE_FALLING,
    GPIO_IRQ_EDGE_BOTH,
    GPIO_IRQ_LEVEL_HIGH,
    GPIO_IRQ_LEVEL_LOW,
} GpioIrqMode;

/* ===== 引脚级 API（以 GpioPin 句柄为参数） ===== */
OmRet   gpio_pin_configure(GpioPin pin, const GpioPinConfig *cfg);
void    gpio_pin_write(GpioPin pin, uint8_t value);
uint8_t gpio_pin_read(GpioPin pin);
void    gpio_pin_toggle(GpioPin pin);

/* ===== 中断 API ===== */
OmRet gpio_pin_attach_irq(GpioPin pin, GpioIrqMode mode,
                           void (*callback)(void *arg), void *arg);
OmRet gpio_pin_detach_irq(GpioPin pin);
OmRet gpio_pin_irq_enable(GpioPin pin, bool enable);

/* ===== 端口级批量 API ===== */
typedef struct {
    GpioController *ctrl;
} GpioPort;

GpioPort gpio_port_get(const char *controller);
bool     gpio_port_valid(GpioPort port);
OmRet   gpio_port_write_masked(GpioPort port, uint32_t mask, uint32_t value);
OmRet   gpio_port_set_bits(GpioPort port, uint32_t pins);
OmRet   gpio_port_clear_bits(GpioPort port, uint32_t pins);
OmRet   gpio_port_toggle_bits(GpioPort port, uint32_t pins);
uint32_t gpio_port_read(GpioPort port);

/* ===== 控制器注册（BSP 侧） ===== */
typedef struct GpioOps {
    OmRet   (*pin_configure)(GpioController *ctrl, uint8_t offset, const GpioPinConfig *cfg);
    void    (*pin_write)(GpioController *ctrl, uint8_t offset, uint8_t value);
    uint8_t (*pin_read)(GpioController *ctrl, uint8_t offset);
    void    (*pin_toggle)(GpioController *ctrl, uint8_t offset);
    OmRet   (*pin_attach_irq)(GpioController *ctrl, uint8_t offset, GpioIrqMode mode,
                               void (*cb)(void *), void *arg);
    OmRet   (*pin_irq_enable)(GpioController *ctrl, uint8_t offset, bool enable);
    // 端口级（可选，NULL 表示不支持）
    OmRet   (*port_write_masked)(GpioController *ctrl, uint32_t mask, uint32_t value);
    OmRet   (*port_set_bits)(GpioController *ctrl, uint32_t pins);
    OmRet   (*port_clear_bits)(GpioController *ctrl, uint32_t pins);
    OmRet   (*port_toggle_bits)(GpioController *ctrl, uint32_t pins);
    uint32_t (*port_read)(GpioController *ctrl);
} GpioOps;

OmRet gpio_controller_register(GpioController *ctrl,
                                const char *name,
                                uint8_t pin_count,
                                uint32_t caps, const GpioOps *ops, void *priv);

/* ===== 控制器结构体（框架内部） ===== */
typedef struct GpioController {
    Device parent;               // 内嵌 Device
    const GpioOps *ops;          // BSP 注入的硬件操作函数表
    uint8_t pin_count;
    uint32_t caps;               // IRQ 能力位图
    void *priv;                  // BSP 私有数据
    GpioIrqHdr *irq_hdrs;        // 中断回调表
} GpioController;
```

---

## 四、迭代补充决策（代码审查后）

### D11: 设备类型标识

**决策**：
`Device` 结构体新增 `DeviceType type` 字段，`gpio_pin_get()` / `gpio_port_get()` 查找设备后校验 `type == DEVICE_TYPE_GPIO`，非 GPIO 设备拒绝访问。

**理由**：
`device_find()` 返回任意 `Device*`，直接强转为 `GpioController*` 无类型验证，存在未定义行为风险。通过类型标识在框架层拦截非法访问，开销仅为一次整数比较。

### D12: IRQ 能力位图

**决策**：
`GpioController` 新增 `uint32_t caps` 字段，注册时声明支持的 IRQ 触发模式。位号与 `GpioIrqMode` 枚举值对齐，`1U << mode` 直接查询。`gpio_pin_attach_irq()` 在框架层校验 `caps & (1U << mode)`，不支持时返回 `OM_ERROR_NOT_SUPPORT`。

```c
#define GPIO_CAP_IRQ_EDGE_RISING   (1U << GPIO_IRQ_EDGE_RISING)
#define GPIO_CAP_IRQ_EDGE_FALLING  (1U << GPIO_IRQ_EDGE_FALLING)
#define GPIO_CAP_IRQ_EDGE_BOTH     (1U << GPIO_IRQ_EDGE_BOTH)
#define GPIO_CAP_IRQ_LEVEL_HIGH    (1U << GPIO_IRQ_LEVEL_HIGH)
#define GPIO_CAP_IRQ_LEVEL_LOW     (1U << GPIO_IRQ_LEVEL_LOW)
```

STM32F4 EXTI 仅支持边沿触发，BSP 注册时传入 `GPIO_CAP_IRQ_EDGE_RISING | GPIO_CAP_IRQ_EDGE_FALLING | GPIO_CAP_IRQ_EDGE_BOTH`。

**理由**：
PAL 枚举定义了 5 种模式，但具体硬件可能只支持子集（如 STM32 EXTI 不支持电平触发）。能力位图让用户在调用 `attach_irq` 时立即得到明确的"不支持"反馈，而非依赖 BSP 层的 switch-default。

### D13: 端口级 API 物理电平语义

**决策**：
端口级 API（`gpio_port_write_masked` 等）使用**物理电平**语义，不经过 `GPIO_FLAG_ACTIVE_LOW` 反转。引脚级 API（`gpio_pin_write` 等）使用逻辑电平语义。

**理由**：
Zephyr 为端口级 API 同时提供逻辑和 `_raw` 物理变体，当前设计出于简洁性考虑仅提供物理接口。`GpioPort` 不携带 per-pin flags，无法实现 per-pin ACTIVE_LOW 反转。后续可按需扩展逻辑变体。使用端口级 API 的场景（并行接口、LED 矩阵）通常已知硬件布局，物理语义更直观。

### D14: BSP attach_irq 不修改电气配置

**决策**：
BSP 层 `pin_attach_irq` 实现仅操作 EXTI 相关寄存器（SYSCFG EXTICR、EXTI RTSR/FTSR/IMR、NVIC），不调用 `HAL_GPIO_Init`，不修改 MODER/PUPDR/OSPEEDR 等电气配置寄存器。用户需先通过 `gpio_pin_configure()` 将引脚配置为输入模式（含所需的上下拉），再调用 `attach_irq`。

**理由**：
`HAL_GPIO_Init` 会重写所有引脚配置，导致用户通过 `gpio_pin_configure` 设置的 pull 被覆盖。将电气配置与中断配置解耦后，两者职责清晰：`configure` 管电气，`attach_irq` 管路由和触发。NVIC 优先级从 BSP 板级配置（`BspGpio.irq_priority`）读取，不再硬编码。

---

## 五、参考来源

调研详见 `docs/gpio_subsystem_survey.md`（workspace 目录）。

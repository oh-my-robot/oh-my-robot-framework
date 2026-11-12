# ADR-0029：驱动框架、器件库与平台适配边界

- 状态：已决策（2026-11-12，审核通过）
- 日期：2026-11-12
- 关联：OMR-13 / OMR-14 / OMR-15 / OMR-16 / OMR-17；ADR-0002 (motor_unified_model_top_level_spec)、ADR-0003 (motor_vendor_adapter_boundary)、ADR-0004 (motor_semantic_contract_index)、ADR-0005 (can_architecture)、ADR-0006 (can_rx_modeling_selection)、ADR-0012 (vendor_adapter_board_data)、ADR-0013 (framework_owned_main)、ADR-0014 (fatal_error_and_startup_split)、ADR-0016 (drivers_services_one_way_dependency)、ADR-0017 (project_config_layering)、ADR-0022 (osal_none_bare_metal)、ADR-0027 (flash_pal_drop_async_domain)、ADR-0028 (storage_evolution_lines)

## 背景 (Context)

`lib/drivers` 当前同时承载三类内容：

1. 通用驱动框架：`Device` 模型、GPIO/SPI/PWM/CAN/Flash/Serial 的总线或设备类框架、PAL 契约、分区表抽象。
2. 具体器件库：DJI 电机、P1010B 电机等厂商协议实现。
3. 服务集成：串口日志后端、设计中的日志持久化后端。

现有代码没有 `drivers -> platform` 的代码依赖；问题不是反向依赖，而是**职责混在一起**：

- 具体厂商器件与框架共用 `drivers/` 命名空间和 `tar_awdrivers` 静态库，新增器件会改动框架构建与发布。
- 框架公共头与厂商协议头混在一起，无法用构建系统强制“器件只能依赖框架公开契约”。
- `lib/drivers` 内的空 `motor` 占位文件与未实现的 `MotorCore` 让“框架 / 器件”边界不可判定。
- 服务集成放在 `peripheral/` 或 `storage/` 下，容易被误认为器件。

平台侧已经按 ADR-0012 收敛为“vendor 共享适配层 + 板数据表”：`platform/bsp/vendor/<V>/<family>/adapters/<periph>/` 提供 ops 实现与 ISR，板侧只保留实例/引脚/时钟/中断数据。这个模式需要与“器件库”划清边界。

## 考虑过的方案 (Options)

### 方案 A：维持现状，器件继续留在 `lib/drivers`

新增器件继续修改 `tar_awdrivers` 与框架 include 树；框架发布与器件发布绑定。无法用构建系统阻止器件反向依赖 platform。否决。

### 方案 B：把整个 `lib/drivers` 外置为独立仓库

框架失去设备模型、总线框架与 PAL 契约的稳定归属；platform 需要反向依赖外置仓库，破坏“实现依赖抽象”的方向。否决。

### 方案 C：三层拆分，器件库独立成仓库（采纳）

- 驱动框架留在框架仓库 `lib/drivers`，只保留设备模型、总线/设备类框架、PAL 契约、通用策略与框架级服务集成。
- 具体器件库独立为 `oh-my-robot-devices` 仓库，依赖驱动框架公开头与 kernel/sync，不依赖 platform/BSP/vendor SDK。
- 平台适配留在 `platform/bsp`，实现驱动框架的 ops 契约，不引用器件库。

### 方案 D：只在树内拆 `framework/` 与 `devices/` 目录，不独立仓库

可以作为迁移阶段，但不能作为终态：器件仍随框架发布，构建隔离与独立版本仍不成立。采纳为 OMR-15 的过渡形态，不作为最终结构。

### 方案 E：用 weak 符号或自动初始化实现器件扩展

静态库按需抽取下，未被引用的 strong 实现不会被抽取，weak 覆盖可能失效；器件的隐式自注册还会隐藏依赖。项目已有明确约束（应用层不把 weak 作扩展机制）。否决。

## 最终决策 (Decision)

### 1. 三层与依赖规则

**驱动框架（framework）**

- 设备模型与注册表：`Device` / `DevInterface` / `device_register` / `device_find`。
- 外设总线与设备类框架：GPIO、SPI、PWM、CAN、Flash、Serial 的公共 API、状态机、缓冲/完成/超时策略与 PAL 契约。
- 通用存储语义：分区表抽象。
- 框架级服务集成：串口日志后端、未来的日志持久化后端。
- 只依赖 kernel、sync、async、data_struct；可按开放清单依赖 services 的公开接口（当前仅 `services/log/log.h`，ADR-0016）。

**器件库（devices）**

- 具体厂商或协议器件：DJI 电机、P1010B 电机、W25Q NOR 等。
- 依赖驱动框架公开头、kernel、sync；不得包含 `platform/`、`third_party/`、BSP 头、厂商 SDK 头或 `drivers/src/` 私有路径。
- 不持有板级数据；不创建 RTOS 线程/定时器；不接管系统执行策略。
- 对外可以使用 `Device` 模型，也可以提供类型化 API；是否注册为 `Device` 由器件语义决定，不强制统一。

**平台适配（platform）**

- 厂商 SDK 适配、板级数据、引脚/时钟/中断、ops 实现。
- 只实现驱动框架的公开契约；不得引用器件库头文件或器件类型。
- vendor SDK 只存在于 `platform/bsp/vendor/` 与 `third_party/`；公共驱动头不得暴露 vendor 类型或宏。

**一句依赖规则**：

> 器件库与平台适配互不依赖；两者都只依赖驱动框架的公开契约，由板级/应用完成实例接线。

### 1.1 术语

- **协议厂商适配器（protocol vendor adapter）**：器件库内的具体器件协议实现，例如 DJI、P1010B。本文简称“器件”。
- **芯片/硅厂商适配器（silicon vendor adapter）**：`platform/bsp/vendor/` 下对 STM32/TI 等 SDK 的适配，例如 `bsp_can_f4.c`。本文简称“平台适配”。
- “vendor adapter”单独出现时含义不明确，后续文档必须写全“协议厂商适配器”或“芯片厂商适配器”。

### 1.2 与既有依赖规则的关系

- `platform` 依赖 `drivers` 的 PAL 抽象头是合法的（`docs/architecture/architecture_reference_manual.md` 的“实现依赖抽象”规则）；ADR-0016 中“platform 只能依赖 kernel/third_party”的绝对措辞不适用于 PAL 抽象头，本 ADR 以架构参考手册为准。
- `drivers` 内部并非所有模块互不依赖：SPI 框架引用 GPIO PAL，电机器件引用 CAN PAL。器件库允许依赖框架公开的跨家族 API 头（如 SPI Flash 依赖 SPI + GPIO，电机依赖 CAN），但禁止依赖 `drivers/src/` 私有实现头或其他器件的私有状态。
- 本 ADR 不改变 ADR-0013 (framework_owned_main)、ADR-0014 (fatal_error_and_startup_split) 的框架级 weak 例外；器件库不得新增 weak 扩展点。

### 2. 现有文件归属与处置

| 当前路径 / 族 | 归属 | 处置 |
|---|---|---|
| `lib/drivers/include/drivers/model/device.h`、`src/model/device.c` | 框架 | 留在 `lib/drivers` |
| `peripheral/{gpio,spi,pwm,can,flash,serial}/pal_*` 与 `src/.../hal_*.c` | 框架 | 留在 `lib/drivers`；这些是总线/设备类框架，不是具体器件 |
| `peripheral/pal_dev.h` | 框架 | 留在 `lib/drivers`；后续清理死伞头 |
| `include/drivers/storage/partition.h`、`src/storage/partition.c` | 框架（存储语义） | 留在 `lib/drivers`；不随器件库移动 |
| `include/drivers/peripheral/serial/log_serial_backend.h`、`src/peripheral/serial/log_serial_backend.c` | 框架（服务集成） | 留在 `lib/drivers`；位置暂不变 |
| `include/drivers/motor/vendors/dji/**`、`src/motor/vendors/dji/**` | 器件库 | 迁移到 `oh-my-robot-devices` |
| `include/drivers/motor/vendors/direct_drive/**`、`src/motor/vendors/direct_drive/**` | 器件库 | 迁移到 `oh-my-robot-devices` |
| `include/drivers/motor/vendors/damiao/damiao.h` | 器件库占位 | 空文件，OMR-15 删除；有实现时再在器件库新增 |
| `include/drivers/motor/motor.h`、`motor_drv.h`；`src/motor/motor.c`、`motor_ctrller.c`、`motor_drv.c` | 框架占位（未实现） | OMR-15 删除。`MotorCore` 不在 v0.2.0 实现 |
| 设计中的 `log_persist` | 框架（服务集成） | 留在 `lib/drivers`，后续集中到 `drivers/integration/log/` |
| 厂商协议文档、PDF、器件使用说明 | 器件库 | 随器件迁移到 `oh-my-robot-devices/docs/` |

`lib/drivers/include/drivers.md` 保留，但只描述框架命名空间；器件库使用 `devices/` 命名空间，并新增自己的入口说明。

### 3. 扩展、发现与注册方式

- 框架只提供显式注册表：`device_register` / `device_find`、`spi_bus_register`、`can_register` 等。器件通过公开 API 注册或由应用显式初始化。
- **禁止用 weak 符号实现器件发现或覆盖**。
- 器件库默认不依赖 `OM_INIT_*` 自注册；应用或板级 profile 层在 `OM_INIT_APPLICATION` / 显式 init 中调用器件初始化。`oh_my_robot.selfreg` 不扩展到器件库；若某个器件确实使用自注册，必须使用 `OM_INIT_DRIVER`（level 2，晚于 BOARD provider），且源文件直接编入 binary（不得只放在静态库中依赖抽取），并在器件库文档中声明。
- 器件实例的绑定方式沿用当前样式的显式接线：板级数据提供命名总线/设备（如 `device_find("can1")`），应用把总线句柄与器件配置交给器件库。框架不自动发现器件。
- 器件库 API 接收 `Device*` / `SpiBus*` / `GpioPinSpec` / 配置结构，不接收板级名称或总线索引；`device_find("can1")`、`gpioa`、`spi_device_attach(busIdx)` 属于应用/板级接线，不进入器件库。SPI 器件使用 `SpiBus*` 或稳定句柄，不使用注册序号。
- 器件库接收 `Device*` 时必须校验 `DeviceType`（CAN/Serial/SPI 当前没有 typed find）；`DeviceType` 是框架闭集，器件库不得扩展。新增设备类需要框架变更或另立 ADR。
- 若需要“器件描述表”，表由应用或器件库的显式入口持有；框架不提供隐式构造/自动注册。

**器件的最小契约**

- 框架管理的总线/外设实例统一实现或内嵌 `Device`，实现 `DevInterface`，通过 `device_register` 进入全局注册表。
- 器件库的类型化对象（电机、传感器等）不要求注册进 `g_dev_list`，可以只消费 `Device*` 总线句柄并暴露自己的类型化 API；这类对象由应用持有。
- 不存在第三种发现机制：要么是框架管理的 `Device`，要么是应用持有的类型化对象。器件库不得另建全局注册表。
- v0.2.0 保留全局 `device_find` 注册表用于框架管理的总线/外设；不为器件库扩展 `DeviceType`。电机、传感器等器件类型不进入 `DeviceType` 枚举；需要设备类抽象时另立 ADR。
- 器件库公开 API 在 v0.2.0 不加入 ADR-0004 (motor_semantic_contract_index) 的 `MM-<domain>-<nnn>` 语义合同索引；未来 `MotorCore` 落地时，其适配合同按 ADR-0004 编号。

### 4. 平台、板级与厂商边界

- 板级数据只定义总线实例、引脚、时钟、中断、DMA 等平台事实；不包含具体器件类型或器件协议配置。
- 器件实例配置（电机 ID/型号、Flash 几何、传感器地址等）由应用或板级 profile 层持有；该层可以依赖器件库，但 `platform/bsp` 不得依赖器件库。
- 平台适配实现框架 ops（`SpiControllerOps`、`CanHwInterface`、`GpioOps`、`PwmOps`、`FlashOps` 等），只包含框架公开头和 vendor SDK 头。
- 公共驱动头不得暴露 STM32/TI/vendor 类型、宏或寄存器定义；当前代码已满足，后续由构建隔离与 CI 检查保持。
- CAN 公共头当前带有控制器族词汇（mailbox、filter bank 等）。v0.2.0 将其视为**控制器族契约**，不是通用 CAN PAL；器件库只使用 `CanUserMsg` / `device_*` 等设备 API，不得直接使用硬件 mailbox/filter-bank 字段。CAN PAL 的进一步通用化另立 ADR，不作为本次拆分的前置条件。
- CAN 框架拥有过滤器分配、入口队列、dispatcher、路由与时间戳（ADR-0005 (can_architecture) / ADR-0006 (can_rx_modeling_selection)）；器件库只在框架提供的回调上下文中做协议解码与器件状态维护，不在 ISR 路径阻塞，不直接操作 filter bank/mailbox。ADR-0003 (motor_vendor_adapter_boundary) 的 `TransportAdapter` 在 CAN 路径上不得绕过 CAN 框架的资源管理；其时间戳/路由职责以 ADR-0005/0006 为准。
- **器件族共性**（同一器件厂商/协议族的共享逻辑与状态，例如 W25Q 族、DJI 族）属于器件库；**控制器/芯片族共性**（同一 MCU 家族的 CAN/SPI 控制器差异）属于 platform 适配层或框架已有的控制器抽象。两者不得混放。
- 器件库必须能在 `osal-none` 下编译；不得假设线程/定时器存在；需要延时或异步执行时，使用框架/OSAL 抽象，或把执行上下文留给应用。器件库不创建线程、定时器或工作队列。
- `pal_flash_dev.h` / `pal_serial_dev.h` 目前把 PAL 契约与框架策略放在同一头文件；v0.2.0 接受这种形态，边界由依赖方向与构建隔离保证，不在本次拆分中强行拆头。
- OMR-15 / OMR-16 首批只覆盖已有板数据契约的 CAN/SPI 器件；其他外设的器件化待其板数据契约完成后推进，本 ADR 不假设所有外设都已完全数据化。

### 5. 服务集成与例外

服务集成不是器件，留在驱动框架，但必须有固定归属：

- 串口日志后端：位置暂不变（当前在 `peripheral/serial/`）；后续清理时集中到 `drivers/integration/log/`，本 ADR 不在 OMR-15 强制迁移，避免一次性改动多个 sample 的 include 路径。
- 日志持久化后端：`drivers/integration/log/`（实现时落地；依赖 partition 与 `services/log` 公开头）。
- 这些模块属于框架的“服务适配”例外，不属于器件库；器件库不得把日志服务当作自己的依赖。
- `drivers -> services` 的开放清单当前只有 `services/log/log.h`。器件库如确需日志，只允许包含 `services/log/log.h`；`services/log` 的实现当前经 `oh_my_robot.selfreg` 直接注入 binary，器件仓库文档必须要求应用启用 selfreg，否则会出现未解析符号。
- **comm 相关内容不在本 ADR 范围**：不规划、不新增 `services/comm` 依赖、不修改 `comm_adapter_boundary.md`、不扩展 `drivers -> services` 开放清单。

### 6. 仓库、构建与版本

**目标结构**

框架仓库：

```
lib/drivers/
  include/drivers/          # 框架公共头
  src/                      # 框架实现 + integration/
  docs/                     # 框架设计文档
```

器件仓库 `oh-my-robot-devices`：

```
include/devices/            # 器件公共头
src/                        # 器件实现
docs/                       # 器件协议/使用文档
samples/                    # 器件示例
xmake.lua                   # tar_awapi_devices / tar_awdevices
```

**迁移期（OMR-15）**

在框架仓库根新增临时 `devices/` 目录（与 `lib/` 平级），作为抽取前的 staging；OMR-15 完成后 `lib/drivers` 只编译框架，`devices/` 由独立的 `tar_awdevices` 目标编译。OMR-16 把 `devices/` 整体迁出为独立仓库，框架删除该目录。

**构建**

- `tar_awapi_driver` 保持框架公共头目标，不包含器件头。
- `tar_awdrivers` 只编译 `lib/drivers/src/**.c`；不再包含厂商器件源。
- 器件仓库提供 `tar_awapi_devices`（header-only）与 `tar_awdevices`（static）。`tar_awdevices` 只依赖 `tar_awapi_devices` / `tar_awapi_driver` 与必要的 kernel/sync/async 头目标，不依赖 `tar_oh_my_robot`、`tar_awdrivers`、`tar_bsp` 或 platform 目标；链接闭包由应用提供。
- 应用/生成的 workspace 同时链接 `tar_oh_my_robot` 与 `tar_awdevices`；框架聚合目标 `tar_oh_my_robot` 不包含器件。
- `tar_awdevices` 必须挂 `oh_my_robot.context` 与 `oh_my_robot.project_cfg` 规则，并注册配置状态 depfile；否则 arch flags、`FRAMEWORK_VERSION`、appcfg/boardcfg 与变更感知会和框架源不一致。器件目标同样加入 LTO 的 GNU ARM safe-target 允许表；OMR-17 修复 LTO 包装层丢弃 target identity 的问题，并给 `link_guard` 增加器件 provider/registry 符号检查。
- 器件库与框架必须使用同一工具链、同一 board/os 配置、同一 `OM_USE_*` / OSAL portdef 构建；v0.2.0 不承诺跨配置预编译 ABI。器件代码不得依赖 `OM_BOARD_*` 等板级宏。
- **`init_workspace`**：
  - 新增 `--devices=<path>`；生成第二个目录链接 `oh-my-robot-devices`。
  - 生成的 `xmake.lua` 先 `includes("oh-my-robot")`，再 `includes("oh-my-robot-devices")`（框架规则先注册），binary 目标 `add_deps("tar_oh_my_robot", "tar_awdevices")`。
  - entry 必须区分 framework 与 devices 仓库；OMR-15 支持 in-tree `devices/` staging，OMR-16 切换到外部 alias。默认 entry 改为框架示例；器件示例由器件仓库提供。
  - `.gitignore` 与 README 同时包含两个 alias；`--force` 清理两个 alias。
  - CI 冒烟门禁不仅 grep 规则名，还要实际 `configure + build` 生成的 shell，并检查 ELF 产物。
- 器件仓库单独 clone 时，通过生成的 workspace（推荐）或显式 `OM_FRAMEWORK_ROOT`/preset 定位框架；不假设裸 clone 后直接 `xmake` 能找到框架。
- 框架发布产物不包含器件库；器件仓库独立发布自己的库与示例。框架版本与器件版本不锁步，器件仓库用自己的 SemVer；兼容范围由后续 `compat.yaml`（OMR-28）声明。

**MotorCore**

ADR-0002 / ADR-0003 的 `MotorCore + VendorAdapter + TransportAdapter` 仍是未来框架层方向，但 v0.2.0 不实现。空的 `motor` 占位文件在 OMR-15 删除；厂商电机驱动按现状迁入器件库。未来实现 MotorCore 时，它属于驱动框架，厂商适配器属于器件库，二者通过公开契约连接；该迁移另立任务。

### 7. OMR-14 关闭条件与迁移边界

**OMR-14 的关闭条件**

- ADR 含「背景 / 考虑过的方案 / 最终决策 / 影响」四节。
- 三层边界可用一句依赖规则表述，并能判定现有 `lib/drivers` 每个文件的归属。
- 给出 v0.2.0 内可执行的迁移批次与回滚方式。
- 文档评审通过，无未决的边界问题。

**不是 OMR-14 的关闭条件**

- 创建 `devices/` staging 目录。
- 移动任何源码、修改 include、改构建目标、改 CI。
- 抽取 `oh-my-robot-devices` 仓库或修改 `init_workspace`。

这些属于 OMR-15 / OMR-16 / OMR-17，OMR-14 只做决策文档。

**OMR-15（树内拆分）**

1. 新建临时 `devices/`（include/src/docs/samples）与 `tar_awapi_devices` / `tar_awdevices`；器件目标挂 context/project_cfg 规则。
2. 迁移 DJI、P1010B 的公共头、实现与文档；删除空的 damiao 占位与空 motor 占位。
3. W25Q 当前不在 checkout 中（仅有设计文档）；OMR-15 先导入器件实现，再按器件库落位，不把 W25Q 放进框架。
4. 更新 `samples/motor/*` 的 include 路径与依赖；`init_workspace` 在 OMR-15 暂时支持 devices staging。
5. `tar_awdrivers` 只编译框架；`tar_oh_my_robot` 不包含器件。
6. 验证：host tests 不变；rm-a / rm-c × gnu-rm / armclang 构建通过；DJI/P1010B 示例行为不变。
7. 回滚：OMR-15 为独立分支/提交，回滚即恢复 `lib/drivers` 原树；不保留兼容转发头，避免框架反向依赖器件。

**OMR-16（抽取独立仓库）**

1. 以 `devices/` 建立 `oh-my-robot-devices` 仓库与 xmake 工程；器件仓库单独 clone 时通过生成的 workspace 或 `OM_FRAMEWORK_ROOT`/preset 定位框架。
2. `init_workspace --devices` 生成双仓工程；默认 entry 改为框架示例，器件示例由器件仓库提供。
3. 把器件 samples/docs 迁入器件仓库；框架仓库删除 `devices/`。
4. 器件仓库 CI 以 pinned 框架版本构建；框架 CI 增加“框架 + 器件示例”集成构建。
5. 回滚：在 OMR-16 验证完成前保留框架内 `devices/` 作为回退；验证通过后删除。若器件仓库构建失败，应用可暂时 pin 上一器件提交。

**OMR-17（收尾与门禁）**

1. 加入构建级依赖门禁：器件目标只能看到框架公开 include；扫描 `platform/`、`third_party/`、`bsp`、vendor SDK、`drivers/src/` 私有路径；加入一个故意反向依赖的负向 fixture，确认门禁会失败。
2. 更新架构参考手册与 `lib/drivers/include/drivers.md`：明确 framework / devices / platform 三层与依赖规则。
3. CI 矩阵：framework-only、framework + devices、host tests、rm-a/rm-c 目标构建；`init_workspace --devices` 生成的 shell 必须实际 configure/build 并产出 ELF。
4. 把 `tar_awdevices` 加入 LTO 的 GNU ARM safe-target 允许表；修复 LTO 包装层丢弃 target identity；扩展 `link_guard` 的器件 provider/registry 符号检查。
5. 清理旧 include 路径、空占位、过期文档；修复或移除 `pal_dev.h` 死伞头与 `pal.md` 的过期描述；确认 `tar_awdrivers` 不再包含器件源。
6. 回滚：门禁失败可单独回退门禁提交；器件仓库可 pin 上一版本，不影响框架发布。

### 8. CI 门禁

- **构建隔离**：`tar_awdevices` 的 include 路径只来自 `tar_awapi_driver` 与器件仓库自身；任何 `platform/`、`third_party/`、`bsp`、vendor SDK 的 include 直接编译失败。
- **静态扫描**：器件仓库源码禁止出现 `platform/`、`third_party/`、`drivers/src/`、`stm32`、`ti/`、`DeviceFamily` 等路径/标识；白名单只允许框架公开头和标准库。
- **双构建**：框架单独构建必须通过（证明框架不依赖器件）；框架 + 器件示例必须通过（证明接线可用）。
- **行为回归**：DJI/P1010B 示例在迁移前后保持相同可观察行为；rm-a/rm-c 构建矩阵覆盖 gnu-rm 与 armclang。
- **测试归属**：框架 host tests 不链接器件；器件仓库自带 host/编译测试（可用框架 host 桩）。目标板验证至少覆盖 DJI 与 P1010B 各一条 CAN 路径。
- **失败语义**：器件不支持的能力必须显式失败，不得伪成功；器件库的返回值遵循 `OmRet` 约定。
- **发布隔离**：框架 release 资产不包含器件库；器件仓库独立发布，版本轴独立。

## 影响 (Consequences)

**正面**

- 框架保持硬件无关；新增器件只改器件仓库，不再修改框架构建与发布。
- 器件库有独立版本与发布节奏；框架与器件通过公开契约解耦。
- 构建系统可以强制依赖方向，而不是靠文档约定。
- 平台适配与器件库互不引用，板级/应用成为唯一接线点，职责清晰。

**代价与风险**

- 双仓库/双构建增加接线成本；`init_workspace` 需要支持器件仓库，应用需要显式调用器件初始化。
- 现有 `samples/motor/*` 的 include 路径与默认 workspace entry 需要迁移；迁移期存在一次性的路径变更。
- 框架公共 API 变更会影响器件仓库；由后续 `compat.yaml` 与器件仓库 CI 的 pinned 框架版本管理。
- 器件库不允许创建线程/定时器，复杂器件若需要后台执行，必须由应用或框架 async 提供执行上下文；这是有意的约束，避免器件库变成系统。
- CAN PAL 的控制器族词汇暂不通用化；器件库只能使用设备 API。该限制记录在案，后续另立 ADR 处理。
- `MotorCore` 未在 v0.2.0 实现；厂商电机驱动先迁移，未来统一模型需要再次迁移适配层。该债务显式记账，不在本次拆分中假装完成。

**非目标**

- 不实现 `MotorCore`，不统一电机语义。
- 不重做 CAN PAL，不把 mailbox/filter-bank 概念移入 ops。
- 不把 partition、日志后端移出驱动框架。
- 不处理 comm 相关内容，不扩展 `services/comm` 开放清单。
- 不做器件自动发现、不引入 weak/自注册作为默认扩展机制。
- 不改变 platform/bsp 的 vendor 适配层与板数据模式（沿用 ADR-0012）。

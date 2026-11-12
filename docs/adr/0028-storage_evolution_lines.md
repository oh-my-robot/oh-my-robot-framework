# ADR-0028：存储演进三条线与机制建立节奏

- 状态：已决策（2026-09-30）
- 日期：2026-09-30
- 参考：ADR-0024（分区注册表 + 句柄）、ADR-0026（分区语义补完）、ADR-0027（Flash PAL 去异步域）；`docs/boot_ota/reference_design_notes.md` K-15（设备对象判据）；过程文档 `docs/internal/active/issue_000_storage_evolution/mechanism_roadmap.md`

## 背景 (Context)

1. **待接入四类器件**：外部 NOR（W25Q256JV：SPI，32MB，4KB 扇区 / 64KB 块 / 256B 页，**全命令集有 4 字节地址变体**）、EEPROM、SD 卡、SPI NAND（MX35LF1GE4AB：1Gb，(2048+64)B 页，(128K+4K)B 块，**片内 4-bit ECC per (512+16)B 默认开启**，坏块标记在块内第 1/2 页 spare 首字节）。
2. **三条并行主线**（用户拍板）：
   - **Flash 线**：Flash 驱动 → NVS / FCB / ZMS / FS / StreamFlash 等存储原语 → **config 配置框架**（本线的最终目标）
   - **NVMEM 线**：NVMEM → EEPROM（非 Flash 存储器路径）
   - **块设备线**：块设备 → SD 卡
3. **当前架构状态**：非易失存储接口 `FlashDev` + partition 层已就位（零状态、注册表 + 句柄）；异步执行域已移除（ADR-0027）；**共性内核不存在**（同类芯片的共享逻辑与状态写在单一适配器里）；`FlashOps` 三函数全必选；`OmRet` 为"0 = 成功，>0 = 失败"的二值约定。
4. **两处手册取证的硬约束**（决定了 NAND 不能先做）：
   - MX35LF："please read out the bad block information **before any erase operation** since the bad block marks **may be cleared by any erase operation**" ⇒ 坏块表必须在首次擦除前建立并驻留，**这是状态**；
   - W25Q256JV："Enter 4-Byte Address Mode (B7h) / Exit (E9h)" + 每命令有 4 字节变体 ⇒ **"当前处于哪个地址模式"是运行期状态**，不是几何常量。
5. **当前架构的判据**（前序讨论收敛）：成熟栈处理多介质的机制是「芯片差异 → 数据表；同族共性 → 族内核；族间差异 → 统一接口上的可选操作 + 核心层领域默认」。本仓只有第一项。

## 考虑过的方案 (Options)

- **A（否决）：先建统一的非易失存储抽象层，三条线共用。** 三条线的形态是正交的——擦除语义（有/无）、寻址单位（扇区/字节/512B 块）、分区形态（编译期常量表 / 命名窗口 / 介质上的 GPT-MBR）、擦后值（确定/无概念）——强行统一只能取最小公倍数，两边都不好用。本仓已立"不发明存储统一层"原则（`storage_landscape.md` §6.4）；且"三线共用"当前没有消费者。
- **B（否决）：按器件差异大小排序，NAND 优先。** NAND 的机制需求最多（共性内核 + 持有状态 + 可选操作 + 错误语义扩展），把它排第一等于**把全部机制建设压在一个最高风险的器件上**：任何一个机制设计错误都要在 NAND 上暴露并返工。而 NOR 能用最小代价把同一批机制建立起来（同类、形态最接近、无坏块、无 ECC）。
- **C（采纳）：三条线各自成接口；器件顺序按"机制建立成本递增"排列 —— NOR → EEPROM → SD → NAND。**

## 最终决策 (Decision)

### 1. 三条线各自成接口，不共用设备抽象

本层 = **一套接口 + 一份能力描述**。接口提供 read / write / erase —— **擦除是接口里的一个操作，不是对所有实现的行为要求**；能力描述器件的事实（有无擦除、擦后值、写与擦的粒度、寻址方式）。**消费者按能力分支，不按器件类型分支**。

| 线 | 接口 | 能力描述 |
|---|---|---|
| Flash 线 | `FlashDev`（read/write/erase + 几何查询） | `FlashGeometry` + `FlashGeometry.caps`（`FLASH_CAP_*`） |
| NVMEM 线 | 新的字节接口（read/write） | 各自的窗口描述 |
| 块设备线 | 新的扇区接口（read/write） | 各自的扇区描述 |

**partition 模块不扩展去覆盖另两条线的分区形态。** SD 的分区表在介质上（GPT/MBR，需解析），与可擦介质的编译期常量表是两种形态；Linux 里 `mtdpart` 与 `block/partitions/` 也没有共同接口。

**关于"族"这个概念**：本决策不再以"族"作命名单元与准入判据。此前把**写入模型**（"写前须擦"）当作族定义，但它其实是**介质能力的反映**，不是用途的反映——三者曾被混同在一层：

| | 是什么 | 谁决定 |
|---|---|---|
| **介质能力** | 芯片支持哪些操作、擦后值是什么 | 物理 |
| **接口契约** | 调用者必须遵守什么规则 | 设计者按用途选 |
| **用途** | 上层服务什么数据形态（字段 / 流 / 通用块） | 应用 |

后果是"免擦介质"与族定义必然冲突（一个"擦写设备"怎么会不需要擦？）。改为"接口 + 能力"后：**能力描述事实、接口不表态、消费者按能力分支**。

**免擦介质（MRAM / RRAM）的位置**：它们不提供擦除操作——**这只是能力组合不同**，既不需要"并入"，也不构成"例外"。接口里的 `erase` 对它们的实现是无操作，能力位 `FLASH_CAP_NO_ERASE_NEEDED` 声明这一事实，消费者据此跳过无意义的擦除。

**两家成熟框架的对照（一手取证）**：

- **Linux**：FRAM/EEPROM 归 `nvmem` —— `drivers/misc/eeprom/at25.c` 的自述是 "Driver for most of the SPI EEPROMs, such as Atmel AT25 models and **Cypress FRAMs FM25 models**"；而 MTD 版 `drivers/mtd/devices/at25.c` 在 v6.12 已不存在（一个"从 MTD 迁出"的实际案例）。
- **Zephyr**：免擦器件留在同一 flash 接口内，用能力位区分 —— `include/zephyr/drivers/flash.h` 的 `FLASH_ERASE_C_EXPLICIT` 与 `caps.no_explicit_erase`；且存储原语**已按能力分支**（`subsys/storage/stream/stream_flash.c:86` 原文："Stream flash does not rely on erase, it does it when device needs it"）。
- **两者不矛盾**：用途决定选哪条接口——服务"字段存储"（内核驱动读 MAC / 校准值）走 nvmem；服务"存储原语"（NVS / FCB / FS）走带能力的 flash 接口。**同一介质历史上两条路都走过**（AT25），这正说明走哪条由用途定，不由介质定。

### 2. 器件顺序：NOR → EEPROM → SD → NAND

理由见方案 C。**NAND 排最后不是"不重要"，而是它是三条机制线的共同受益人**：它需要的共性内核（NOR 阶段建）、可选操作（NOR 阶段建）、状态容器（EEPROM/SD 阶段建）、错误语义（见决策 4）在它之前都已就位。

### 3. 机制建立节奏

| 阶段 | 建立的机制 | 谁受益 |
|---|---|---|
| **现在** | 接口/能力模型成文 + 能力位收敛到 `FlashGeometry.caps`；两条约定（决策 4、5）记录在案 | 三条线全部 |
| **NOR** | **共性内核对象**（`hw` 从"随便挂"正名为"挂内核对象"：同类芯片共享的状态 + 逻辑）；地址宽度能力进 `caps`；`FlashOps` 可选化 + 领域默认语义 | NOR 第二颗芯片（零代码）；NAND 直接复用 |
| **EEPROM** | 新接口独立成型的范式（并列设备类型 + 各自的窗口/分区形态） | SD、NAND 照此办理 |
| **SD** | 介质上分区表的解析路径 | — |
| **NAND** | 有状态共性内核（坏块表、ECC 状态读、ECC 段拆页） | — |

**共性内核对象是其中的关键机制**：把"零状态"这个性质**限定在 partition 层**——器件层天然有状态（当前地址模式、坏块表、片内 ECC 开关），而 `void *hw` 只能承载私有态、不能承载可共享态。内核对象是那个共享载体（对照 Linux 的 `spi-nor core` / `nand_base`，它们按形态分立，不按介质）。

### 4. 约定一：**"成功但附带信息"不走 `OmRet`**

`OmRet` 是"0 = 成功，>0 = 失败"的二值约定，没有警告域的位置。NAND 的 ECC 纠正（Linux 用 `-EUCLEAN`，靠"负值 errno 但语义为警告"实现）在本仓**不能**照搬：引入警告域会要求**所有**调用点把 `ret != OM_OK` 改写成区间判断，漏一处即静默 bug，且此时上层原语（NVS/FCB 等）的代码习惯已经固化。

**表达方式选定为"查询接口或出参"**（具体形态到 NAND 阶段定），使上层保持 `ret != OM_OK` 的判断习惯不变。

### 5. 约定二：容量边界（32 位寻址）

`FlashGeometry.capacity` / `OmPartitionEntry.offset,size` / `OmPartitionEraseUnit` 均为 `uint32_t`（**4GB 寻址上限**），且均匀几何模式的 `sectorCount` 为 `uint16_t`（**65535 扇区上限**，超出须改用 region 表）。

当前判定：**对 config 存储 / 日志 / boot meta 的用途充分，不升级**；边界显式记录，超出时须整体升 64 位（涉及上列全部类型）。

## 影响 (Consequences)

- **正面**：
  - 每个阶段都有明确的机制产出，且后一个器件是前一个阶段成果的第一个复用者；
  - NOR 阶段建立的共性内核由 NAND 直接复用，NAND 从"一次架构改造"降级为"一次机制复用"；
  - 三条线的接口边界由同一套能力模型把守，避免"为迁就新器件而放宽契约"——这是本类系统最贵的错误。
- **代价与风险（显式记账）**：
  - **NAND 的架构问题被推迟而非解决**：OOB 通道、坏块表驻留、ECC 段拆页、错误语义形态全部留到第四阶段。推迟是刻意的，但**约定 4 必须现在就立**，否则上层代码习惯会堵死它；
  - **三条线在顶部（config 框架）会汇合**：若 config 需要跨线后端，**由 config 自己定义最小存储后端接口、由各线实现它**——不反过来在底层预建统一层（"谁消费谁定义"）；
  - **config 框架不得自己实现原子更新**：掉电一致性是 NVS/FCB 层的职责，这条保证 Flash 线的内部顺序（原语 → config）不可颠倒。
- **能力位收敛到器件层一处**：`FlashGeometry.caps`（`uint32_t` + `FLASH_CAP_*` 宏，与 `pal_gpio_dev.h` / `pal_pwm_dev.h` 的 caps 位图同款形态）是能力位的**唯一**定义处；`partition.h` 原有的 `OmPartitionCapFlags` 及其便利函数已删除，`flash_geom_has_erase()` 随之改为 `flash_geom_needs_erase()`（查能力位）。**同一轮里连整个 `OmPartitionGeom` 分区视图也一并删除**——它 5 个字段中 4 个是器件属性拷贝、第 5 个（`capacity`）权威表里本就有，而句柄已持有 `FlashDev *`，器件几何经 `flash_geometry(h->dev)` 一步可达（现查询面为 `om_partition_capacity()` + `om_partition_dev_geom()`；详见 ADR-0026 的修订注记）。删除的理由就是本次讨论的教训：**同一事实在两处可见，就会长出两套语义**，而多存的那一份还附带一份无法被强制的一致性义务。
- **能力位默认值落保守侧**：位一律取否定式（置位 = 声明该例外），漏填即回到"写前须擦"，不会让消费者误以为可以跳过擦除直接写。
- **尚未落地的一步（显式记账）**：`flash_register` 当前仍要求几何声明擦除单位，故 `FLASH_CAP_NO_ERASE_NEEDED` 对原生成员恒为 0、免擦介质暂不可注册。放开该校验（并让 `flash_erase` 对免擦器件走无操作路径）留到**免擦介质真正接入时**——现在没有这样的器件要接（MRAM 走 NVMEM 线）。
- **与既有决策的关系**：
  - **ADR-0026/0027** 的结论不变，本决策在其之上排定演进节奏；
  - **"缺失的中间层（管理层：磨损轮转 / 掉电事务化）"的判断部分撤回**：FCB / NVS / ZMS / littlefs 各自都带 GC，**GC 是本层的、不是共用的**（与 Zephyr 形态一致）。在这些原语各自落地之前，不存在一个公共管理层的需求，"先有真实消费者再抽象"原则继续适用。
- **本决策的适用边界**：它规定的是**机制建立顺序**与**接口/能力的划分**，不规定各存储原语（NVS/FCB/ZMS/FS/StreamFlash）之间的层次关系——那属 Flash 线的内部设计，另案。

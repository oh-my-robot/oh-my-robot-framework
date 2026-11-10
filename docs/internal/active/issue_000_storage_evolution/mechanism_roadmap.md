# 存储演进：机制就绪路线图（可执行）

- 日期：2026-09-30
- 权威决策：`docs/adr/0028-storage_evolution_lines.md`（三条演进线 / 器件顺序 / 机制节奏 / 两条约定）
- 性质：过程文档。ADR 记录"为什么"，本文记录"做什么、动哪些文件、怎么验"。

---

## 阶段 0：现在（零代码逻辑改动，仅注释与契约）

### 0.1 接口 / 能力模型成文（已完成）

**动机**：此前把**写入模型**（"写前须擦"）当作族定义，但它其实是**介质能力**的反映——于是"免擦介质"与族定义必然冲突（一个"擦写设备"怎么会不需要擦）。改为"**接口 + 能力描述 + 消费者按能力分支**"后，免擦只是能力组合不同，既不"并入"也不"例外"。

**动作（已完成）**：`pal_flash_dev.h` 文件头改写为接口/能力模型；能力位收敛到 `FlashGeometry.caps`（`uint32_t` + `FLASH_CAP_*`，与 gpio/pwm 的 caps 同款）；`partition.h` 的 `OmPartitionCapFlags` 删除、改为直接投影器件层位图；`flash_geom_has_erase()` 改为 `flash_geom_needs_erase()`（查能力位）。

**验证**：clang-format 21.1.8 门禁 + 四目标构建 + host（`flash_dev_test` 84、`partition_test` 186）——**均已通过**。

### 0.2 清除与模型矛盾的旧表述（已完成）

**动机**：`pal_flash_dev.h` 与 `partition.h` 的族边界段曾写着"免擦可擦器件（MRAM 类）经 erase 缺省语义并入"——与新的接口/能力模型冲突（把"擦除是可选的"与"擦除不存在"混同：前者是能力问题，后者是接口问题）。

**动作（已完成）**：两处旧表述删除；`flash_verify_erased()` 的免擦早退改查能力位；`partition_view_fill()` 改为投影器件层位图。

**验证**：格式门禁 + 四目标构建 + host 测试——**均已通过**。

### 0.3 约定一：错误语义（`OmRet` 不设警告域）

**动机**：NAND 的 ECC 纠正需要"成功但附带信息"的语义。Linux 用 `-EUCLEAN`（负值 errno 但语义为警告）实现，**本仓不能照搬**——`OmRet` 是"0 = 成功，>0 = 失败"的二值约定，引入警告域要求所有调用点改写判断，漏一处即静默 bug。

**动作**：记入 ADR-0028 决策 4，并在 `core/om_def.h` 的 `OmRet` 定义旁加一行注记：**"0 = 成功，>0 = 失败；'成功但附带信息'不走本类型，走查询接口或出参"**。

**验证**：纯注释，格式门禁。

### 0.4 约定二：容量边界

**动机**：`uint32_t` 寻址上限 4GB；均匀几何模式的 `sectorCount` 是 `uint16_t`（65535 扇区上限）。

**动作**：记入 ADR-0028 决策 5；在 `FlashGeometry` 的 `capacity` / `sectorCount` 字段旁注明边界与升级路径（超出须整体升 64 位，涉及 `capacity` / `OmPartitionEntry.offset,size` / `OmPartitionEraseUnit`）。

**验证**：纯注释，格式门禁。

---

## 阶段 1：NOR（W25Q256JV）—— 三个机制，全部有真实消费者

### 1.1 共性内核对象（关键机制）

**动机**：芯片差异里有一类**放不进表**——它是**状态**或**共享逻辑**。手册取证给出两个硬例：

- W25Q256JV 的"**当前**处于 3 字节还是 4 字节地址模式"是运行期状态，且有 B7h/E9h 切换序列；
- MX35LF1GE4AB 的坏块表**必须在首次擦除前建立**（"bad block marks may be cleared by any erase operation"），且必须驻留。

当前 `void *hw` 能承载**私有**态，但不能承载**可共享**态——所以第二个 SPI NOR 适配器仍要重写拆页 / WIP 等待 / 4 字节模式管理。

**形态**（改约定不改结构体）：`hw` 从"随便挂"正名为"挂内核对象"：

```
FlashDev.hw ──→ SpiNorDev {          /* 共性内核对象 */
                  cfg;                /* 芯片表项（差异 = 数据） */
                  addrMode;           /* 运行期状态：3/4 字节 */
                  busLock;            /* 总线锁（多片共用 SPI 总线） */
                  chipPriv;           /* 芯片私有态（原 hw 的职责） */
                }
```

**共享逻辑进内核**：拆页（256B 页）、WIP 等待、写使能、4 字节模式切换、SFDP/JEDEC ID 探测。
**留在适配器**：SPI 控制器操作、CS 控制、时钟配置。

**受影响文件**：新增 `lib/drivers/src/peripheral/flash/`（或 `platform/` 侧）的 SPI NOR 共性内核；新增 W25Q 适配器；`pal_flash_dev.h` 补充 `hw` 的约定说明。

**验证**：host 仿真（新增 spi-nor sim：页/扇区/块 + WIP 时序 + 4 字节模式状态机）+ 真机（现有 `flash_verify` 扩展 G4 段）。

### 1.2 地址宽度能力进几何

**动机**：W25Q256JV 是 256Mbit（32MB）> 16MB，**全命令集有 4 字节变体**（Read 13h / Fast Read 0Ch / Page Program 12h / Sector Erase 21h / 64KB Block Erase DCh）。这是**芯片常量**，属表项。

**动作**：`FlashGeometry` 增加"地址宽度"表达（字段或能力位）。**同时按 `OmPartitionCapFlags.reserved:31` 的做法留 reserved 位域**——这是"新能力吃预留位、不改结构体一次"的唯一手段。

**受影响文件**：`pal_flash_dev.h`；`hal_flash.c`（若有校验依赖地址宽度）。

**验证**：host 测试用例（4 字节器件的地址越界/对齐）+ 格式门禁 + 四目标构建。

### 1.3 `FlashOps` 可选化 + 领域默认语义

**动机**：三函数全必选 ⇒ 免擦器件（MRAM）必须写空 `erase`；NAND 的 OOB 通道无处加入。成熟栈的做法是**可选操作 + 核心层按能力的领域含义取默认值**（Linux `mtdcore` 的四种处理：`-EOPNOTSUPP` / 回退通用路径 / 恒假语义值如"无坏块"）。

**动作**：`FlashOps` 成员改为可空；`hal_flash.c` 定义领域默认：

- `erase == NULL` ⇒ **擦除恒成功**（免擦器件；擦后校验同时跳过）
- `readOob == NULL` ⇒ OOB 不可用（NAND 阶段加此成员）
- `write == NULL` ⇒ 只读器件（可选）

**受影响文件**：`pal_flash_dev.h`、`hal_flash.c`、现存适配器（保持填满即可，无需改动）。

**验证**：host 新增"免擦器件"用例（不填 `erase`，验证擦除恒成功且擦后校验跳过）+ 现有 84 项回归 + 四目标构建。

---

## 阶段 2：EEPROM（NVMEM 线）

**建立的机制**：新接口独立成型的范式——并列设备类型（`NvmemDev`：字节粒度 `read` / `write` + 窗口描述）+ 各自的窗口/分区形态。

**不做的**：不并入 `FlashDev`（它的能力组合里没有擦除，接口与能力描述都对不上：字节粒度、无擦后值、无区域生命周期）；不要求 partition 模块支持它。

**参照物**：Linux `drivers/nvmem/`（字节粒度非易失存储的统一抽象，后端可为 EEPROM / OTP / efuse，无擦除概念，布局由消费者描述）。

**验证**：host 仿真 + 真机（EEPROM 读写与复位保持）。

---

## 阶段 3：SD 卡（块设备线）

**建立的机制**：**介质上分区表**的解析路径（GPT/MBR）。

**关键区别**：Flash 线的分区是**编译期常量表**（partition 模块的形态）；SD 的分区表**存在介质上**，需运行期解析。二者是两种形态，**partition 模块不扩展去覆盖它**（Linux 的 `mtdpart` 与 `block/partitions/` 同样没有共同接口）。

**验证**：host 仿真（分区表解析）+ 真机（读卡、分区识别）。

---

## 阶段 4：NAND（MX35LF1GE4AB）

**此时已就位**：共性内核对象（1.1）、地址宽度能力位与预留位（1.2）、可选操作与领域默认（1.3）、状态容器范式（阶段 2/3）。

**本阶段新建**：

1. **NAND 共性内核**（`spinand` 位置——Linux 里 SPI NAND 与 raw NAND 是**两个独立子系统**，各自填统一接口）；
2. **OOB 通道**（可选操作，形态参考 `struct mtd_oob_ops`：数据缓冲与 OOB 缓冲平行，一次操作可只读数据 / 只读 OOB / 两者都读）；
3. **坏块表**：出厂标记在块内第 1、2 页 spare 首字节（= 00h）；**必须在首次擦除前建立**并驻留 RAM（1Gb = 1024 块 ⇒ 位图 128 字节）；"识别"属驱动层，"管理"（换块/降级）属未来管理层；
4. **片内 ECC 的状态读**：MX35LF 的内部 4-bit ECC 默认开启（Set Feature 1Fh + B0h Bit4 控制），主控不做 ECC，但**必须能读 ECC 状态**；ECC 段编程约束（512B 主区 + 12B Metadata1 须一次编程完）；
5. **几何补字段**：`oobSize` / `oobAvail`（NOR = 0）/ `eccStrength` / `eccStepSize`（NOR = 0）。

**错误语义的具体形态在本阶段定**（受约定一约束：走查询接口或出参，不动 `OmRet`）。

**验证**：MX35LF 真机（坏块表建立与保持、ECC 状态读、擦后校验带 ECC 状态判定、掉电后坏块表重建）。

---

## 不做清单（防止提前建抽象）

| 不做 | 理由 |
|---|---|
| 统一非易失存储层（三条线共用设备抽象） | 三条线形态正交；无消费者；本仓已立"不发明存储统一层" |
| 公共管理层（磨损轮转 / 掉电事务化） | FCB / NVS / ZMS / littlefs **各自带 GC**——GC 是本层的不是共用的；原语落地前不存在该需求（ADR-0028 已撤回旧判断） |
| partition 层为介质分叉 | partition 对介质透明是它的价值（`mtdpart` 同款） |
| 错误码警告域 | 会污染所有调用点的判断习惯（约定一） |
| 为 EEPROM / SD 抽象通用分区接口 | 关闭的是三种正交形态；Linux 也没有 |
| config 框架自己实现原子更新 | 掉电一致性是 NVS/FCB 的职责（保证 Flash 线顺序不可颠倒） |
| config 框架预建多后端抽象 | 先绑定 NVS，第二后端（EEPROM）真需要时再抽——"谁消费谁定义" |

---

## 验证基线（每阶段收尾必过）

- **格式门禁**：`/d/ProgramFiles/LLVM/bin/clang-format.exe`（21.1.8，**不能用 PATH 里的 17.0.6**），命令见 `omr-local-build-gotchas`；
- **四目标构建**：`robot_project` / `log_module_filter` / `partition_verify` / `flash_verify`（构建壳 `xmake.lua`，真机 sample 无独立 xmake.lua）；
- **host 测试**：`flash_dev_test`(84) / `partition_test`(186) / `osal_none_test`(118) / `om_core_test` / `om_log_test`；
- **真机**：`partition_verify`(53) / `flash_verify`(27)。真机流程与"结果会被吞掉"的三个陷阱见 `omr-hardware-verify-workflow`。

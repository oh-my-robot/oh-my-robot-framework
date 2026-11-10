# ADR-0026：分区语义补完——介质能力暴露与错误语义分层

- 状态：已决策（2026-09-19）
- 日期：2026-09-19
- 参考：设计事实源 `docs/internal/active/issue_000_partition_completion/partition_completion_design.md`（§0 对接面 / §4 接口 / §5 决策状态）；调研稿 `docs/internal/active/issue_000_log_persist_research/log_persist_research.md`（Part A–J + P1/P2/B1/B2/B3 各线原始报告）；ADR-0024 (partition_registry_handle)；差距分析 §2.2（五层切分）/ §6.6（不倒挂原则）；boot 侧 ADR-0025（**在另一克隆** `base_on_omr/oh-my-robot-framework`，非本仓）
- 编号说明：**0025 已被 boot 侧占用**（`0025-boot_decision_data_and_jump_contract.md`，另一克隆）。ADR 编号是跨工作线的共享命名空间，故本决策取 0026。

## 背景 (Context)

1. **能力已在，接缝未成**（本决策的核心判断）：`FlashGeometry`（`pal_flash_dev.h:76-85`）**已含**擦除单位、非均一区域表 `sectorRegions`、`writeUnit`、`erasedValue`——**比 ESP-IDF 新版 BDL 更全**；分区层**已有**地址窗口与两级边界检查。缺的是**暴露**：`open` 期明明校验过"扇区友好"，却**没有 API 枚举分区内的擦除单位**。⇒ 本决策不是"补一个新能力"，而是"把已有能力合成一个稳定的、面向消费者的面"。
2. **首发消费者已存在且已锁定决策**：bootloader 的决策数据（boot meta）正在另一工作线设计中，其 ADR-0025 已定：entry 32B 定长、**≥3 份**、擦除归引导侧、erase 态（全 0xFF）**须显式判无效**、提交标记最后写。它对本底座的直接需求 = **擦除单位枚举** + **已擦判定**。
3. **同款缺口在业界有源码级代价证据**：Zephyr 的 littlefs 适配层注释逐字——"There's no flash_area API to implement this, so we have to make one here."（即成熟框架里消费者被迫自己造）。
4. **调研查出的三条反直觉事实**（均有出处，见调研稿）：
   1. **没有任何框架提供"运行期注册 API + 持久化 + 单一事实源"三合一**——Zephyr 用隔离、ESP-IDF 用仲裁+冻结、U-Boot 用"让表本身可变"各解一半；
   2. **没有任何框架让"分区能力声明"真的被强制**——实证 5 例"声明了没人读"（最典型：Zephyr `read-only` 在 binding 有、`flash_area_write/erase` 根本不检查，issue 挂数年，PR #36979 实现过又撤出）。⇒ 判据：**grep 有没有一处读它并返回错误码**；
   3. **`allow_erase` 是「主体相对」的**（Zephyr #43052 原文论证：同一条 app 分区，**对 bootloader 可写、对 app 只读**）⇒ **一张 boot/app 共用的编译期常量表在结构上装不下布尔权限字段**。
5. **非均一几何否掉了两个"标量直觉"**：器件的擦除单位可以非均一（同一器件内多段不同扇区尺寸），"一个擦除单位的大小"**根本不存在**；且**"对齐"不是模运算**——按较小尺寸对齐的地址若落在更大扇区内部**不是可擦边界**，误信会**擦掉整个大扇区**。

## 考虑过的方案 (Options)

- **A（采纳）：分区语义层「补完」**——在既有 `partition` 模块内补"几何视图 + 窗口内擦除单位序列 + `is_erased` + 错误语义约定"，**模块零状态不变，数据面不新增任何 API**。
- **B（否决）：新建"介质访问抽象"层（模块名 `OmMedia`）**——与分区语义层**职责重叠**："介质可替换"是 PAL 后端接口与分区窗口**共同**提供的**性质**，不是一个新实体；多一个名字指同一件事。**本设计初稿曾如此表述，已更正**（横向切分维持五层不变）。
- **C（否决）：并入器件层**——违反 §6.6 不倒挂原则。MCUboot 给分区的定义是**擦写隔离不变式**（"An area can be fully erased without affecting any other areas"）——**这是分区语义，不是器件物理语义**。
- **D（否决）：消费者自取几何**（各消费者自己 `flash_find` + `flash_geometry`）——正是背景 3 的 Zephyr 现状，代价已被实证。

## 最终决策 (Decision)

1. **定位**：分区语义层的**补完**，**不是新层**。全部命名用 `om_partition_*`；**`OmMedia` 这个名字取消**。
2. **接口面 = 只补"能力与几何"的查询面，数据面零新增**（`read`/`write`/`erase`/`erase_range` 已存在）：
   - `OmPartitionGeom`（分区视图：`capacity`/`writeUnit`/`pageSize`/`erasedValue`/`eraseBeforeWrite`）——**全部由 `FlashGeometry` 投影**，不含器件级字段与整器件区域表；
   - 窗口内擦除单位：`om_partition_erase_unit_count` / `_at` / `_covering` + `is_uniform()` / `max_erase_unit()`；
   - `om_partition_is_erased(off, len, *out)`——与 `erasedValue` 逐字节比较。
   - 擦除单位的计算**内部写成纯函数**（分区窗口 ∩ 器件区域表），留将来不经分区表直接复用的口。
3. **两个标量被显式挡掉**（反面形态已在业界实证为陷阱）：
   - **无 `eraseUnit` 标量**（非均一下不存在）；**擦除单位由数组元素携带**；
   - **无 `eraseAlign` 标量**——"对齐"在非均一下**不是模运算**；擦除合法性判**"两端是否扇区边界"**（器件层 `flash_erase_validate` 已是此形态，注释逐字"跨大小不同的区域也成立"）。**一个标量字段会被误当成可擦性判据，比不加更坏。**
4. **能力声明按"谁最先知道"分层**（本决策的通用规则）：

   | 类别 | 例子 | 谁填 | 放哪 | 主体相对 |
   |---|---|---|---|---|
   | **介质物理事实** | `erasedValue` / `writeUnit` / 写前须擦 | **驱动** | `FlashGeometry` ⇒ 投影进 `OmPartitionGeom` | 否 |
   | **几何形状** | 擦除单位序列 / `is_uniform` | 驱动（`sectorRegions`） | 同上，**必须数组而非标量** | 否 |
   | **策略 / 权限** | `allow_erase` | 产品与 boot/app 的约定 | `OmPartitionEntry`（共享表） | **是** |

   ⇒ **ABI 耦合（两侧同时改）仅对策略字段存在**；物理事实走运行期投影视图，与 boot/app 的编译期耦合无关。

   **能力位形态（2026-09-20 更正）**：**不用 `bool`**——`bool` 的零值 `false` 在"写前须擦"这一语义下恰是**最危险**的一侧，违反本决策"漏填落保守侧"的原则。改为**位域 + `reserved`**（学 ESP-IDF BDL `reserved:27`：新增能力位不改结构体大小、不破 ABI），**位取否定式**（学 Linux MTD `MTD_NO_ERASE`：置位 = 声明例外，未置位 = 保守默认）：`OmPartitionCapFlags{ noEraseNeeded:1; reserved:31; }`。
5. **`allow_erase` = 演进项**（V1 靠纪律 + `is_erased` 自检），**但形态已定死**：因它是**主体相对**的，**不能做成表内布尔字段**；须为**策略枚举**（`ERASE_ANY`/`ERASE_BOOT_ONLY`/`ERASE_NEVER`）+ **每工程自身身份常量**，在框架的 `part_erase()` 内**取交集**才放行。
6. **错误语义分层**：
   - **越界 → `OM_ERR_RANGE`**（调用方请求越出可访问范围，**改偏移**）；**未对齐 / 参数为空 / 表配置错 → `OM_ERR_INVALID_ARG`**（**改参数或改表**）。**越界优先于未对齐报出**。**两层同时改码**，否则同一条件两层不同码。
   - 「**该区不可用、换下一区继续**」→ **器件层发**，落码 `OM_ERR_FLASH_UNUSABLE`。**与 `OM_ERR_FLASH_IO` 必须分开**（IO = 这一次失败 ⇒ 重试同一区；UNUSABLE = 这块区已验证不可用 ⇒ 跳过该区），否则消费者无从选择策略。**码段 = 模块特有码 `0x1000+`**（非 `0x001-0x0FF` 别名段——后者只映射既有通用码，而本码恰恰没有通用对应）。**检测机制 = 器件层擦后校验**（擦除成功后读回比对 `erasedValue`）：不能指望"擦除返回失败"本身作为判据，那同样可能只是瞬态。
   - 「格式不兼容」与「记录校验失败」**必须不同码**，且都是 fail-closed。
7. **分区表形态 T1**：**编译期常量表**，**一个头文件被 boot 与 app 两工程 include**（编写单一源）。**T2（持久化表住 meta 区）= 演进项**。⇒ **T1 下本设计无跨线阻塞，不需要找 boot 侧对齐**；布局指纹**已由 boot 侧认领**（同为演进项），本设计不重复规划。
8. **运行期可自建分区 = 形态 A**：`OmPartitionRegistry{table, count}` 的表指针**已可指向调用方 RAM**，故"运行期构造表再 open"**今天就能做**，**不新增注册 API**。五条边界：表内自检防溢出（`len <= total - off`，**不用** `off + len <= total`）、不跨表仲裁、表生命周期归调用方（框架**不缓存表指针**）、空表返错而非 panic、超容量返错绝不静默截断。
9. **句柄缓存 `FlashDev*` + `const FlashGeometry*`**（open 期解析一次），**"信任开发者"**，前提是**设备永驻**（PAL v0 无注销接口）。
10. **EEPROM / SD 不接入分区层**：分区层守的擦写隔离不变式在那些介质上不成立或另有归属（Linux 对 EEPROM 用 nvmem cells、对块设备用 GPT；littlefs 干脆把区域概念下推成块设备回调，"partition" 一词在其文档中出现 0 次）。跨族统一在**消费面窄接口**，不在介质层。

## 影响 (Consequences)

- **正面**：
  - boot meta 有了可依赖的面；**这套接口将有一个真实消费者真的靠它活着**——正是背景 4.2 那条判据（"grep 有没有一处读它并返回错误码"）在项目内的自查；
  - 错误语义可分层降级（BUSY 重试 / 换下一区 / 整体拒绝 / 只丢该记录）；
  - **两个标量陷阱被显式挡住**，并在文档里写明"为什么不要"，防止将来被加回来。
- **收回一处主张（显式记账，不委婉）**：§2.5 原称"**我们比业界更严**——Zephyr 与 ESP-IDF 都缓存设备指针，我们刻意每次 I/O 按名重解析"。**决策 9 一落地，该主张不再成立**，本设计在这一项上与业界**同形**。更正后：这是一次**显式交换**——用**设备指针的不可伪造/不可悬垂性**换掉**每 I/O 的名字解析开销**；仍保留的是 **"offset/size 恒从表取"**（每次 I/O 依旧句柄域校验 + 从表取偏移），**让出的是 `dev` 指针成为可伪造字段**（悬垂风险由"设备永驻"消解）。⚠️ **若将来 PAL 引入设备注销/重注册，前提失效，本交换必须重估。**
- **代价**：`is_erased` / `erase_unit_*` / `geom` 从"V1 应做"升为**首发消费者的承重项**——延期直接卡住 boot meta；策略字段的 ABI 耦合使 boot 与 app **必须同批编译**（T1 已在结构上接受此约束）。
- **不进接口（记账）**：`eraseTimeMax`——时序属性且随温度/磨损变化，**没有保证**，接口给出字段会暗示"这是保证"；文档可写，接口不承诺。`encrypted` 同理（无实际密钥路径时它就是个装饰）。
- **落地状态（2026-09-19）**：
  - **修订（2026-10-01）：`OmPartitionGeom` 已删除**。它的 5 个字段里，4 个是器件属性（`writeUnit` / `pageSize` / `erasedValue` / `caps`）的拷贝、第 5 个（`capacity`）在权威表里本就有——而句柄本就持有 `FlashDev *`，器件几何经 `flash_geometry(h->dev)` 一步可达。该视图属于"为想象中的消费者准备的便利"，且与本模块移除外层几何指针时立下的原则直接冲突（"多存一份只会引入一个无法被强制的一致性义务"）。现形态：句柄只存 `{注册表, 索引, FlashDev *}`；查询面改为 `om_partition_capacity()`（分区自己的属性，从表取）+ `om_partition_dev_geom()`（器件几何，经域校验返回）。**连带收益**：原"view.capacity 与权威表比对"的篡改哨兵随之消失——那正是"多存一份"的产物。分区层真正的职责（命名 / 隔离不变式 / 边界强制 / 擦除单位枚举）不受影响；对照 Zephyr 的 `struct flash_area`，它同样只存 `{fa_id, fa_off, fa_size, fa_dev}`，不做任何器件属性投影。
  - **决策 2（查询面）已落码**：`OmPartitionGeom` / `OmPartitionEraseUnit` + 7 个 API 全部实现；擦除单位枚举为**按需走步**（`partition_erase_unit_probe` + `flash_erase_unit_size_at`），**不物化数组**——故原"静态上限 32"决定**作废**（上限概念随之消失，不存在 ENOMEM 路径）；
  - **决策 3（无标量）已落码**：无 `eraseUnit` / `eraseAlign` 标量；判"两端是否扇区边界"；
  - **决策 6（错误语义）已落码**：两层同时改码 + 新增 `OM_ERR_FLASH_RANGE` 别名；
  - **决策 9（句柄缓存）已落码**：句柄内嵌 `dev` / `geom`，热路径零解析；`partition_handle_valid` 增两指针非空门禁。**公开头不引入 PAL 头**（前置声明 `struct FlashDev;` / `struct FlashGeometry;` 承载，为此给后者补 struct tag）；
  - **错误语义补完**：`OM_ERR_FLASH_UNUSABLE`（模块特有码段 `0x1000+`）= "该区结构性不可用 ⇒ 跳过该区"，与 `OM_ERR_FLASH_IO`（"这一次失败 ⇒ 重试"）**刻意分开**；配套**器件层擦后校验**（擦除成功后读回比对 `erasedValue`），判据 `flash_geom_has_erase()` 与分区视图的 `eraseBeforeWrite` **共用单一定义点**（2026-10-01 注：前者已更名 `flash_geom_needs_erase()` 并改查能力位，后者随 `OmPartitionGeom` 删除而不存在——见上方修订段；本条决策语义不变）；
  - **2026-09-20 三处形态更正**：① 能力位 `bool` → **位域 + `reserved`（否定式位）**；② 撤掉 `struct FlashDev;`/`struct FlashGeometry;` 前置声明，**直接 `#include` PAL 头**（前置声明把类型身份写在两处，且为它给 `FlashGeometry` 补 tag 属本末倒置；代价 = include 图变重，记为已知取舍）；③ 擦后校验**重试默认 1 + 可配置** `OM_FLASH_ERASE_VERIFY_ATTEMPTS`——**该码语义随之变为"在本配置的尝试次数内仍未达擦后值"**；
  - **验证**：`partition_test` **179 passed / 0 failed**（T9 覆盖非均一单位序列、跨尺寸点查询、均一/非均一两态、非法句柄保守返回、句柄篡改拒绝、已擦自检、**静默擦失败 → UNUSABLE**、**`attempts=1 ⇒ 恰 1 次擦 + 1 次校验读`**、**缓存指针门禁**、能力位零值语义）；ARM 固件三目标 `armclang` 构建 0 error；flash 编译门禁通过；`clang-format` 21.1.8 门禁通过；
  - ⚠️ `flash_dev_test` 的运行门禁因既有崩溃另立 **Issue #77**，故器件层 `OM_ERR_RANGE` / 擦后校验的**运行断言**目前仅编译验证（`partition_test` 经分区层间接覆盖擦后校验）。
- **未做（演进项，明确后置）**：T2 持久化表（含"表无效时 boot 怎么办"的兜底）；**布局指纹**（boot 侧认领）；`allow_erase` 策略枚举；ER-3 随机器件族 / ER-5 块设备族；日志持久化后端（**等真实外置 SPI NOR 到位**，不在片内 flash 做）；「运行期现场划分且重启还在」（= T2）。

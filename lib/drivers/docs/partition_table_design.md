# 分区表抽象设计（可擦存储器件族的上层语义）

> 版本：**v2 接口定稿**（2026-09-14；v1 = 2026-09-07 接口定稿，已被本版就地取代——v1 无任何生产消费者，不另立双事实源）
> 状态：v2 已落码并通过**双端验证**——host（`partition_test` 119/0）+ 实机（rm-a/F427，`samples/pal/partition` **53/0**；128K 扇区擦除实测 1072 ms，擦除期间心跳线程零断流）
> 关联：`docs/boot_ota/reference_design_notes.md`（P-03/K-02/ER-4）、`docs/boot_ota/multi_strategy_boot_design.md`（Q-02 布局拍板）、`docs/boot_ota/storage_landscape.md`（可擦族管理模块位）、ADR-0021 (boot_multi_strategy_skeleton)、ADR-0017 (project_config_layering)、`docs/internal/active/issue_000_storage_gap_analysis/storage_gap_analysis.md`（G-06/P1）

---

## 1. 分区表抽象是什么

一张**静态描述表**：把一块（或多块）物理存储"如何切成逻辑区域"表达为条目集合。条目最小形态：

```
名字 + 设备引用 + 区域内偏移 + 大小
```

上层代码**只按名字访问区域，永不书写裸地址**。查询面很小：按名查（返回设备/偏移/大小）、枚举、按分区读写的便捷层（内部走器件访问层并做分区边界校验）。

示例布局（表项示意，非具体板数据——板布局是表的实例，不是本抽象的一部分）：

```
┌────────────┬────────┬─────────┬────────┐
│ name       │ dev    │ offset  │ size   │
├────────────┼────────┼─────────┼────────┤
│ bootloader │ flash0 │ 0x00000 │   64K  │
│ meta       │ flash0 │ 0x10000 │   16K  │
│ app_a      │ flash0 │ 0x20000 │  ~960K │
│ app_b      │ flash0 │ 0x100000│    1MB │
└────────────┴────────┴─────────┴────────┘
```

本抽象回答的问题：**"物理资源怎么划分"**——从"谁在使用这些资源"中抽出，单独成表、单独成模块。

## 2. 为什么需要它

### 2.1 单一事实源

布局数值被多个独立消费者需要：引导程序（双槽/meta 位于何处）、升级下载侧（staging 写入何处）、存储类上层（配置/日志持久化）。各自硬编码的后果：同一数值散落多处，消费者之间错位（例如下载侧与引导侧对同一槽位的偏移认知不一致）即可互相踩踏乃至破坏运行区；改布局须同步改动全部消费者。分区表使布局**只定一次**，引导程序与应用共享同一份表（或共享表常量），天然一致——布局因此也是引导程序↔应用**契约的一部分**（镜像格式契约的布局维度）。

### 2.2 名字即抽象边界

上层语义只表达"逻辑区域名"（如 staging 槽），区域名背后的设备、偏移、大小、对齐约束全部被遮住。换布局、换介质、换器件，消费方代码零改动。**存储位置解耦的注册点正是这张表**：条目可指向片内器件、外部串行器件乃至块设备（受管封装族），抽象本身不感知。

### 2.3 换板/换产品 = 换表实例

布局同时受芯片几何约束（扇区图/容量）与业务决策（槽数/划分）影响——表属于**工程配置层**（bootcfg 体系，按板片段 + 用户自定义；几何约束由器件层提供并在操作期校验），而非平台板数据。不同板/不同产品的差异收敛为表的另一份实例，框架代码、引导核心、升级逻辑均不动——与"换芯片换适配器"的器件层原则同构。

### 2.4 业界同构

成熟系统均有同一动作：把"物理划分"从"使用者"中抽出为静态表（分区表 / 闪存映射 / 布局描述），或以名字为键的区域 API 族。本抽象是其通用形态。

## 3. 分层论证：为什么归 drivers 层

P-03 的措辞为"驱动上层语义"。完整论证拆三条：

### 3.1 它是可擦存储器件族的语义，贴着器件走

只有消费**可擦器件**（有擦除几何、有擦除单位、有已擦契约）时，分区才有意义——分区是擦除单位对齐、槽位生命周期这些器件语义之上的组织形态。本抽象唯一的下层依赖是器件访问面（设备模型 + 几何语义）。存储目标形态中它位于器件族与其管理模块（含磨损环形等）的同一语义域：**器件族语义需要聚合在器件族旁边**，而不是散入服务层。

### 3.2 核心消费方包含裁剪形态的引导程序

消费方清单：引导程序（无 OS 单执行流、面积预算紧张、无服务栈）、升级下载侧（应用内）、存储类上层（日志/配置持久化）。最挑剔的消费方是引导程序——它只能拉动依赖面最小的模块。若本抽象置于服务层并隐式依赖服务设施，裁剪形态将无法消费，引导程序只得退回布局硬编码（回到 2.1 的痛点）。落位于器件访问层语义位、只依赖设备模型 → **引导程序、应用、任何运行时形态均可消费**。

### 3.3 依赖方向自洽

器件访问层对服务层仅开放既定服务接口（单向例外，逐个开放）。若本抽象置于服务层而它必须访问器件设备，将制造反向依赖；落位于器件层语义位则只依赖本层内部，可被服务层、业务层、独立工程形态自由消费——方向干净。

### 3.4 措辞张力（如实记录）

"分区表不是驱动"——它是**表数据 + 查询逻辑**，并非设备驱动。"驱动上层语义"这一措辞依赖一个隐含事实：drivers 层实际承载的是"器件访问语义族"（设备访问面 + 其上的语义模块），本抽象属于后者、与各外设设备族平行。如需在分层文档中把该边界写明确，属后续修订；**落位结论不变：drivers 层**。

## 4. 边界（本抽象不做什么）

- **不做磨损/擦写均衡**：属存储/日志层的环形职责（P-05），同族不同模块。
- **不做掉电事务化**：掉电语义由消费方（meta 轮转、staging 流程）承担。
- **不做卷/文件语义**：分区是"命名区域"，不是文件系统卷；块设备/文件系统属受管封装族与上层消费形态。
- **不感知策略**：双槽/单槽/外部镜像等布局策略差异 = 表的实例差异，不是本抽象的分支。
- **不混入器件访问层**：分区概念不进设备几何（P-03 校验点：设备层头文件不出现"分区/boot/app"概念）。
- **不管表的来源**（v2 新增）：表驻留 ROM（编译期常量）还是调用方 RAM（介质解析而来）不由本模块关心——本模块只读该表。介质表的**格式与解析器**属独立议题，不在本抽象内。

## 5. 族边界与三族分区形态（2026-09-07 补；演进策略未来待定）

本分区表服务于**可擦存储器件族**（boot/OTA/存储上层消费的正是可擦器件）。"命名子区域"思想在三族都有对应形态，但**语义轴随族而变，按族分立成形，不做统一分区层**（storage_landscape §6.4 规律：上层声明窄需求、族内各自适配）：

| 族 | 器件 | 分区形态 | 与 v2 的关系 |
|---|---|---|---|
| 可擦 | NOR/NAND/MRAM | 分区表（扇区对齐 + 擦除单位，erase 为区域生命周期操作） | **本模块 v2** |
| 随机器件 | EEPROM/FRAM/OTP | **命名窗口（无擦除无对齐）**——Linux nvmem cells 为同构先例（共享 EEPROM 上给 MAC/校准/序列号划命名区域，动机与分区表同源：单一事实源防消费者踩踏） | ER-3 落地时独立成形（轻 API read/write），**不并入分区表**——无扇区/擦除语义的分区表是空壳 |
| 块设备 | SD/eMMC/U盘 | GPT/MBR（覆盖写/扇区语义，表在介质内） | 由 FS/disk 层语义承担（ER-5），不混入裸片存储抽象 |

边界要点：
- **免擦器件可入可擦族**（ER-1）：MRAM/自动擦 NOR 以 erase 缺省 → `NOT_SUPPORTED` 并入——分区层对它们自然成立（read/write 有效），无需改分区抽象。
- **数据模型族中性**：`OmPartitionEntry`（name/devName/offset/size）无擦除/对齐字段；族差异全在操作分发（经器件访问层 flash 面路由）。
- 演进策略（随机族何时落地 nvmem-cell 式、块族接入形态）未来待定——当前无器件需求不提前实现。

## 6. 关联

- 分层原则与调研：`docs/boot_ota/reference_design_notes.md` P-03（驱动上层语义、不混入设备层）、K-02（静态描述表：名字/设备/偏移/大小）、K-20（槽位映射生态无 remap 框架 API）、ER-4（管理模块化排期承接）
- 布局拍板：`docs/boot_ota/multi_strategy_boot_design.md` Q-02（双槽 + 引导区 + meta 独立区）、ADR-0021 (boot_multi_strategy_skeleton)
- 器件访问底层：FlashDev 设备抽象（几何/擦除语义）——`docs/boot_ota/flash_dev_design.md`
- 存储形态全景：`docs/boot_ota/storage_landscape.md`（可擦族管理模块位）
- 差距分析与本版动因：`docs/internal/active/issue_000_storage_gap_analysis/storage_gap_analysis.md`（G-06 无句柄化 API + 分区模块单例；P1）
- 本版任务与实现计划：upstream Issue **#75**（`oh-my-robot/oh-my-robot-framework`）；`docs/plans/2026-09-14-partition-api-v2.md`（10 任务，TDD）
- 决策提纯：ADR-0024 (partition_registry_handle)——注册表 + 句柄形态与信任模型（模块零状态；防伪造契约向业界收敛的记账）

## 7. 接口定稿（v2，2026-09-14 拍板后落码）

### 7.1 形态：注册表 + 句柄，模块零状态

v1 把表经 `om_partition_register` 交入**模块私有全局**（`static const OmPartitionEntry *s_table`）。该形态有两个消费面上的硬伤：

1. **热路径每次 I/O 两次名字查找**——`om_partition_read(name,…)` 内部 `om_partition_lookup`（strcmp 线性）+ `flash_find`→`device_find`（链表 + 名字比较）；
2. **模块单例**——只能有一张表，阻断 bootloader 侧的真实需求。

v2 把表的所有权交还调用方：**注册表是调用方持有的普通对象，句柄由 `open` 产出**。模块随之退化为**一组无状态纯函数**——无 init 顺序依赖、无并发保护需求、无隐藏全局。这一条同时满足引导程序（免运行期注册、可 const）、多表并存、以及"框架无隐藏全局可变状态"三项诉求。

### 7.2 数据结构

```c
/* 条目 —— 与 v1 一致 */
typedef struct OmPartitionEntry {
    const char *name;    /* 逻辑名（表内唯一）——字符串本体在表内，只读 */
    const char *devName; /* 器件名（flash0…） */
    uint32_t    offset;  /* 器件内偏移 */
    uint32_t    size;    /* 分区大小 */
} OmPartitionEntry;

/* 注册表 —— v2 新增：表 + 条目数，可整体 const（ROM 常量） */
typedef struct OmPartitionRegistry {
    const OmPartitionEntry *table; /* 指向条目数组；ROM 或调用方 RAM 皆可，模块只读 */
    uint32_t                count;
} OmPartitionRegistry;

/* 编译期常量注册表：免运行期注册（实参须为数组，勿传指针） */
#define OM_PARTITION_REGISTRY(table_)                        \
    { (table_), (uint32_t)(sizeof(table_) / sizeof((table_)[0])) }

/* 句柄 —— open 产出，数据通路的唯一入口 */
typedef struct OmPartitionHandle {
    const OmPartitionRegistry *reg;
    uint32_t                   index; /* 操作期校验 index < reg->count */
} OmPartitionHandle;
```

`OM_PARTITION_REGISTRY` 的实参**必须是数组**：传指针时 `sizeof` 比值退化为 0 → 空表语义，首次 `open` 即 `NOT_FOUND`（响亮失败，不会越界）。头注释须写明。

### 7.3 API

```c
/* ---- 注册表 ---- */
OmRet    om_partition_registry_validate(const OmPartitionRegistry *reg); /* 全表 fail-fast（可选） */
uint32_t om_partition_registry_count   (const OmPartitionRegistry *reg);
OmRet    om_partition_registry_at      (const OmPartitionRegistry *reg, uint32_t index,
                                        OmPartitionEntry *out);          /* 枚举（by-value） */

/* ---- 解析 ---- */
OmRet om_partition_query(const OmPartitionRegistry *reg, const char *name,
                         OmPartitionEntry *out);  /* by-value 纯信息，不碰器件（沿用 v1 语义） */
OmRet om_partition_open (const OmPartitionRegistry *reg, const char *name,
                         OmPartitionHandle *h);   /* 名字解析 + 器件解析 + 几何校验 */

/* ---- 数据通路（句柄入口） ---- */
OmRet om_partition_read (const OmPartitionHandle *h, uint32_t off, void *buf,        size_t len);
OmRet om_partition_write(const OmPartitionHandle *h, uint32_t off, const void *data, size_t len);
OmRet om_partition_erase(const OmPartitionHandle *h);                          /* 整分区 */
OmRet om_partition_erase_range(const OmPartitionHandle *h, uint32_t off, size_t len);
```

**`erase_range` 是对 v1 文档/实现不一致的修复**：v1 设计稿 §7 原写"分区内偏移读写**擦**"，但 v1 实现只有整分区擦（`om_partition_erase(name)` 无 offset）。v2 补齐按范围擦——**扇区对齐由器件层强制**（FlashDev 已校验），本层只做分区内双端边界断言。下一个消费者（日志持久化后端的扇区预擦轮转）依赖此能力。

### 7.4 语义与错误处理

**错误码约定**（注册表**作为入参被检验** → `INVALID_ARG`；注册表被**查穿** → 空表/未命中 = `NOT_FOUND`）：

| 情形 | 返回 |
|---|---|
| 注册表面：`registry_validate` / `registry_at` 收到不可用注册表（`reg == NULL \|\| table == NULL \|\| count == 0`） | `OM_ERR_INVALID_ARG`（入参被检验） |
| 注册表面：`registry_at` 的 `index >= count` | `OM_ERR_NOT_FOUND` |
| 查询面：`query` / `open` 收到不可用注册表 | 空表语义 → `OM_ERR_NOT_FOUND`（沿用 v1） |
| `query` / `open`：名字未命中（表内无畸形条目） | `OM_ERR_NOT_FOUND` |
| `open`：名字未命中**且表中存在畸形条目**（`name == NULL`） | `OM_ERR_INVALID_ARG`（配置错，非查找未命中） |
| `open`：器件不存在 | `OM_ERR_NOT_FOUND` |
| `open`：越器件容量 / 非扇区友好 | `OM_ERR_INVALID_ARG`（fail-fast，旧表不受影响） |
| 操作期：`h == NULL \|\| h->reg == NULL \|\| h->index >= h->reg->count` | `OM_ERR_INVALID_ARG` |
| 操作期：`off`/`len` 越分区 | `OM_ERR_INVALID_ARG`（双端；含 `off+len` 溢出防护，v1 次序保留） |
| `erase_range`：非扇区对齐 | 由器件层返回 `OM_ERR_INVALID_ARG`，**不静默扩擦** |
| `erase_range`：`len == 0` | `OM_OK`（无操作；**句柄有效性仍先行校验**） |
| `read` / `write`：`len == 0` | `OM_OK`（`off` 仍须在域内、`buf`/`data` 仍须非空） |

**两条易被"顺手修正"的语义，须留意**：

1. **`open` 不是全表校验器**——首个名字命中即返回，故表中别处存在畸形条目时命中项仍返 `OM_OK`；全表 fail-fast 是 `registry_validate` 的职责。
2. **畸形条目（`name == NULL`）在扫描中被跳过**（不可匹配、亦不会令 `strcmp` 触硬故障）——这是为"表可来自介质（运行期解析）、`registry_validate` 为可选"这一前提兜底；`query` 对同一张坏表仍返 `NOT_FOUND`（查询面只回答"名字在不在"）。

**并发**：模块零状态 → 全部 API 天然可重入。数据通路的并发归属不变（设备级由 FlashDev 执行域收敛；分区级由表几何契约保证不重叠）。

## 8. 校验分层（v2 重排）

v1 的"注册期全表 fail-fast"随注册动作一并消失，改为三层：

1. **`open` 期**：该分区的扇区友好 + 容量校验——**首次使用即响亮失败**；
2. **`om_partition_registry_validate`（可选）**：全表 fail-fast，保住 v1 的整表保证。应用在上电初始化调用；**引导程序亦建议调用**——一个 N 次循环的代价，而在引导语境里"配置错就停住"的价值最高；
3. **操作期**：句柄域校验 + 分区内边界断言；扇区对齐由器件层强制。

## 9. 与 v1 的差异记账

| 项 | v1 | v2 | 说明 |
|---|---|---|---|
| 表持有 | 模块私有全局 | 调用方持有（可 const） | 模块变零状态 |
| 模块状态 | `s_table`/`s_count` 私有全局 | **无** | 无 init 顺序、无锁、可重入 |
| 寻址 | 每次 I/O 按名重解析 | `open` 一次 → 句柄 | 热路径消除两次名字查找 |
| 防伪造 | name-only 硬契约（严于业界） | 句柄域校验 | **向业界收敛**：Zephyr `flash_area` / MCUboot `flash_area` 均无来源校验（边界按描述符自身字段判，可手搓描述符）。v2 仍**多保留** open 期几何校验 + 可选全表校验；且 `offset/size` 恒从表取，调用方改不动（业界描述符自带这两字段、可改） |
| 几何 fail-fast | 注册期全表（自动） | open 期按分区（自动）+ 全表（需显式调） | 全表保证由隐式变显式 |
| 多实例 | 不支持 | 注册表可多份并存 | 引导程序需求 |
| 分区内擦 | 文档称有、实现没有 | `erase_range` 补齐 | 修文档/实现不一致 |

## 10. 验证

### 10.1 host（`samples/host/partition_test`，随实现重写）

沿用 v1 全部用例（注册结构校验/查询/双端边界/几何拒绝/幽灵器件/篡改拷贝无副作用），v2 新增：

- **多注册表隔离**：两张表并存，同名字不串扰；
- **句柄域校验**：`index >= count` 的伪造句柄被拒；
- **RAM 来源表**：运行期填的注册表与 const 注册表行为一致；
- **`registry_validate`**：全表拒绝路径，且旧表不受影响；
- **`erase_range`**：扇区对齐拒绝 + 分区内边界拒绝。

### 10.2 实机（`samples/pal/partition`，新增）

沿用 `samples/pal/flash/main.c` 的既有安全范式：**验证专用区 = bank2 空区**（应用镜像只占低地址）；**操作前 blank 检查**，非空白（非本程序残留）跳过并报告；串口观测。

实机覆盖 host 覆盖不了的四项：

| # | 项 | host 为何不行 |
|---|---|---|
| R1 | 真实非均匀扇区几何（F427 每 bank `16K×4 / 64K / 128K×7`、24 扇区、bank2 SNB `+4`） | host 用合成几何，`is_partition_sector_aligned` 的区域表跨段遍历走不到真实形状 |
| R2 | 真实"跨 16K→64K 尺寸边界"的分区判定（合法跨段 vs 差一个扇区） | 分区几何校验最易错处，合成几何下平凡 |
| R3 | 真实 FlashDev 通路（XIP 读 / 逐字 program + 回读校验 / EOP 中断驱动擦除） | host 仿真后端是 `memset`/`memcpy`，不走这条路 |
| R4 | 真实擦除耗时（ms 级）下的 `erase_range` 同步语义与让出 | host 擦除瞬时 |

用例：open 成功/失败、真实几何下的错配拒绝、`erase_range` 跨尺寸边界、多注册表隔离、blank 检查。

## 11. 本版不做

- **on-media 分区表格式 + 解析器**（G-07 的另一半）——本版只保证 API 容纳 RAM 来源的表并提供校验入口；格式设计须单独讨论；
- 注册表生命周期管理（调用方拥有）；
- 运行期注册 API（刻意不做——它正是"隐藏全局"的来源）。

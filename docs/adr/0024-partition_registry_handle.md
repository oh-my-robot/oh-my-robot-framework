# ADR-0024：分区表抽象 v2——注册表 + 句柄形态（模块零状态）

- 状态：已决策（2026-09-14）
- 日期：2026-09-14
- 参考：upstream Issue **#75**；`lib/drivers/docs/partition_table_design.md`（v2 设计事实源，§7 接口 / §8 校验分层 / §9 差异记账）；`docs/plans/2026-09-14-partition-api-v2.md`；`docs/internal/active/issue_000_storage_gap_analysis/storage_gap_analysis.md`（G-06 / P1）；ADR-0021 (boot_multi_strategy_skeleton)

## 背景 (Context)

1. v1（2026-09-08 接口定稿：`om_partition_register` + name-only 操作）有两个消费面硬伤：
   - **热路径每次 I/O 两次名字查找**——`om_partition_read(name,…)` 内先 `om_partition_lookup`（strcmp 线性），再 `flash_find` → `device_find`（链表遍历 + 名字比较）；
   - **模块单例**——表经 `om_partition_register` 交入模块私有全局（`static s_table` / `s_count`），一个 binary 只能有一张表。

   差距分析将此记为 **G-06**（P1）：不改则多实例场景被架构性锁死。
2. bootloader 项目开工在即，其四项需求共同否定了 v1 的形态假设：
   1. **免运行期注册**——表须编译期 `const`，无"register 先于一切操作"的隐式时序约束；
   2. **表可来自介质**——表可能由介质在运行期解析而来（驻调用方 RAM），非仅 ROM 常量；
   3. **一 binary 内多表并存**；
   4. **框架无隐藏全局可变状态**。
3. 触发如实记账：本决策**不是推测性设计**。v1 的模块私有形态在定稿时按"无需求不取"延后（原话：Zephyr 公开符号模式"因无 DT 宏需求而不取"）；bootloader 需求具体且近期落地，故该延后撤销。v1 无任何生产消费者，v2 就地取代 v1，不另立双事实源。

## 考虑过的方案 (Options)

- **A（采纳）：注册表对象 + `{reg, index}` 句柄**——表所有权交还调用方；`om_partition_open` 一次完成名字解析 + 器件解析 + 几何校验并产出句柄，数据通路全部走句柄；模块退化为**一组无状态纯函数**。一次改动同时消除两个硬伤，满足全部四项需求。
- **B（否决）：`open` 返回条目指针，每个操作额外接收注册表**——句柄更小，但**每个操作多一个参数**，且存在"句柄与注册表错误配对"的活风险：把一个可校验的域（`index < count`）换成调用方必须自行保证的组合约束。
- **C（否决）：不加句柄，仅给 name-only API 增加注册表参数**——改动最小，但**保留每次 I/O 的名字查找**，本次要解决的热路径问题原样存留。

## 最终决策 (Decision)

- **采用形态 A**，具体接口面（全文见设计文档 §7）：
  1. `OmPartitionRegistry{table, count}` 由调用方持有；`OM_PARTITION_REGISTRY(table)` 宏以数组实参在编译期推导条目数，配合 `static const` 声明即得 ROM 常量注册表——**不做运行期注册 API**（它正是"隐藏全局"的来源，刻意缺席）；表模块只读，驻 ROM 或调用方 RAM 皆可；
  2. `om_partition_open(reg, name, h)` 产出 `OmPartitionHandle{reg, index}`；数据通路 `read` / `write` / `erase` / `erase_range` 一律句柄入口；
  3. 模块**零静态状态**——无私有全局、无 init 顺序依赖、全部 API 天然可重入。
- **校验三层**（取代 v1 的单层注册期校验，见设计文档 §8）：open 期按分区做几何校验（首次使用即响亮失败）；`om_partition_registry_validate` 可选全表 fail-fast（应用上电初始化调用，引导程序亦建议——配置错就停住的价值最高）；操作期做句柄域校验与分区内双端边界断言。扇区对齐恒由器件层强制。
- **错误码契约**（逐函数表见设计文档 §7.4，实现期细化的边界情形以公开头文件注释为准）：`open` 名字未命中 / 器件不存在 / 空表 → `OM_ERR_NOT_FOUND`；open 期越器件容量 / 非扇区友好 → `OM_ERR_INVALID_ARG`；操作期伪造或损坏句柄、分区内越界 → `OM_ERR_INVALID_ARG`；`erase_range` 非扇区对齐由器件层返回 `OM_ERR_INVALID_ARG`，**不静默扩擦**。
- **补 `om_partition_erase_range`**——v1 设计稿 §7 写"分区内偏移读写**擦**"，v1 实现只有整分区擦；本项**修复既有的文档/实现不一致**，下一个消费者（日志持久化后端的扇区预擦轮转）依赖此能力。

## 影响 (Consequences)

- **防伪造硬契约被放宽（显式记账，不委婉）**：v1 的 **name-only 硬契约**——操作只接受分区名、内部对权威表重解析，调用方无法表达地址——**放宽为句柄域校验**。经上游源码核实：**Zephyr `flash_area` 与 MCUboot `flash_area` 均无来源校验**——两者交出已解析描述符，边界按描述符自身字段判定，调用方可以手搓一个描述符。v1 严于业界；v2 收敛到业界基线。
- **v2 仍比业界多做的**：open 期几何校验与可选全表 fail-fast（业界两者皆无；v1 的注册期全表保证由隐式变显式）；且 `offset`/`size` 恒从表取，调用方改不动（业界描述符自带这两字段、可变）。伪造句柄至多指向同注册表内的**既有条目**，地址数值不可伪造。
- **残余风险换类**：从"伪造输入被接受"（输入类）变为"选错条目 / 陈旧注册表"（**内存破坏类**，可寻址他区）——由"open 是唯一正当句柄来源"、表内分区互不重叠的几何契约与调用纪律承担。
- 模块零状态：无 init 顺序约束、无并发保护需求、天然可重入；数据通路并发归属不变（设备级由 FlashDev 执行域收敛）。
- 迁移：`partition_test` 已迁移至 v2 API 并补齐多注册表隔离 / 句柄域校验 / RAM 来源表 / `erase_range` 用例；v1 无生产消费者，应用侧零迁移成本。实现与验证按 `docs/plans/2026-09-14-partition-api-v2.md` 执行。
- **挂起项（本版明确不做，见设计文档 §11）**：on-media 表格式与解析器（本版只保证 API 容纳 RAM 来源表并提供校验入口，格式须单独讨论）；注册表生命周期管理（调用方所有——注册表须比其派生的任何句柄活得更久）；运行期注册 API（刻意缺席）。

# ADR-0010：分散加载自动注册初始化系统

- 状态：已实施（Phase 1-3 + kernel 层合并，rm-a/rm-c-board，gnu-rm + armclang 验证通过）
- 日期：2026-07-30
- 参考：Linux initcall、Zephyr SYS_INIT、RT-Thread auto-init

## 背景 (Context)

OMR 启动当前靠 `main()` 显式调用一长串 `bsp_*_register()` / `xxx_init()`，每增减模块都要改板级 `bsp_cpu.c` 或应用 `main.c`，模块间耦合重。框架曾有一个 `OM_PRE_INIT` 极早期自注册雏形（`.om_pre_init` 段 + 边界符号遍历），但从未接入启动链、零使用、只覆盖单级、且仅 rm-a-board 接了链接脚本（且其 armmlink 别名机制实际有 bug，从未被真实构建检验过）。

期望：模块在自己 `.c` 写一行注册宏即完成挂载，启动时按级别自动执行，`main()` 不再逐一显式调用。

## 考虑过的方案 (Options)

### 级别划分
- **能力轴极简（3 级）**：级只表"调度器前后/能否用 HAL"，层间顺序靠 priority 数字。缺点：层语义隐式、priority 易碰撞。
- **依赖轴镜像分层（采纳）**：每级 = 架构的一层（`EARLIEST/BOARD/DRIVER/SERVICE/SYSTEM/LATE`），级序即层序，自文档化；对应 Zephyr `PRE_KERNEL_1`(提供者)/`PRE_KERNEL_2`(消费者)、Linux `subsys/device`、RT-Thread `BOARD/DEVICE/COMPONENT/APP`。调度器分界不做成一级，而做成"上下文属性"：`EARLIEST/BOARD/DRIVER` 跑在 main（不可阻塞），`SERVICE/SYSTEM/LATE` 跑在 init 线程（可阻塞/IPC，Phase 3 引入）。
- 否决单级扩展（无法表达分层依赖）与 6 级照搬（与 priority 重复）。

### 段布局与排序
- **每级一段 + 链接期 `SORT_BY_NAME` 排同级 prio（Zephyr 风）**：armlink scatter 无段内 SORT 等价物，跨工具链不可移植。
- **每级一段 `.om_init_<N>` + 链接期级别顺序 + 启动期同级 prio 排序（采纳，最终方案）**：级别顺序由链接脚本按段排列保证（Linux/RT-Thread 同款，链接期解析、零运行期成本）；同级 prio 在启动期做一次小排序（表项少，成本可忽略；armlink 无段内排序等价物，故不做链接期 prio 排序）。
- 早期曾采用"单段 + 启动期 (level,prio) 全排序"，后按成熟框架做法改为每级一段（级别顺序链接期）——见本轮演进。

### 自注册 entry 的抽取保障（核心难点）
静态库按需抽取会丢弃无外部引用的自注册 `.o`。`OM_USED`/`KEEP` 都救不回（前者只挡编译器，后者只对已抽取的 `.o` 生效）。候选：
- **`--whole-archive` 包裹框架库**：实测在 XMake 不可行——`add_ldflags` 落在 `-lc -lm` 之后包裹不到 `-ltar` 块；`before_link` 重构 `target:get("links")` 无效（dep 派生的 `-ltar_*` 不在 `links` 属性里）；`set_kind("object")` 经 phony 聚合不传播 `.o`，直接依赖又缺 hard-float ABI 标志。否决。
- **直接注入（采纳）**：新规则 `oh_my_robot.selfreg`，`on_config` 时 glob `lib/systems/src`、`lib/services/src` 的 `.c` 直接 `target:add("files")` 进 binary。直接 `.o` 先链接定义符号，归档成员随后不被抽取，无需剔除、无重复。已验证 entry 存活且 fn/level/prio 正确。
- 否决：锚符号汇总（分层违反 + bootstrap 死锁）、`SHF_GNU_RETAIN`（不挡库抽取）、weak 桩（违反 §11.5 不用 weak）。

### armmlink 边界符号
- **独立别名文件 `.set __om_init_start, Image$$ER_OM_INIT$$Base`（旧 om_pre_init 方案）**：实测 `.set` 别名在最终 ELF 符号表中被省略；且别名文件所在 `.o` 因仅靠 `.set` 提供符号、不被归档抽取。否决。
- **`om_init.c` 内直接 `extern Image$$ER_OM_INIT$$Base` + 宏重命名（采纳）**：armclang 允许 `$` 在标识符中；`om_init.o` 因 `om_do_initcalls` 被引用必然链接，无抽取依赖。已验证。

## 最终决策 (Decision)

- **级别**（依赖轴）：`OM_INIT_LEVEL_EARLIEST/BOARD/DRIVER/SERVICE/SYSTEM/LATE`，枚举 `OmInitLevel`。
- **API**：`OM_INIT(func, level, prio)` 主宏（`__COUNTER__` 唯一符号，允许同 func 多级注册）；`OM_INIT_<LEVEL>(fn)` 别名（默认 prio=50）。回调签名 `OmRet (*)(void)`。
- **段**：每级一段 `.om_init_<N>`（N=级别数值，级别常量定义为宏以便拼段名），链接脚本按级排列 → 级别顺序链接期解析；GCC 每级 `PROVIDE __om_init_<N>_start/end`，armmlink 每级执行域 `ER_OM_INIT_<N>` → `Image$$ER_OM_INIT_<N>$$Base/Limit`（om_init.c 直接 extern + 宏重命名）。
- **编排**：`om_do_initcalls(lo, hi)` 逐级遍历对应段（级别顺序已由链接器保证）、同级按 prio 选择排序、依次调用、失败记录并继续（`OM_INIT_ABORT_ON_FAIL` 可中止）。
- **抽取保障**（两路直连注入，规避静态库按需抽取）：
  - 框架自注册源（`lib/systems/src`、`lib/services/src`）：`oh_my_robot.selfreg` 规则 `on_config` 时 glob 直接 `target:add("files")` 进 binary；
  - 板级自注册源：每个 `bsp_*_impl.c` 在自己文件里 `OM_INIT_BOARD(bsp_*_self_init)` 自注册（包一层 `void→OmRet` wrapper），由 `inputs.lua` 自动 glob `boards/<board>/source/peripherals/**.c`（排除 `override_sources` 的 `_it.c`）→ 进 `selfreg_sources` → 从 `tar_board` 剔除 + `board_assets` 直连 binary；`bsp_cpu.c`（含 `om_board_self_init`、强 `HardFault_Handler`）经板数据 `selfreg_sources` 显式声明。加外设 = 在 `peripherals/` 下新建 `.c` 写 `OM_INIT`，零清单编辑。
- **构建校验**：linkguard 按工具链校验边界符号（GCC 查 `__om_init_*`、armclang 查 `Image$$ER_OM_INIT$$*`），缺失即构建期报错。
- **删除** `OM_PRE_INIT`/`om_pre_init_run()`/`.om_pre_init` 段/`om_pre_init_boundary.c`，无兼容（零使用）。
- **全程零 weak**（符合 §11.5）：entry 为强符号静态实例，边界符号为 PROVIDE/`Image$$`（strong）。

## 影响 (Consequences)

- **正面**：模块/外设作者写 `OM_INIT_<LEVEL>(foo)` 即完成注册（板级外设自动发现）；分层与启动顺序对齐；漏配链接脚本有构建期报错；rm-a/rm-c 双工具链（gnu-rm/armclang）实测通过。
- **约束**：binary 须启用 `oh_my_robot.selfreg` 规则；框架自注册源目录约定为 `lib/systems/src`、`lib/services/src`，板级为 `source/peripherals/`；`EARLIEST/BOARD/DRIVER` 回调不得阻塞（调度器未启）。
- **体积**：框架自注册源会编译两次（archive 一次 + direct 一次，后者实际生效），板级源经剔除只编译一次（direct）；可接受，必要时给框架源也加剔除优化。
- **Phase 3（kernel 层合并 + init 线程 + 调度器分裂，已实现）**：**core 与 osal 合并为 kernel 层**（定义层合并，参考 Linux/Zephyr/RT-Thread 把核心定义与 OS 抽象合为一层；`architecture_reference_manual.md` 已同步）：撤掉 `core↛osal` 规则，`kernel-core`（定义/原语，保持 OS 无关由约定保证，供 samples/host）与 `kernel-os`（osal 抽象）同层可互调。init 子系统留在 kernel 层；启动编排在框架侧——`lib/source/core/om_system_startup.c` 提供 `om_system_startup()`（由 `tar_awkernel` 编译，依赖 core+osal，避免 `tar_awcore→tar_os` 构建环；文件与 om_init.c 同目录，同属 init 子系统）：调度器前 `om_do_initcalls(EARLIEST, SERVICE)` → 建 init 线程（`OSAL_PRIO_CRITICAL_BASE`，跑 `om_do_initcalls(SERVICE, COUNT)`，可阻塞/IPC）→ `osal_kernel_start()`（不返回）。**app 的 main 只调一行** `om_system_startup();`；app 自身启动设置（建业务线程等）与其它模块一样**经 `OM_INIT_APPLICATION` 分散加载**（新增 `OM_INIT_LEVEL_APPLICATION` 级，位于 SYSTEM 后、LATE 前；app 源直接编入 binary，entry 天然存活）。样板见 `_verify_init/main.c` 与 `samples/pal/gpio/main.c`（`supercap_self_init` 在 SYSTEM 级、`gpio_app_setup` 在 APPLICATION 级经 init 线程执行，含 `osal_sleep_ms` 阻塞以证明 post-scheduler 上下文）。
- **后续**：Phase 1-3 已完成 rm-a/rm-c（双工具链验证）；待办——lp-mspm0g3507（需先解预存的 `vApplicationStackOverflowHook`）、其余 sample main 迁移到 init 线程范式。

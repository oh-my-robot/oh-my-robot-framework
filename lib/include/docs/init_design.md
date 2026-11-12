# Init 子系统设计文档

## 概述

Init 子系统是 OM 框架的**分散加载自动注册初始化系统**（参考 Linux initcall / Zephyr SYS_INIT / RT-Thread auto-init）：模块在自己 `.c` 里写一行注册宏即完成挂载，启动时按级别自动执行——**加模块零清单、启动顺序由链接器段序保证、`main()` 由框架提供**。

核心思想：把"初始化"从 `main()` 里的显式调用清单，变成**模块自注册 + 链接期排序 + 启动期编排**三段式：

```
模块写 OM_INIT_<LEVEL>(fn) ──→ 链接器按级别排入 .om_init_<N> 段 ──→ om_system_startup() 逐级执行
```

- 代码位置：设施在 **kernel 层**（API 事实源为公开头文件 `core/om_init.h`）；编排器、启动编排、框架 main 实现随代码库演进，按符号 `om_do_initcalls` / `om_system_startup` / `om_main` 定位（稳定表述约定：本文档不列举实现文件路径，见参考索引）
- 决策记录：ADR-0010（系统设计）、ADR-0013（框架接管 main）
- 移植指南：维护手册 §11.5（链接脚本 `.om_init_<N>` 段）

## 级别模型：依赖轴镜像分层

级别按**架构分层**定义（每级 = 架构的一层），级序即层序（下层先注册、上层可 `device_find` 到下层）：

| 级别 | 数值 | 语义 | 调度器上下文 |
| --- | --- | --- | --- |
| `EARLIEST` | 0 | 硬件/时钟就绪前，仅寄存器操作 | 调度器前（main 帧） |
| `BOARD` | 1 | 板级自举（`om_board_init` @prio0）+ bsp 设备注册（提供者） | 调度器前（main 帧） |
| `DRIVER` | 2 | PAL/适配器/电机驱动（消费者，`device_find` 绑定） | 调度器前（main 帧） |
| `SERVICE` | 3 | comm/log/config/diagnostics | 调度器后（init 线程） |
| `SYSTEM` | 4 | chassis/gimbal/supercap 等业务系统 | 调度器后（init 线程） |
| `APPLICATION` | 5 | **app 自身启动设置（建业务线程等）** | 调度器后（init 线程） |
| `LATE` | 6 | 全部就绪后的自检/诊断收尾 | 调度器后（init 线程） |

**调度器分裂是"上下文属性"而非一级**：`EARLIEST/BOARD/DRIVER` 跑在调度器启动前（`main` 调用帧，框架提供），**回调不得阻塞、不得使用需调度器的 OSAL 服务**；`SERVICE/SYSTEM/APPLICATION/LATE` 跑在 **init 线程**（CRITICAL 带优先级，先于一切业务线程完成），可阻塞/IPC/建线程。

对应成熟框架：Zephyr `PRE_KERNEL_1`（提供者）/`PRE_KERNEL_2`（消费者）、Linux `subsys/device` 分层、RT-Thread `BOARD/DEVICE/COMPONENT/APP`。

## 注册 API

```c
OM_INIT(func, level, prio)          /* 主宏：__COUNTER__ 唯一符号，允许同 func 多级注册 */
OM_INIT_<LEVEL>(fn)                 /* 分级别名，默认 prio=50 */
```

- 回调签名：`OmRet (*)(void)`——返回值 `OM_OK` 成功、`>0` 失败
- **失败语义**：默认记录并继续（启动期 abort = 变砖）；定义 `OM_INIT_ABORT_ON_FAIL` 可在首个失败时中止
- **同级执行顺序由 prio 决定**（启动期一次小排序；armlink 无段内排序等价物，不做链接期 prio 排序）；同 prio 相对顺序不保证，需要强序请用不同 prio
- **级别"只增不删"**：新增级不影响老模块，删除级要改所有注册点

## 段机制：每级一段，级别顺序链接期解析

模块注册回调被放入**每级一段**的 `.om_init_<N>` 段（N = 级别数值，级别常量定义为宏以便拼段名）：

- **级别顺序由链接脚本按段排列保证**（Linux/RT-Thread 同款，链接期解析、零运行期成本）
- **同级 prio 在启动期做一次小排序**（表项数少，成本可忽略）
- **GCC ld**：每级 `PROVIDE(__om_init_<N>_start = .); KEEP(*(.om_init_<N>)); PROVIDE(__om_init_<N>_end = .);`
- **armlink**：每级一个执行域 `ER_OM_INIT_<N>`，自动生成 `Image$$ER_OM_INIT_<N>$$Base/Limit`；`om_init.c` 在 `__ARMCC_VERSION` 下直接 `extern` 引用并宏重命名为 `__om_init_<N>_start/end`（**不要用 `.set` 别名文件**——实测别名在最终符号表中被省略）
- **linkguard 校验**：构建后按工具链校验每级边界符号（GCC 查 `__om_init_*`、armclang 查 `Image$$ER_OM_INIT$$*`，N=0..6），缺失即构建期报错（"该板链接脚本未提供每级段"）

## 自注册 entry 的存活保障（关键机制）

静态库按需抽取会丢弃**无外部引用**的自注册 `.o`——`OM_USED` + `KEEP` 都救不回（前者只挡编译器，后者只对已抽取的 `.o` 生效）。因此：

- **框架自注册源**（`lib/systems/src`、`lib/services/src` 下的 `.c`）：由 `oh_my_robot.selfreg` 规则 `on_config` glob 直接 `target:add("files")` 进 binary——直接 `.o` 先链接定义符号，归档成员随后不被抽取
- **板级外设**：每个 `bsp_*_impl.c` 在自己文件里 `OM_INIT_BOARD(bsp_*_self_init)` 自注册，`inputs.lua` 自动 glob `boards/<board>/source/peripherals/**.c` → `selfreg_sources`（从 `tar_board` 剔除 + `board_assets` 直连）——**加外设 = 在 `peripherals/` 下新建 `.c` 写 `OM_INIT`，零清单编辑**
- binary 目标须启用 `oh_my_robot.selfreg` 规则（与 `context/board_assets/image_convert` 并列）

这是 Linux/Zephyr"全对象链接（obj-y）"目标在 XMake 静态归档模型下的等价实现（XMake 无干净的 per-dep `--whole-archive` 入口，详见 ADR-0010）。

## 启动编排：om_system_startup()

`om_system_startup()`（kernel 层，正常不返回）是启动全链路的编排器：

```
main（框架弱符号）
 └─ om_system_startup()
     ├─ 1. 调度器前：om_do_initcalls(EARLIEST, SERVICE)   ← 板级自举+外设注册+驱动，不可阻塞
     ├─ 2. 建 init 线程（OSAL_PRIO_CRITICAL_BASE）
     │     └─ om_do_initcalls(SERVICE, COUNT)             ← 服务/业务/app 设置，可阻塞/IPC
     └─ 3. osal_kernel_start()                            ← 启动调度器，不返回
```

- init 线程用 **CRITICAL 带**优先级，确保 SERVICE..LATE（含 app 的 `OM_INIT_APPLICATION` 注册）**先于一切业务线程完成**
- app 自身启动设置（建业务线程等）写在自己 `.c`：`static OmRet app_setup(void){...} OM_INIT_APPLICATION(app_setup);`——与其它模块一样经链接器段分散加载，无需显式注册

## main 范式：框架接管（ADR-0013）

**用户不写 `main`**。框架提供弱符号默认入口（Zephyr `kernel/main.c` 同款）：

```c
OM_WEAK int main(int argc, char *argv[])   /* 带参签名：抑制 armclang 为无参 main TU 自动生成的
                                            * 强符号 __ARM_use_no_argv，避免与用户强 main 重复定义 */
{
    (void)argc; (void)argv;
    om_system_startup();
    while (1) {}
}
```

- 经 `oh_my_robot.selfreg` 规则直编进所有 binary；`tar_awcore` 已 `remove_files` 剔除（main 是 binary 级入口，不进静态归档）
- **逃生通道分层**：
  - **L0 默认**：不写 main，业务经 `OM_INIT_APPLICATION` 等注册
  - **L1 自定义启动**：写强 `main` 自动覆盖框架弱符号，内部调公开 API `om_system_startup()` 组装（该函数**保持公开 API，禁止内联/删除**）
  - **L2 完全接管**：写强 `main` 不调框架启动，只链模块（host 测试 / bootloader / 库化嵌入）
- **构建开关**：`om_framework_main`（默认 on，`build/config/options.lua`）；off 时 selfreg 不注入框架 main（宿主工程自带 main / 双弱符号规避），对应 Zephyr `CONFIG_APP_LINK_WITH_MAIN` 反向语义
- **weak 使用边界**：框架侧提供弱符号默认实现属于"框架定义的扩展点"（方向由框架给出、用户强符号覆盖），与应用层自行用 weak 承载业务逻辑（CLAUDE.md 约束）性质不同

## 扩展指南

| 需求 | 做法 |
| --- | --- |
| 加业务模块 | 在模块 `.c` 写 `OM_INIT_SYSTEM(xxx_self_init)`（或对应级别）；源放 `lib/systems/src` 或 `lib/services/src` 自动注入；需要后台任务的在回调里建线程 |
| 加板级外设 | 在 `boards/<board>/source/peripherals/` 新建 `.c`，写 `OM_INIT_BOARD(bsp_xxx_self_init)`——零清单编辑 |
| 加新级别 | 级别枚举/宏 + 每块板链接脚本新增一段（.ld 一节 / .sct 一执行域）+ linkguard 范围同步；遵守"只增不删" |
| 新板移植 | 链接脚本必须包含全部 7 个级别段（详见维护手册 §11.5 模板）；linkguard 会构建期校验 |
| 自定义启动序列 | 写强 `main`，内部按需调 `om_do_initcalls(...)` / 建线程 / `osal_kernel_start()`（L1）；如需更细粒度步骤分解，见 ADR-0013 影响节的演进指引 |
| 宿主/测试入口 | 写强 `main`（L2），不依赖框架启动 |

## 常见陷阱

1. **调度器前回调不得阻塞**：`EARLIEST/BOARD/DRIVER` 跑在调度器启动前，使用需调度器的 OSAL 服务（睡眠/信号量/队列）会死锁或未定义
2. **静态库抽取**：无外部引用的自注册 `.o` 会被按需抽取丢弃——`OM_USED`+`KEEP` 救不回，必须走 selfreg 直编
3. **armclang `__ARM_use_no_argv`**：armclang 会给定义**无参** `main` 的 TU 自动生成强符号；框架 main 用带参签名抑制，用户写强 main 无此问题
4. **失败默认记录并继续**：`OM_INIT_ABORT_ON_FAIL` 才中止——需要"启动失败→亮灯/重启"等分支行为的场景，当前编排器返回值被 `(void)` 丢弃，演进方向为 `om_fatal_error` 挂钩（见 ADR-0013 影响节）
5. **weak 纪律**：应用层不得用 weak 作为扩展机制；框架侧弱符号仅限 main 这类"框架定义扩展点"

## 参考索引

（稳定锚：公开头文件 / 决策记录 / 关联文档；实现文件路径随重构可能变动，以符号 grep 定位，不在本文档列举）

- `core/om_init.h`——API 事实源（类型/宏/级别/段/注意点；编排器/启动编排/main 实现按符号 `om_do_initcalls` / `om_system_startup` / `om_main` 定位）
- `docs/adr/0010-auto_init_system.md`——系统设计决策
- `docs/adr/0013-framework_owned_main.md`——框架接管 main 决策 + 后续演进指引
- `docs/build/maintenance_manual.md` §11.5——新板链接脚本段移植模板
- `lib/include/docs/fatal_design.md`——fatal 设施设计（启动失败收口的下游叙事）

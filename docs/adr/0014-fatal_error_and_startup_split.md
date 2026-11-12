# ADR-0014：致命错误设施（om_fatal_error）+ 启动步骤分解（pre/post scheduler）

- 状态：已实施（rm-a/rm-c 双工具链验证通过）
- 日期：2026-08-16
- 参考：Zephyr `z_fatal_error` / `k_sys_fatal_error_handler`、Linux `panic`、RT-Thread `rtthread_startup` 分步

## 背景 (Context)

1. **启动失败路径无处安放**：`om_system_startup()` 里 `om_do_initcalls` / `osal_kernel_start` 的返回值被 `(void)` 丢弃（ADR-0013 记录的演进项）——initcall 失败、调度器启动失败均为静默行为，无法做"启动失败→亮灯/重启/回退 bootloader"。
2. **框架无统一致命错误收敛点**：HardFault（各板自写强符号）、任务栈溢出 hook（RTOS 配置层）、断言（框架核心无此设施）各自为政。
3. **自定义启动序列无法停在调度器前**：bootloader/裸机诊断需要"硬件就绪+驱动注册完、永不启调度器"，但启动编排只有 `om_system_startup()` 一个整体入口。

## 考虑过的方案 (Options)

### fatal 设施
- **init 子系统私有错误处理（否决）**：致命错误源跨越 kernel（init/断言）、platform（HardFault）、third_party（栈溢出）——私有化无法承载；正确切分是**设施与触发源分离**：设施放 kernel-core（OS 无关），各层触发源各自接入。
- **只复用 OmRet、不设原因枚举（否决）**：OmRet 是"返回值"语义（0=OK），fatal 是"运行时事件"语义，混用拧巴；独立 `OmFatalReason` 枚举（只增不改）表达触发源类别，`cause` 携带具体错误码。
- **"handler 不得返回"靠约定（否决）**：约定太脆弱；**强入口 + handler 返回后禁中断 halt 兜底**——"fatal 永不返回"由入口强制。
- **handler 注册表 vs weak（采纳 weak）**：注册表需要初始化、ISR 上下文不可靠；weak 是"框架侧扩展点"（符合仓库 weak 约束），与 Zephyr `k_sys_fatal_error_handler` 同构。weak 属性只随默认实现（`om_fatal.c`），声明不带——避免用户强覆盖时的属性冲突。

### 启动步骤分解
- **七步细分解（RT-Thread 同款，否决）**：RT-Thread 的 7 步（board/timer/heap/scheduler_init/application/start）是因为它**没有 initcall 分层**，7 步就是它的级别；OMR 已有 7 级 initcall（`OM_INIT_LEVEL_*`），再出七步 API 与 initcall **叠床架屋**。
- **两级 pre/post scheduler（采纳）**：唯一"拆得出自洽状态"的分解点——"硬件就绪、驱动已注册、调度器未启" vs "调度器运行中"。Zephyr 的 SYS_INIT 级别体系本身就以调度器为界（`PRE_KERNEL_1/2` vs `POST_KERNEL/APPLICATION`），证明这是用户可见的合法边界；Zephyr 隐藏内部步骤、暴露级别边界，是同一原则的两面。
- **完全不分解（否决）**：`om_do_initcalls(lo, hi)` 已公开，bootloader 用户可自行拼装——但"建 init 线程（CRITICAL 带）+ 起调度器"的封装与失败路径统一，需要官方出口；两级 API 是对既有能力的官方封装，不是新能力。

## 最终决策 (Decision)

- **设施**（kernel-core，OS 无关，进 `tar_awcore`）：
  - `lib/include/core/om_fatal.h` + `lib/source/core/om_fatal.c`
  - `OmFatalReason` 枚举：当前仅 `OM_FATAL_STARTUP`（启动期失败：initcall/init 线程/调度器启动）；只增不改
  - `om_fatal_error(OmFatalReason reason, OmRet cause)`：强符号唯一入口，**永不返回**——调 handler → 禁中断 halt 兜底；可重入保护（fatal 中再触发直接 halt）
  - `om_fatal_handler(reason, cause)`：weak 默认空实现（声明不带 weak 属性，属性随定义）；用户覆盖做亮灯/软复位/跳 bootloader
  - **禁中断**：经 `om_interrupt.h` 的 `om_hw_disable_interrupt_force()`（= port 层 `port_int_disable()`，注释明确"错误处理等极端场景"），与 `bsp_cpu.c` 既有用法一致
- **步骤分解**（kernel 层，`om_system_startup.c`）：
  - `OmRet om_startup_pre_scheduler(void)`：`om_do_initcalls(EARLIEST, SERVICE)`，返回首个失败错误码
  - `void om_startup_post_scheduler(void)`：建 init 线程（CRITICAL 带）→ `osal_kernel_start()`（不返回）；线程创建失败 / 调度器启动失败 → `om_fatal_error(OM_FATAL_STARTUP, ...)`
  - `om_system_startup()`：= pre + post 默认组合接线；pre 失败 → `om_fatal_error`——**行为不变（纯内部重构 + 失败路径补全）**
- **触发源接入（本次范围，严格限定）**：initcall 失败（**调度器前后对称**：pre 段失败由 `om_system_startup` 检查、init 线程内 `SERVICE..LATE` 失败由线程检查，均 `om_fatal_error(OM_FATAL_STARTUP)`——"记录并继续"仅限 `om_do_initcalls` 扫描内部，调用方级策略为"启动期失败一律显式停机"）/ init 线程创建失败 / 调度器启动失败
- **触发源全部接入**（各触发源各自接入，收敛同一入口）：
  - `OM_ASSERT` → `OM_FATAL_ASSERT`（见 `core/om_assert.h`，携带 `__FILE__:__LINE__`）
  - FreeRTOS `configASSERT` 失败 → `vAssertCalled` → `OM_FATAL_ASSERT`；栈溢出 → `vApplicationStackOverflowHook` → `OM_FATAL_STACK_OVERFLOW`（`platform/osal/freertos/osal_fatal_freertos.c`，板级 FreeRTOSConfig.h 不再自写 printf 宏/本地死循环）
  - **HardFault** → `OM_FATAL_HW_FAULT`：架构共享 strong 实现 `platform/bsp/arch/cortex-m/om_hardfault.c`（naked 汇编捕获异常 PC → `om_fatal_error`），板级 `bsp_cpu.c` 删除自写 while(1)；linkguard 校验 `HardFault_Handler` 必须为 strong（weak = 启动文件兜底仍在生效，收口失效）

## 影响 (Consequences)

- **正面**：启动失败从静默（`(void)` 丢弃）变为**可观测、可恢复**（handler 挂亮灯/重启/跳 bootloader）；自定义启动序列有官方两级组合面（`pre` + 决策逻辑 + `post`）；"fatal 永不返回"由入口强制而非约定；bootloader 场景获得"停在调度器前"的官方路径。
- **约束**：
  - `om_fatal_handler` 覆盖实现不得返回（返回也逃不出入口 halt）；不得在 handler 里依赖调度器/malloc
  - 调度器前无日志设施，fatal **不承诺记录**（入口预留记录点，空实现）
  - pre/post 顺序契约由文档约束（post 依赖 pre 已执行），无运行时校验
  - weak 纪律不变：仅框架侧扩展点；应用层不得自行用 weak
- **兼容**：`om_system_startup()` 行为不变；API 纯新增（`om_fatal_error` / `om_fatal_handler` / `om_startup_pre_scheduler` / `om_startup_post_scheduler`）
- **后续演进**（设施保持最小——恢复是 handler 策略，不内建；以下按触发需求逐步落地）：
  - **触发源扩展**：HardFault 统一收口（platform 层，共享 strong 实现上移 + 删板级自写 + linkguard 校验，独立 PR）；fatal 记录点接日志服务（SERVICE 级就绪后）
  - **受控恢复机制**（参照 Zephyr `sys_reboot` / Linux `panic_timeout` / MCUboot 回退 / WDT 兜底）：
    1. **软复位原语**：port 层 `om_port_system_reset()`——handler 内主动复位（最常用恢复路径）
    2. **超时自动重启样板**：Linux `panic_timeout` 模式——handler 延时 + 复位 + 重启计数（**无人值守场景**；作为 handler 样板文档给出，不内建）
    3. **看门狗兜底**：halt 循环 + 已使能 WDT = 硬件级复位兜底（免费能力，文档写明）
    4. **bootloader 回退**：reboot reason / 启动计数持久化（RTC backup 寄存器或 flash，port 层原语）→ 连续失败 N 次回退上一版本（MCUboot 风格）
  - **边界纪律**：降级模式（AUTOSAR limp-home 类）**不属于 fatal 设施**——是业务层正常错误路径（OmRet 返回值 → 降级策略），fatal 只处理"必须停机/必须重启"
  - host 侧 fatal 语义测试（注入 handler 验证"入口调 handler → halt"链路）

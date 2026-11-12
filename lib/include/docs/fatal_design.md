# Fatal 设施设计文档

## 概述

Fatal 设施是 OM 框架**不可恢复错误的统一收敛入口**（参考 Zephyr `z_fatal_error` / `k_sys_fatal_error_handler`、Linux `panic`）。所有致命触发源——启动失败、断言、任务栈溢出、CPU 硬件异常——统一经 `om_fatal_error()` 进入，用户可覆盖 `om_fatal_handler()` 挂恢复动作（亮灯 / 软复位 / 跳 bootloader）。

核心设计原则（详见 ADR-0014）：

- **设施与触发源分离**：设施只负责"受控地到达恢复点"（调 handler → 禁中断 halt）；触发源各自接入，各层（kernel / platform / RTOS）都可调用
- **入口强制 fatal 语义**：`om_fatal_error()` 永不返回——handler 返回后入口禁中断 halt 兜底，"handler 不得返回"由入口强制而非约定
- **最小设施、策略在外**：不内建恢复逻辑（软复位/超时重启/WDT 兜底/bootloader 回退均为 handler 策略，见 ADR-0014 后续演进节）

代码位置：设施在 **kernel-core**（OS 无关，进 `tar_awcore`；头文件 `core/om_fatal.h` 为 API 事实源）；触发源分布于 **kernel 启动编排**、**RTOS 端口**、**CPU 架构层**——实现文件路径以符号 grep 定位，不在本文档列举。

## API 面

```c
typedef enum {
    OM_FATAL_STARTUP,        /* 启动期失败：initcall / init 线程 / 调度器启动 */
    OM_FATAL_ASSERT,         /* 断言失败（OM_ASSERT / RTOS configASSERT 类机制） */
    OM_FATAL_STACK_OVERFLOW, /* 任务栈溢出（RTOS 检测） */
    OM_FATAL_HW_FAULT,       /* CPU 硬件异常（HardFault） */
} OmFatalReason;             /* 只增不改 */

typedef struct OmFatalContext {
    const char *file;   /* 触发点文件（__FILE__） */
    int         line;   /* 触发点行号（__LINE__） */
    uintptr_t   pc;     /* 触发点 PC（硬件异常返回地址） */
    const char *detail; /* 附加说明（失败回调名 / 溢出任务名） */
} OmFatalContext;       /* 无则全零/NULL */

void om_fatal_error(OmFatalReason reason, OmRet cause, const OmFatalContext *ctx);
/* 强符号唯一入口：调 handler → 禁中断 halt。永不返回，任何上下文可调（ISR 安全） */

void om_fatal_handler(OmFatalReason reason, OmRet cause, const OmFatalContext *ctx);
/* 框架侧 weak 扩展点（默认空实现，weak 属性随定义不随声明——用户定义同名强函数自动覆盖） */
```

## 语义契约

1. **永不返回**：handler 返回后入口 `om_hw_disable_interrupt_force()` 禁中断 + halt 兜底
2. **任何上下文可调**：启动期（调度器前）/ 线程 / ISR——不依赖调度器、不 malloc、不阻塞
3. **可重入保护**：fatal 进行中再触发（handler 内再次 fatal / 中断路径）→ 直接禁中断 halt，不重复调 handler
4. **不承诺记录**：调度器前无日志设施（log 服务是 SERVICE 级）——入口预留记录点为空实现
5. **ISR 安全**：handler 与入口调用链小栈占用（栈溢出触发源场景下栈已损坏）

## 触发源全景

> 稳定表述约定：触发源按**常见触发场景 + 入口符号**描述（场景语义永恒、符号是公开接口，均不随平台/实现重构变动）；不出现具体平台/架构名词（换 RTOS/换芯片文档仍成立）；实现文件路径可从符号 grep 定位，不在本文档列举。

| 常见触发场景 | 入口符号（接入点） | 携带上下文 |
| --- | --- | --- |
| 代码断言失败（状态不变量被违反，`OM_USE_ASSERT` 开启时） | `OM_ASSERT(cond)` | file/line |
| RTOS 内部契约检查失败（configASSERT 类机制） | `vAssertCalled` | file/line |
| 任务栈溢出（RTOS 检测，检测开关开启时） | `vApplicationStackOverflowHook` | detail=任务名 |
| CPU 硬件异常（非法内存访问/未定义指令/总线错误等） | 共享 strong `HardFault_Handler` | pc（异常返回地址） |
| 启动期初始化失败（initcall 返回错误/init 线程创建失败/调度器启动失败） | `om_system_startup()` / init 线程 | detail=失败回调名 |

**收口纪律**：枚举只增不改（新增触发源 = 追加成员 + 接入点 + ADR-0014 同步）；linkguard 校验 `HardFault_Handler` 必须为 strong（weak = 启动文件兜底仍在生效，收口失效，构建期报错）。

## 启动编排接入

`om_startup_pre_scheduler()`（OmRet 传播首个失败）+ `om_startup_post_scheduler()`（建 init 线程 → `osal_kernel_start()` 不返回）——**启动期任何级别 initcall 失败均 fatal**（pre 段 + init 线程内对称），`ctx.detail` 携带失败回调名：

```
om_system_startup() → pre 失败 → om_fatal_error(OM_FATAL_STARTUP, ret, {.detail=om_init_last_fail_name()})
                    → post：线程创建失败 / 调度器启动失败 → om_fatal_error(...)
```

策略：带病启动比显式停机更危险（机器人场景）；"记录并继续"仅限 `om_do_initcalls` 扫描内部，调用方级策略为显式停机。

## 用户指南

**默认行为**：任何致命错误 → 禁中断 halt（死循环）。无日志、无恢复——调试器断点观察 `om_fatal_handler` 即可定位。

**覆盖 handler 实现受控恢复**（用户 .c 里定义强函数，自动覆盖 weak 默认）：

```c
#include "core/om_fatal.h"

void om_fatal_handler(OmFatalReason reason, OmRet cause, const OmFatalContext *ctx)
{
    /* 记录（ctx->file:ctx->line / ctx->pc / ctx->detail）→ 亮错误灯 */
    /* 软复位 / 跳 bootloader / 延时后复位（无人值守自动恢复）——不得返回 */
    for (;;) {}
}
```

**记录现场（组合范例）**：记录不属于 fatal 设施——致命现场的输出是日志服务的"故障直出"能力（om_log_panic：自身禁中断、绕过队列/线程/锁、过滤提满全出、后端最可靠通道；见 services/log/README.md），组合点 = handler 覆盖。默认 handler（空实现）无输出——需要"开箱现场记录"时按下方模板覆盖。

```c
#include "core/om_fatal.h"
#include "services/log/log.h"

OM_LOG_MODULE(main_app, OM_LOG_LEVEL_INFO);

void om_fatal_handler(OmFatalReason reason, OmRet cause, const OmFatalContext *ctx)
{
    om_log_panic(&_om_log_module, OM_LOG_LEVEL_FATAL,
                 "fatal reason=%d cause=%d at %s:%d pc=0x%lx detail=%s",
                 (int)reason, (int)cause,
                 ctx->file ? ctx->file : "?", ctx->line,
                 (unsigned long)ctx->pc,
                 ctx->detail ? ctx->detail : "?");
    for (;;) {}
}
```

恢复机制组合（详见 ADR-0014 后续演进节）：软复位原语（未来 port 层）、超时自动重启（Linux `panic_timeout` 模式）、WDT 兜底（halt 循环 + 已使能看门狗 = 硬件级复位，免费能力）、bootloader 回退（需 reboot reason 持久化，未来）。

**边界纪律**：降级模式（limp-home 类）**不属于 fatal 设施**——业务层不可恢复以外的问题用 OmRet 返回值处理；fatal 只处理"必须停机/必须重启"。

## 常见陷阱

1. **handler 不得返回**：返回也逃不出入口 halt——这是特性（入口强制）而非缺陷
2. **weak 属性只随默认实现**：`om_fatal.h` 声明不带 `OM_WEAK`——声明带 weak 会与用户强覆盖冲突（armclang 尤甚）
3. **静态库抽取**：`HardFault_Handler` 等"覆盖 weak"的强符号必须经 `selfreg_sources` 直连 binary——进归档会被启动文件 weak 先满足引用而永不抽取（linkguard 会以 `weak-only` 报出）
4. **LTO 移除**：被 naked 汇编跳转引用的 C 帮手必须 `OM_USED`——内联汇编引用对编译器/LTO 不可见，armclang 报 L6137E
5. **naked 汇编顺序**：HardFault 的 SP/PC 捕获必须在序言压栈前（naked 函数），否则帧偏移错误
6. **枚举只增不改**：删除/重排枚举值会破坏既有 handler 的 reason 语义
7. **栈溢出场景**：hook 已运行在溢出栈上——handler 内勿做重活（大栈帧/递归）

## 参考索引

（稳定锚：公开头文件 / 决策记录 / 关联文档；实现文件路径随重构可能变动，以触发源全景表的入口符号 grep 定位）

- `core/om_fatal.h`——API 事实源（枚举/context/入口/handler 声明）
- `core/om_assert.h`——框架断言（OM_ASSERT → OM_FATAL_ASSERT）
- `docs/adr/0014-fatal_error_and_startup_split.md`——设施边界 + 颗粒度判据 + 受控恢复演进
- `lib/include/docs/init_design.md`——init 子系统设计（启动编排的上游叙事）

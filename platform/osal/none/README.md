# osal-none（裸机单执行流端口）

无 RTOS 形态的 OSAL 端口（`--os=none`）。语义与裁决见 ADR-0022 (osal_none_bare_metal)
与 ADR-0023 (osal_minimal_core)；设计与验证记录见 `osal_none_core_design.md`。

## 端口构成

| 组成 | 位置 | 性质 |
|---|---|---|
| OSAL 面实现（arch 无关） | `osal_*_none.c` | 本目录直接提供，无需移植 |
| 端口参数义务 | `om_osal_portdef.h` | 优先级上限 / 线程名长度等硬性宏 |
| 端口配置 | `osal_none_cfg.h` | 堆池大小（0 = 禁用堆） |
| **移植面（每 arch 一份）** | `portable/<arch>/osal_none_arch.c` | **新芯片/新架构只需实现本文件** |

## 移植面契约

`portable/<arch>/osal_none_arch.c` 实现 `osal_none_internal.h` 声明的 arch 钩子：

| 钩子 | 职责 |
|---|---|
| `osal_none_time_now_ms()` | 单调毫秒时基（自由运行计数，中断全关可计时；**不得依赖调试域**——DWT/ITC 无调试器不计数） |
| `osal_none_arch_wait_pause()` | 忙等循环让步点（目标 = 空操作；host = 让出时间片） |
| `osal_none_arch_in_isr()` | 中断上下文判断（IPSR 读 / 模拟标志） |
| `osal_none_arch_crit_task_enter()/exit()` | 任务侧临界区（真关中断） |
| `osal_none_arch_crit_isr_enter()/exit()` | ISR 侧临界区 |

构建接线：`xmake.lua` 按构建上下文 `arch` 自动解析 `portable/<arch>/osal_none_arch.c`
注入 `tar_os`；缺失即构建期报错。host 测试的 arch 实现属测试基础设施
（各 host 测试工程自带，如 `samples/host/osal_none_test/osal_none_arch_x64.c`）。

现成实现：`portable/cortex-m4/`（ARMv7-M，SysTick 自由运行时基 + PRIMASK 临界区 +
IPSR 判断；纯 C + 通用内联汇编，gnu-rm / armclang / tiarmclang 通用）。

## 原生约束

- 设备树/板级提供 `SystemCoreClock` 后时基方可工作（时钟初始化先于一切 osal 时间调用）；
- 时基的 SysTick 由 arch 实现懒启动（纯计数、不使能 TICKINT）；需要 tick 中断的
  工程自行置 `SysTick->CSR.TICKINT` 并实现 `SysTick_Handler`。

# OSAL进度说明

> 来源：历史混合文档（按职责拆分：OSAL 机制合同）。


## 0. 维护规则（强制）

- 每次阶段性验收完成后，必须更新本文档。
- 对同一功能的后续重构或修订，必须在原有条目原地更新，不得另起一行规避历史结论。
- 每次更新必须包含：阶段、变更范围、当前状态、验证结果、未解决问题。
- 文档记录必须可由代码路径或验证结果回溯，不得写入不可验证结论。

## 1. 适用范围与依据

- 适用范围：[`oh-my-robot/lib/osal`](../../../../../../lib/osal) 与 [`oh-my-robot/lib/sync`](../../../../../../lib/sync) 相关重构进度。
- 规划依据：
  - [`oh-my-robot/docs/internal/archive/v0.0.0_features/issue_000_process_docs_migration/osal/osal_implementation_plan.md`](osal_implementation_plan.md)
  - [`oh-my-robot/docs/internal/archive/v0.0.0_features/issue_000_process_docs_migration/osal/osal_refactor_plan.md`](osal_refactor_plan.md)
  - [`oh-my-robot/docs/internal/archive/v0.0.0_features/issue_000_process_docs_migration/osal/osal_top_level_design_guide_for_agents.md`](osal_top_level_design_guide_for_agents.md)
- 当前代码参考（time 子域）：
  - [`oh-my-robot/lib/osal/include/osal/osal_time.h`](../../../../../../lib/osal/include/osal/osal_time.h)
  - [`oh-my-robot/platform/osal/freertos/osal_time_freertos.c`](../../../../../../platform/osal/freertos/osal_time_freertos.c)
  - [`oh-my-robot/platform/osal/freertos/osal_time_freertos.h`](../../../../../../platform/osal/freertos/osal_time_freertos.h)
  - `oh-my-robot/samples/stm32f407xx/osal_time/main.c`

## 2. 阶段总览（按阶段原地维护）

| 阶段 | 验收结论 | 当前状态 | 代码证据 | 验证状态 | 未解决问题 |
| --- | --- | --- | --- | --- | --- |
| Phase 0 | 已阶段验收（按既定“规划-确认-执行”完成） | 已冻结，进入维护态 | [`oh-my-robot/docs/internal/archive/v0.0.0_features/issue_000_process_docs_migration/osal/osal_refactor_plan.md`](osal_refactor_plan.md)、[`oh-my-robot/docs/internal/archive/v0.0.0_features/issue_000_process_docs_migration/osal/osal_top_level_design_guide_for_agents.md`](osal_top_level_design_guide_for_agents.md) | 已完成边界冻结与阶段确认 | 需继续按阶段补齐“验收证据可追溯”记录 |
| Phase 1 | 已阶段验收（v1 范围内 time + core + thread + semaphore + mutex + queue + event_flags + timer 收口） | 已收口，进入维护态（`join` 延期至 v2） | [`oh-my-robot/lib/osal/include/osal/osal_time.h`](../../../../../../lib/osal/include/osal/osal_time.h)、[`oh-my-robot/platform/osal/freertos/osal_time_freertos.c`](../../../../../../platform/osal/freertos/osal_time_freertos.c)、[`oh-my-robot/platform/osal/freertos/osal_time_freertos.h`](../../../../../../platform/osal/freertos/osal_time_freertos.h)、`oh-my-robot/samples/stm32f407xx/osal_time/main.c`、[`oh-my-robot/lib/osal/include/osal/osal_core.h`](../../../../../../lib/osal/include/osal/osal_core.h)、[`oh-my-robot/platform/osal/freertos/osal_core_freertos.c`](../../../../../../platform/osal/freertos/osal_core_freertos.c)、[`oh-my-robot/lib/osal/include/osal/osal_queue.h`](../../../../../../lib/osal/include/osal/osal_queue.h)、[`oh-my-robot/platform/osal/freertos/osal_queue_freertos.c`](../../../../../../platform/osal/freertos/osal_queue_freertos.c)、`oh-my-robot/samples/stm32f407xx/osal_queue/main.c`、[`oh-my-robot/lib/osal/include/osal/osal_event.h`](../../../../../../lib/osal/include/osal/osal_event.h)、[`oh-my-robot/platform/osal/freertos/osal_event_freertos.c`](../../../../../../platform/osal/freertos/osal_event_freertos.c)、`oh-my-robot/samples/stm32f407xx/osal_event/main.c`、[`oh-my-robot/lib/osal/include/osal/osal_thread.h`](../../../../../../lib/osal/include/osal/osal_thread.h)、[`oh-my-robot/platform/osal/freertos/osal_thread_freertos.c`](../../../../../../platform/osal/freertos/osal_thread_freertos.c)、[`oh-my-robot/lib/osal/include/osal/osal_sem.h`](../../../../../../lib/osal/include/osal/osal_sem.h)、[`oh-my-robot/platform/osal/freertos/osal_sem_freertos.c`](../../../../../../platform/osal/freertos/osal_sem_freertos.c)、[`oh-my-robot/lib/osal/include/osal/osal_mutex.h`](../../../../../../lib/osal/include/osal/osal_mutex.h)、[`oh-my-robot/platform/osal/freertos/osal_mutex_freertos.c`](../../../../../../platform/osal/freertos/osal_mutex_freertos.c)、[`oh-my-robot/lib/osal/include/osal/osal_timer.h`](../../../../../../lib/osal/include/osal/osal_timer.h)、[`oh-my-robot/platform/osal/freertos/osal_timer_freertos.c`](../../../../../../platform/osal/freertos/osal_timer_freertos.c)、`oh-my-robot/samples/stm32f407xx/osal_timer/main.c` | `xmake build robot_project` 已通过 | `join` 延期到 v2；OSAL 其余原语后续按需求逐项审计 |
| Phase 2 | 部分验收通过（completion 子域） | 进行中（completion 已切换为编译期后端直连；样例已收敛为“功能正确性 + 并发压力”单模式验收） | [`oh-my-robot/lib/sync/include/sync/completion.h`](../../../../../../lib/sync/include/sync/completion.h)、[`oh-my-robot/lib/sync/src/completion.c`](../../../../../../lib/sync/src/completion.c)、`oh-my-robot/samples/sync_completion/main.c` | `xmake build robot_project` 已通过（2026-02-20） | event 子域待收敛；真实 ISR 路径验证待独立样例补齐 |
| Phase 3 | 部分验收通过（selector/capability 子域） | 进行中（capability 宏注入链路已落地） | [`oh-my-robot/platform/sync/xmake.lua`](../../../../../../platform/sync/xmake.lua)、[`oh-my-robot/platform/sync/freertos/sync_accel.lua`](../../../../../../platform/sync/freertos/sync_accel.lua)、[`oh-my-robot/platform/sync/linux/sync_accel.lua`](../../../../../../platform/sync/linux/sync_accel.lua) | `xmake build robot_project` 已通过（2026-02-20） | 分原语 capability 仍待按实际加速后端逐项声明 |
| Phase 4 | 待验收 | 待开始 | 待补录 | 待补录 | 待补录 |
| Phase 5 | 待验收 | 待开始 | 待补录 | 待补录 | 待补录 |

## 3. 当前已确认进展（与阶段总览一一对应）

### 3.1 Phase 0（架构冻结与工程骨架）

- 验收结论：已阶段验收。
- 本阶段定位：冻结边界与执行模式，不在本阶段引入大规模机制改造。
- 当前状态：维护态；后续仅允许按验收结论原地修订，不新增并行版本描述。

### 3.2 Phase 1（OSAL 最小原语）

- 验收结论：已阶段验收（v1 范围收口）。
- 已落地实现进度（time）：
  - `osal_delay_until` 参数语义已统一为 in/out 游标 `deadline_cursor_ms`。
  - 周期延时主路径优先使用 `xTaskDelayUntil`，并保留宏关闭时回退路径。
  - `osal_time` 样例命名已统一为游标语义（`deadline_cursor_ms` / `expected_previous_deadline_ms`）。
- 已落地实现进度（core）：
  - `osal_malloc/osal_free` 已明确严格模式：禁止在 ISR 中调用。
  - 误用处理策略已落地：Debug 断言 + 运行时安全返回（`osal_malloc` 返回 `NULL`，`osal_free` 直接返回）。
  - 已拆分临界区接口为 task/isr 两套语义：`osal_irq_lock_task/unlock_task` 与 `osal_irq_lock_from_isr/unlock_from_isr`。
  - 已移除旧的魔数编码路径（`0x80000000u/0x7FFFFFFFu`），并完成现有调用点迁移。
  - OSAL 六类核心句柄（`thread/sem/mutex/queue/event_flags/timer`）已从 `typedef void*` 升级为不透明强类型指针（`struct xxx_handle*`），并在 FreeRTOS 端统一为本地转换入口，减少散落强转。
- 已落地实现进度（thread）：
  - `osal_thread_attr_t.stackSize` 已统一为“字节”语义，FreeRTOS 端内部完成字节到 `configSTACK_DEPTH_TYPE` 的转换。
  - `osal_thread_create` 已收敛为 `thread` 必填（不可为 `NULL`），并补齐参数校验与失败时句柄清零。
  - `osal_thread_join` 在 v1 延期（当前 FreeRTOS 端保持 `OSAL_NOT_SUPPORTED`），并已补齐线程上下文与参数合法性校验。
  - `osal_thread_create/join/terminate/yield/exit/kernel_start` 已统一线程上下文约束（ISR 误用：断言 + 安全返回）。
  - `osal_thread_terminate` 已保留 `terminate(other)` 机制语义，并完成风险标注：不承诺自动回收目标线程持有的业务资源。
  - `terminate(self)` 已收敛为 `OSAL_INVALID`，强制改用 `osal_thread_exit` 自退出。
  - 新增独立样例：`oh-my-robot/samples/stm32f407xx/osal_thread/main.c`，覆盖 create/self/yield/join/terminate 核心边界。
  - `oh-my-robot/samples/stm32f407` 下已完成线程栈参数迁移：历史按 word 编写的栈配置已显式换算为字节表达（`* OSAL_STACK_WORD_BYTES`）。
- 已落地实现进度（semaphore）：
  - `osal_sem_wait(OSAL_WAIT_FOREVER)` 保持无限等待语义，不做降级。
  - `osal_sem_post/osal_sem_post_from_isr` 在计数已满时统一返回 `OSAL_NO_RESOURCE`。
  - 新增计数查询接口：`osal_sem_get_count` 与 `osal_sem_get_count_from_isr`。
  - `create/delete/wait/post/get_count` 与 `post_from_isr/get_count_from_isr` 上下文边界已收敛（误用：断言 + 安全返回）。
  - `osal_sync` 样例已补充 semaphore 边界验证：`get_count` 与满计数 `post` 行为。
- 已落地实现进度（mutex）：
  - `mutex` 合同已收敛为非递归语义：同线程重复加锁按超时规则处理，不归类为参数非法。
  - `mutex` 解锁语义已收敛：非 owner 解锁返回 `OSAL_INVALID`。
  - `create/delete/lock/unlock` 上下文边界已收敛（线程上下文；误用：断言 + 安全返回）。
  - `osal_sync` 样例已补充 mutex 边界验证：未持有即解锁返回 `OSAL_INVALID`。
- 已落地实现进度（queue）：
  - `queue` 查询接口已状态化：`messages_waiting/spaces_available` 改为 `OSAL status + out 参数`，消除“0 值混淆错误”的接口歧义。
  - FreeRTOS 后端已移除本地 `registry` 影子状态，查询主路径优先采用官方接口。
  - `send/recv/peek` 的 ISR 版本已收敛为上下文强约束（误用：断言 + 安全返回）。
  - ISR 满/空路径返回码已收敛为业务语义 `OSAL_WOULD_BLOCK`，不再错误归类为 `OSAL_INTERNAL`。
  - 新增独立样例：`oh-my-robot/samples/stm32f407xx/osal_queue/main.c`，用于 queue 合同边界验证。
- 已落地实现进度（event_flags）：
  - `create/delete/set/clear/wait` 已统一线程上下文约束（误用：断言 + 安全返回）。
  - `set_from_isr` 已收敛为 ISR-only 合同（误用：断言 + 安全返回）。
  - `set_from_isr` 失败路径返回码已收敛为 `OSAL_NO_RESOURCE`，不再笼统归类 `OSAL_INTERNAL`。
  - 新增 `OSAL_EVENT_FLAGS_USABLE_MASK` 合同，接口固定为 `uint32_t`；公共头不再提供默认兜底，必须由端口层注入。
  - FreeRTOS 端已在 [`oh-my-robot/platform/osal/freertos/xmake.lua`](../../../../../../platform/osal/freertos/xmake.lua) 通过 `tar_awapi_osal` 单点注入 `OM_OSAL_EVENT_FLAGS_USABLE_MASK=0x00FFFFFFu`。
  - 构建约束已收敛为单一来源：`tar_os` 等消费者目标不得重复注入该宏，统一经 `add_deps("tar_awapi_osal")` 继承。
  - FreeRTOS 后端已统一对 `set/clear/wait/set_from_isr` 输入位进行掩码校验，超出掩码返回 `OSAL_INVALID`。
  - `wait` 输出值已收敛为仅返回可用业务位，屏蔽底层控制位。
  - 独立样例 `oh-my-robot/samples/stm32f407xx/osal_event/main.c` 已补充非法高位输入验证，并保留 `wait_any/wait_all/no_clear/timeout` 核心语义覆盖。
- 已落地实现进度（timer）：
  - `osal_timer_create` 已升级为 `status + out_handle` 形态，去除“句柄或 NULL”混合语义。
  - 新增 `osal_timer_mode_t`，明确 `one-shot/periodic` 模式语义，替代裸 `auto_reload` 参数。
  - 新增 `osal_timer_reset`，并固定“重启/重装（rearm）”合同：未运行态 reset 等价 start。
  - `create/start/stop/reset/delete/get_id/set_id` 已统一线程上下文约束（ISR 误用：断言 + 安全返回）。
  - `start/stop/reset/delete` 命令未入队时的返回码已收敛为等待语义映射：
    `timeout=0 -> OSAL_WOULD_BLOCK`，`timeout>0/OSAL_WAIT_FOREVER -> OSAL_TIMEOUT`。
  - 新增独立样例：`oh-my-robot/samples/stm32f407xx/osal_timer/main.c`，覆盖 create/start/stop/reset/delete/get_id/set_id 核心边界。
- 未完成范围：
  - OSAL 其余原语仍待按需求逐项审计与机制重构。
- 待做事项（下一项）：
  - Phase 2.2 event 语义收敛（责任边界冻结 -> reference 合同实现 -> 验证）。

### 3.3 Phase 2（SYNC reference）

- 当前状态：进行中（completion 子域）。
- 细化进度（completion）：
  - completion 合同已冻结为“单等待者 one-shot 完成事件”，不承载驱动业务条件（如水位线/协议判定）。
  - `completion` 公共头已补齐合同注释：`wait` 线程上下文、`done` 可在线程/ISR 调用、重复 `done` 返回 `BUSY`。
  - reference 后端已统一超时映射与状态机收敛：`timeout=0` 非阻塞失败返回 timeout，`done` 重复触发返回 busy。
  - FreeRTOS notify 加速后端已移除：不再使用 task notification index 作为 completion 专用后端。
  - completion 公共 API 已切换为**编译期后端直连**：`OM_SYNC_ACCEL && OM_SYNC_ACCEL_CAP_COMPLETION` 满足时直连 accel，否则直连 reference，不再引入运行期函数指针分发开销。
  - 串口阻塞收发接入已收敛为“消费 completion 通知”，并补充等待返回值处理，不反向修改 completion 语义。
  - 串口阻塞收发已补齐“可中止等待”闭环：阻塞等待支持超时返回；`SUSPEND` 会主动唤醒阻塞等待者；`RESUME` 会重新下发 IO 类型配置，避免挂起后链路失活。
  - 串口 TX 启动失败路径已修复：发送启动失败不再进入 `completion_wait` 永久等待，统一走错误上报并退出当前等待回合。
  - 新增独立样例：`oh-my-robot/samples/sync_completion/main.c`，覆盖 one-shot/单等待者/timeout/重复 done 核心语义。
  - 样例压力模型已收敛为双路径：组 8（线程高并发）+ 组 9（线程模拟 ISR done + 线程 wait），统一单模式验收。
  - 组 8 已引入 `done<waiter / done==waiter / done>waiter` 三态优先级矩阵；`BUSY` 硬门槛仅在 `done>=waiter` 关系下强制。
  - 组 9 采用单线程最高优先级 ISR 模拟触发路径，并以周期性 `sleep` 让出 CPU，保证 waiter 与控制线程可推进。
  - 组 9 已移除芯片头与 NVIC 依赖，避免样例层破坏依赖边界。
  - 超时等待函数已统一改为基于 `osal_time_now_monotonic()` 的真实截止判断，消除 `sleep(1ms)+计数` 在非 1ms tick 下的超时膨胀问题。
  - 样例收尾路径已加入基础设施门禁：超时后采用“stop + 循环补 done”确定性收敛 waiter；收尾失败会中止后续组，避免级联误判污染。
  - 组 6 单等待者合同已修复握手竞态：新增“waiter 已进入等待态”确认后再断言 `OM_ERROR_BUSY`。
  - 验收口径已更新为双轨：
    - 功能正确性轨道：固定轮次 + 严格 one-shot 计数关系（`done_ok >= wait_ok` 且差值不超过 1）。
    - 并发压力轨道：时间窗 + 活性阈值（`wait_ok >= 1`）+ `BUSY` 竞争证据，不再将“未跑满固定轮次”作为压力轨失败条件，也不将该轨道用于性能评级。
  - 收尾阶段补发 done 已与主压力统计口径隔离，避免将停机收敛动作误判为生产者统计异常。

### 3.4 Phase 3（selector 与 capability）

- 当前状态：进行中（selector/capability 子域）。
- 细化进度：
  - [`oh-my-robot/platform/sync/xmake.lua`](../../../../../../platform/sync/xmake.lua) 已支持从各端口 `sync_accel.lua` 的 `capabilities` 字段注入 `OM_SYNC_ACCEL_CAP_*` 编译宏。
  - capability 注入支持两种表达：数组形式（`{"completion"}`）与键值形式（`{completion=true}`）。
  - capability 名称已统一做宏安全归一化（大写 + 非字母数字转下划线），避免平台宏命名漂移。
  - 当前 FreeRTOS/Linux 端 `capabilities` 为空，因此不会注入 completion capability，构建结果保持 reference 回退语义。

### 3.5 Phase 4（双端一致性验证）

- 当前状态：待开始。
- 细化进度：待补录。

### 3.6 Phase 5（可选优化）

- 当前状态：待开始。
- 细化进度：待补录。

## 4. 更新模板（后续阶段直接覆盖原条目）

> 说明：第 3 章的各 Phase 小节必须与第 2 章一一对应，且仅做原地更新，不新增并行记录。

| 阶段 | 功能域 | 当前状态 | 代码证据 | 验证状态 | 未解决问题 |
| --- | --- | --- | --- | --- | --- |
| Phase X | 示例：OSAL queue | 示例：进行中 | 示例：[`oh-my-robot/platform/osal/freertos/osal_queue_freertos.c`](../../../../../../platform/osal/freertos/osal_queue_freertos.c) | 示例：构建通过/用例通过 | 示例：ISR 路径待压测 |


## 来源映射

1. 原文件：历史混合文档
2. 拆分策略：保留原信息并以 OSAL 机制视角组织，SYNC 语义见 oh-my-robot/docs/internal/archive/v0.0.0_features/issue_000_process_docs_migration/sync/sync_requirements_volume.md。







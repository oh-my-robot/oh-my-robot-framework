# ADR-0023：OSAL 收敛为最小原语集（删除队列与事件标志）

- 状态：已决策（2026-09-14）
- 日期：2026-09-14
- 参考：ADR-0022 (osal_none_bare_metal)、`platform/osal/none/osal_none_core_design.md`（A3 修订）、`docs/boot_ota/reference_design_notes.md`（K-19）

## 背景 (Context)

1. OSAL 现提供 8 族（thread/sem/mutex/queue/event/timer/time/core）。复查在树消费者（lib/、platform/，排除端口自身与静态分析锚点）：**queue、event 零真实消费者**。
2. 队列语义在框架内被广泛需要，但全部由组合满足——三个在树实例：
   - 日志统一消息环（`lib/services/src/log/ring.c`）：ringbuf + 门铃 sem（`post_auto`）+ 关中断临界区；且可零 OSAL 退化（`OM_LOG_ASYNC=0` 时现场触发，连 sem 都不引入）；
   - `lib/ipc/src/pipe.c`：同构（ringbuf + sem）；
   - `lib/async/src/workqueue.c`：侵入式链表 + sem + completion（需 O(1) 取消/去重，与"元素拷贝"形态无关）。
   而 `osal_queue` 的"定长元素拷贝 + 阻塞收发"形态与上述三类需求形状均不匹配——真实需求是变长/延迟格式化/滞留回放、流、可取消工作项。
3. 端口成本：os=none 端口按"全族"落地后，queue/event 两族无消费者却占据端口义务；每新增后端均重复该成本。成本公式：`N 后端 × M 族` vs `N 后端 × P 原语 + 组合规则`。
4. 行业边界判据（调研结论）：移植契约型 OSAL 与内核即 API 型框架的共同点——**对象进不进契约由"语义是否依赖内核内部（调度/阻塞/超时链/ISR 唤醒/优先级继承）"决定；可在契约之上无损组合者不入契约**。FreeRTOS 自身即组合范例（sem/mutex 由 queue 派生：`SemaphoreHandle_t = QueueHandle_t`）。CMSIS-RTOS2 作为移植契约型 OSAL 含 7 类对象，但其对象集是"中间件需求的并集 + 全部后端原生可映射"，不含 workqueue/condvar 等派生对象。
5. 框架哲学（分层原语化）要求 OSAL 自身也只承载内核语义原语——队列/事件语义属"纯数据结构 + 原语"的组合层。

## 考虑过的方案 (Options)

- **维持全族**（A3 原决策：全族以统一语义规则提供，裁面只是工程面积选项）。否决：零消费者实证 + 每后端重复实现成本 + 与在树组合实践背离。
- **全族保留但降为派生实现**（`lib/sync/` 提供通用 queue/event）。否决：三类真实需求形状互不相同，通用形态仍无消费者；先提取将再造一个无人使用的形态（YAGNI）。
- **删除 queue/event，队列语义 = 组合规则**。**采纳。**
- 每端口原生加速（completion 的 `OM_SYNC_ACCEL_CAP_*` 模板）。记档为未来选项，本次不需要。

## 最终决策 (Decision)

- **OSAL 端口契约 = {thread, sem, mutex, time, core(irq/isr/mem)} + `osal_priority` 优先级约定**——这些族的语义依赖内核内部（调度单元、阻塞与超时、所有权/优先级继承、硬件时基与中断面）。
- **删除 `osal_queue` 与 `osal_event`**：公共头、freertos 与 none 两端实现、两端 portdef 的事件掩码义务与构建注入、静态分析锚点引用、配套语料。
- **队列/事件语义 = 组合规则**：纯数据结构（`lib/data_struct`，零 OSAL）+ `osal_sem`（门铃/计数）+ 临界区（`osal_irq_lock`）；容量策略（满则丢/阻塞/滞留回放）由消费者按需选定。规则记入 OSAL 文档。
- **提取判据**：同一组合形状出现 ≥2 个消费者时，才提取到 `lib/sync/` 层。
- **修订 A3**（`osal_none_core_design.md`）：面清单由"全族"改为"核心五族 + 组合规则"。

## 影响 (Consequences)

- 正面：端口面收敛——新后端实现五族而非八族；保留族全部有真实消费者与验证路径，契约更稳；与在树组合实践（log/pipe/workqueue）一致。
- 行为：两端不再提供 queue/event；原使用点为零，无代码迁移成本。
- 约束：需要队列语义的模块自行组合（规则入文档）；`osal_config.h` 的队列注册表宏、portdef 的事件掩码义务删除。
- 兼容：host 语料与实机语料的 queue/event 项删除（其余族不受影响）；none 设计文档 A3 加修订注记。
- 后续（记录不实施）：timer 归属（保留 osal——两端原生提供；派生化选项待需求驱动）；静态创建路径（CMSIS 式 `cb_mem`）为未来演进项。

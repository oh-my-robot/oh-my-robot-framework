# ADR-0019：日志统一消息环（printk 收敛——替代队列 + 早期缓冲双形态）

- 状态：已实施（feature/log-ring）
- 日期：2026-09-05
- 参考：ADR-0015 (log_service)、ADR-0018 (deferred_log_drop_point)

## 背景 (Context)

ADR-0015 v2（统一异步）与 ADR-0018（早期缓冲）落地后，log 服务存在**三种投递形态**
（打包入队 / 早期缓冲入环 / 同步兜底直出），语义分裂显著：

1. **deferred 只在异步模式存在**：同步模式（`OM_LOG_ASYNC=0`，无 RTOS/最小配置的出发点）
   无"未就绪窗口"，EARLIEST/BOARD 级（后端未注册）日志静默丢弃——残余缺口
2. **三路径 = 三套文档/断言/回归面**：deferred 在同步/异步语义下的差异需专门说明
3. 成熟系统（Linux printk）的答案是**生产侧统一形态**：消息恒入常驻环形（廉价、锁内、
   上下文安全），消费侧由消费者（console/线程）自由调度——"deferred"是环形未消费的
   自然状态，而非特殊模式。Zephyr 三态模式（IMMEDIATE/DEFERRED/MINIMAL）与 RT-Thread/
   ESP-IDF 的同步直出并存，均被本决策视为形态分裂的佐证与反面。

本 ADR 采纳第 3 点收敛：单一消息环替代「异步队列 + 早期缓冲」双缓冲。

## 考虑过的方案 (Options)

### 容器实现

- **`data_struct/Ringbuf`（SPSC 定长元素环，纯原子无 OSAL）+ 环形门铃**：生产侧临界区
  串行化（多生产者收敛为单写者角色——临界区成本 = 消息拷贝级，与 v2 `osal_queue_send`
  同量级）；消费侧单消费者（日志线程 or 同步现场发射器）；`ringbuf_init` 静态缓冲 =
  **零初始化生命周期可用**（printk 语义关键）。**采纳。**
- **`data_struct/MpscRingbuf`（MPSC CAS + per-slot ready）**：无锁多生产者更强，但
  "队首槽未就绪"等待语义使消费者必须可重试/自旋（环形消费侧重入队 ready 顺序的耦合），
  且单元素拷贝无锁收益不成立——延续 ADR-0015 对 mpsc 的评估。**否决。**
- **`ipc/Pipe`（Ringbuf + 双二值信号量字节流）**：门铃语义（空→非空才 post）经
  `ipc/docs/pipe/pipe_design.md` 实例化，**作为门铃模式采纳**；但 pipe 本体否决：
  ① 字节流按剩余空间截断写——破坏"一条消息"原子性（ADR-0015 原评价仍成立）；
  ② `pipe_init` 必然创建两个 `OsalSem`——调度器前/最小配置不可用（同步模式出发点）；
  ③ 有 write_sem（为"阻塞写者"服务）——满丢策略下不需要。
- **`osal_queue` 定长队列（v2 现状）**：阻塞收/ISR 发/满 WOULD_BLOCK 四合一但**不含
  "无消费者滞留"模型**（队列未创建前是哑区）——无法充当"常驻环"。**否决（不升级）。**

### 双缓冲 vs 单环替换

- **保留队列 + 单独早期环（现状两形态）**：ADR-0018 形态；语义分裂保留。**否决。**
- **单环替换（采纳）**：生产恒入环（消息帧原子）；消费触发二选一（`OM_LOG_ASYNC`）：
  异步 = 日志线程（门铃 ep-take → drain → emit）；同步 = 现场判定（any_accepts →
  drain 全量保序 → emit）。早期无消费者 = 环滞留（deferred 语义回归形态）；
  服务就绪点（SERVICE initcall，同步模式）= drain（回放滞留段）。
  宏：`OM_LOG_QUEUE_LEN`/`OM_LOG_DEFERRED`/`OM_LOG_DEFERRED_BUF_SIZE` 退役 →
  `OM_LOG_RING_LEN`（默认 16，2 的幂，约 1.2KB——与 8 槽队列 + 1KB 早期缓冲预算持平）。

### 丢弃观测

- **丢弃消息以日志身份注入环**（printk "log_buf split" 思想）：SPSC 环满时无槽注入
  （生产侧覆盖接口不存在——`ringbuf_update_out` 为消费侧语义）——**否决（表单不佳）。**
- **保留 ADR-0018 后验告警**（`log_drop_warn`：消费侧/Emit 侧节流补发 WRN，直接 emit
  不走环——防递归）：语义已 host 验证。**采纳（自 deferred.c 迁移至 ring.c）。**

### 时间戳

- 参数包加 `timestamp` 字段（生产时刻：`osal_time_now_monotonic` 线程/ISR 双安全）——
  与 printk 生产时刻打点对齐；同步模式现场发射=同一时刻（既有断言不变）；
  异步模式修正 v2"线程 emit 时刻"失真。**采纳（OmLogMsg +4B）。**

## 最终决策 (Decision)

- **单消息环**：`Ringbuf`（data_struct）+ 临界区串行生产 + 单消费者 + 门铃（异步模式：
  `OsalSem` 二值、空→非空才 post——pipe 模式）；门铃仅在 `OM_LOG_ASYNC` 编译（同步
  模式零 OSAL）；静态缓冲 + 惰性初始化（零 init 生命周期，早期/最小配置可用）。
- **路径收敛**：`om_log_log` = 校验 → 模块级过滤 → 打包（含生产时刻时间戳）→ 生产入环。
  无"就绪判定"；无同步兜底路径（va_list 直出仅 panic 保留：现场当场时间戳、提满）。
- **触发**：异步 = 日志线程（SERVICE init 建门铃+线程；早期生产自然滞留 → 线程启动后
  顺带 drain = 回放；窗口语义统一）；同步 = 生产后 any_accepts(本条) → drain 全量（保
  生产序）→ 现场 emit；无后端消息滞留 → SERVICE init（`OM_INIT_SERVICE`，ring 自身
  注册，仅 !ASYNC）drain 回放（当下表过滤）。
- **丢弃点**：环满 + 参数包超限；`om_log_stats().dropped` 汇总；后验告警保留（节流，
  直接 emit，迁移 ring.c）。
- **宏**：`OM_LOG_RING_LEN`（默认 16、2 的幂、下限 4 `#error` 守卫）；其余三宏退役。
- panic/模块级/后端机制不变；`OM_LOG_ASYNC` 语义 = 消费触发选择器（保留）。

## 影响 (Consequences)

- 正面：形态分裂消亡（"deferred 在同步/异步语义差异"问题消失——deferred 即环滞留态）；
  同步模式（无 RTOS）启动日志闭环（EARLIEST/BOARD 滞留 → SERVICE 回放）；
  时间戳 = 生产时刻（对齐 printk）；门铃/环均为现成组件（无新造原语）；
  无 OSAL 的最小配置下环仍可用（纯原子）。
- 行为：异步模式输出时点与 v2 等效（线程取数即发）；环满丢新 + 告警（同 ADR-0018）；
  `OM_LOG_DEFERRED_BUF_SIZE` 的"启动段保早"语义由"环滞留"接管（消费者就绪前的
  滞留段保持；就绪后环为瞬时缓冲——无消费者时仍滞留）。
- 约束：生产侧临界区 = 消息拷贝级（ISR 打日志守既有短消息纪律）；`%s` 生命周期契约 =
  生产到消费窗口（与 v2 异步一致）；`OM_LOG_RING_LEN` 2 的幂（配置守卫）。
- 兼容：`OM_LOG_DEFERRED`/`OM_LOG_QUEUE_LEN`/`OM_LOG_DEFERRED_BUF_SIZE` 宏删除（接口
  行为不变：`OM_USE_LOG` 裁量、模块/后端/统计/panic API 面不动）；host 测试重构：
  sync 模式两目标（filter 全链 + ring 滞留/回放/满丢/告警节流）；异步模式消费
  （门铃/线程）目标板验证回退（无 OSAL 桩——与 host 脚手架边界一致）。
- 演进：多消费者/在线后端注册（printk console 热插语义）= 环天然支持（后续 v4 评估）。

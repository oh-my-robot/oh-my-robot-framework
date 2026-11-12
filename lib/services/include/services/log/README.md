# Log 服务设计文档

## 定位

services 层日志服务：为全体消费者提供统一的日志记录通道。模块注册制（每模块级别——编译期初始=宏参数 + 运行时按模块调节）、流式格式化、后端抽象（广播 + per-backend 默认级与按模块覆盖）、统一异步投递（就绪路径恒低调用侧成本）、故障直出（fatal/崩溃场景的可靠输出路径）。fatal 设施为下游消费方（handler 组合，见"示例·故障组合"）。

## 机制

### 投递（生产形态唯一：消息环）

- **生产侧（恒一形态）**：调用侧打包参数包（生产时刻时间戳，约 1-2µs）→ 临界区入常驻消息环（`Ringbuf` 定长元素——SPSC 原子，静态零初始化；满 = 丢新 + 计数）——**无"就绪判定"/无"未就绪窗口"**：消费未就绪 = 环中滞留（早期日志 = "deferred"回归形态）
- **消费触发（`OM_LOG_ASYNC` 二选一）**：
  - `1` 异步：日志线程（LOW 带，SERVICE init 建门铃+线程）——门铃（二值信号量，环 空→非空 才 post）take → 抽环 → 格式化+扇出；线程启动首轮 drain 接住启动期滞留（= 回放）
  - `0` 同步（零 OSAL）：生产后判定后端接受（accept_mask——按消息 (module, level) 求值，任一后端接受才 drain）→ 同上下文 drain 全量（保生产序）→ 现场直出；无后端 → 滞留；`OM_INIT_SERVICE`（服务就绪点）→ 回放滞留段（当下后端表过滤）
- **故障直出（`om_log_panic`）**：禁中断、绕过环/线程/锁，调用侧同步格式化（当场时间戳）；**无 per-backend 过滤（提满全出——崩溃现场证据保全）**；级别标注保留；后端提交 = panic 钩子优先（最可靠通道，如串口轮询写——DMA/中断死仍有效）→ NULL 退回 push 尽力而为

### 丢弃点（后验告警）

- 丢弃点（消息决定不再输出处）：**消息环满**（丢新——保滞留段最早序）+ **参数包超限**（`OM_LOG_MAX_ARGS`）
- 丢弃全程不打扰调用方（无失败路径）；**后验告警** = 丢弃不再只进计数——环满由消费侧补发 WRN（`log drop: ring-full dropped <N> (total <M>)`，N=增量、M=累计；节流 `OM_LOG_DROP_WARN_INTERVAL_MS` 防刷屏；消息时间戳 = 补发时刻）；`om_log_stats()` 可查总量
- 无后端接受/级别过滤不属于丢弃（正常过滤语义——流量不守恒是预期）；环滞留段不占"丢弃"语义（消费者就绪前暂存）

### 过滤与分发

- 链路：模块级（入口处、生产前——初始=宏参数；运行时 `om_log_module_set_level` 按名调节——惰性登记后生效）→ 后端级（消费时刻按消息 (module, level) 求值一次——见下）→ 队列级（环满丢弃+计数——见"丢弃点"节，属丢弃非过滤）
- **后端级 = 默认级 + 按模块覆盖**：每后端一张覆盖表（moduleId → level；随后端注册表同生命周期——unregister 清除）；生效级 = 覆盖命中 ? 覆盖值 : 默认级；`OFF` 覆盖 = 显式拒——该模块对该后端全级拒绝（含 FATAL，区别于"仅拒 < FATAL"）
- 组合形态：默认档 + 个别放宽；或注册默认 `OFF` + 覆盖抬升点名模块 = **白名单**（点名模块才收，其余零扇出）；未覆盖模块一律按默认级裁判
- 惰性登记限制：按模块覆盖以模块登记为前提（模块首次打日志入库）——覆盖 API 对未登记模块名 = NOT_FOUND；消息侧 moduleId < 0（未登记/表满）对覆盖表不可查 → 回退默认级（兜底）
- 求值一次 + 零查表扇出：一条消息按 (module, level) 求值一次得接受位图（accept_mask——位 i = 后端 i 接受）→ 位图 = 0 = 被全部后端拒绝 → 零格式化快路径；否则格式化一次（无每后端独立格式化、无消息复制）→ 逐段按位图位移推送命中后端
- FIFO 边界：logger 保证"单生产者 FIFO + 消息原子 + 全后端广播一致"——事务完成点 = push 返回；push 之后的时序/排队/送达归后端自持
- 扇出按**消息级位图快照**推送（求值一次、逐段零查表）——与在途消息的注销交错窗口为"每条消息"粒度（段级不再复检）：后端在该消息求值后被注销，其剩余段仍按旧位图推送

### 消息形态

- 流式分段：32B 段缓冲（可配），任意长消息分多段回调（栈占用恒定为段缓冲+状态）
- 统一结束符 `\n`（后端可做表示层映射，不得改变行边界语义）
- 时间戳 `[HH:MM:SS.mmm]`（单调 ms，十进制；线程/ISR 双安全）
- 消息头：`[LVL][HH:MM:SS.mmm][module] msg\n`

### 失败感知（分层）

- 打日志无失败路径：日志失败绝不打扰调用方（错误码仅出现在管理类 API）
- 后端提交被拒 → 后端自持诊断；队列满/早期缓冲满/参数超限 → log 服务丢计数（`om_log_stats()` 可查）+ 丢弃点补发 WRN 自证（见"丢弃点"节）

## 架构

```
lib/services/include/services/log/log.h     ← 公共 API 事实源
lib/services/src/log/log_internal.h          ← 组件间私有接口
lib/services/src/log/formatter.c             ← 流式格式化 + 规格解析（log_spec_next 唯一事实源）+ 时间换算
lib/services/src/log/msg.c                   ← 参数包打包（计数/抓取/上限丢弃；生产时刻时间戳）
lib/services/src/log/core.c                  ← 服务主体（过滤编排/打包/emit/panic 直出；持有 LogRing 实例与 "log" 告警模块）
lib/services/src/log/backend.c               ← 后端表（注册/注销/级别/广播/panic 投递）
lib/services/src/log/ring.c                  ← 消息环通道能力（LogRing 实例 API：生产/抽环/回放/门铃 post 组合）
lib/services/src/log/log_async.c             ← 异步消费调度器（门铃创建+线程+等待循环——"谁/何时消费"）
lib/services/src/log/stats.c                 ← om_log_stats 汇总
lib/services/src/log/backends/               ← 后端实现（零驱动依赖的随服务家族存放——RTT 在此）
lib/services/src/log/backends/rtt_backend.c  ← RTT 后端（调试通道高带宽；零驱动依赖）
lib/drivers/src/peripheral/serial/           ← 串口后端（drivers→services 单向依赖，随驱动家族存放）
third_party/segger-rtt/                      ← RTT 库本体（外部库——BSD 许可；经 selfreg 注入 binary）
samples/host/om_log_test/                    ← 宿主测试（注入源 + osal 桩）
```

- 依赖：services → kernel（接口/原语）+ data_struct（Ringbuf——纯原子无 OSAL）；drivers → services（开放清单：log.h——单向）；platform 只依赖 kernel/third_party 与 drivers 抽象头（PAL）
- 自注册：服务主体（core.c）经 `OM_INIT_SERVICE`——异步模式启动消费调度器（门铃+线程入实例）、同步模式回放滞留段；backend 注册经组合层 `OM_INIT_DRIVER` 等分散加载

## 接口

```c
/* 级别 */
typedef enum { OM_LOG_LEVEL_DEBUG=0, OM_LOG_LEVEL_INFO, OM_LOG_LEVEL_WARN,
               OM_LOG_LEVEL_ERROR, OM_LOG_LEVEL_FATAL, OM_LOG_LEVEL_OFF, OM_LOG_LEVEL_MAX } OmLogLevel;

/* 模块注册 + 调用宏（每 TU 一次；未注册调用 = 编译错误；上限定 8，可配） */
#define OM_LOG_MODULE(name, level) ...
#define OM_LOG_DEBUG(fmt, ...) / OM_LOG_INFO / OM_LOG_WARN / OM_LOG_ERROR / OM_LOG_FATAL
/*（编译期双重防线：format 属性 + __VA_ARGS__ 计数负数组）*/

/* 后端 */
typedef struct OmLogBackend {
    const char *name;
    void (*push)(OmLogBackend *backend, const char *segment, size_t len);  /* 快速提交 */
    void (*flush)(OmLogBackend *backend);                                  /* 可选 */
    void (*panic)(OmLogBackend *backend, const char *segment, size_t len); /* 可选：故障提交通道 */
} OmLogBackend;
OmRet om_log_backend_register(OmLogBackend *backend, OmLogLevel level);
OmRet om_log_backend_unregister(OmLogBackend *backend);
OmRet om_log_backend_set_level(const char *backend_name, OmLogLevel level);
/* 后端按模块覆盖：后端级 = 默认级 + 覆盖表（生效级 = 覆盖命中 ? 覆盖值 : 默认级）；
   未登记模块名 = NOT_FOUND（模块首次打日志后登记——惰性语义）；OFF 覆盖 = 显式拒（全级） */
OmRet om_log_backend_set_module_level(const char *backend_name, const char *module_name, OmLogLevel level);
OmRet om_log_backend_clear_module_level(const char *backend_name, const char *module_name);
OmRet om_log_backend_get_module_level(const char *backend_name, const char *module_name, OmLogLevel *level);
/* 组合示例：注册默认 OFF + 覆盖抬升点名模块 = 白名单（点名模块才收） */

/* 模块级运行时调节 */
OmRet om_log_module_set_level(const char *module_name, OmLogLevel level);
OmRet om_log_module_get_level(const char *module_name, OmLogLevel *level);

/* 故障直出（fatal/崩溃上下文）——无锁/提满全出/panic 钩子优先 */
void om_log_panic(const OmLogModule *module, OmLogLevel level, const char *fmt, ...);

/* 统计 */
typedef struct { uint32_t dropped; } OmLogStats;
OmRet om_log_stats(OmLogStats *stats);
```

## 配置

| 宏 | 默认 | 说明 |
|---|---|---|
| `OM_USE_LOG` | 1 | 服务级裁剪（值语义：`=0` 或 appcfg `#undef`） |
| `OM_LOG_ASYNC` | 1 | 消费触发选择：1=日志线程（门铃+线程）；0=现场触发（零 OSAL）；`=0` 或 appcfg `#undef` |
| `OM_LOG_MAX_ARGS` | 8 | 参数包上限（1..16，超出 `#error`） |
| `OM_LOG_RING_LEN` | 16 | 消息环槽数（定长 `OmLogMsg`；2 的幂、下限 4 编译期 `#error`；满=丢新+计数+告警） |
| `OM_LOG_DROP_WARN_INTERVAL_MS` | 1000 | 丢弃后验告警最小间隔（毫秒） |
| `OM_LOG_MAX_BACKENDS` | 4 | 后端表上限；受消息级位图承载约束（上限 8——`backend.c` `_Static_assert` 编译期守卫） |
| `OM_LOG_MAX_MODULES` | 16 | 模块注册表上限（惰性登记；按名调节） |
| `OM_LOG_SEGMENT_SIZE` | 32 | 段缓冲（字节；栈占用锚） |
| `OM_LOG_RTT` | 0 | 内置默认 RTT 后端开关（1=零接线隐藏注册，0=仅显式 API 注册） |
| `OM_LOG_RTT_NAME` | `"rtt"` | 默认 RTT 后端名（按名调节用） |
| `OM_LOG_RTT_LEVEL` | `OM_LOG_LEVEL_INFO` | 默认 RTT 后端初始级别（越界编译期 `_Static_assert` 报错） |

全宏在 `om_config.h`，均可经 `om_appcfg.h` 覆写。

### 裁剪契约（`OM_USE_LOG=0`——零副作用）

- **类型无条件**：`OmLogLevel/OmLogModule/OmLogBackend/OmLogStats` 始终定义——外围结构体/回调可继续引用
- **接口消失**：`om_log_*`/`om_rtt_backend_register`/`om_log_serial_backend_register` 声明随 `#if OM_USE_LOG` 消失——调用 = **编译期错误**（信号清晰，而非链接期幽灵接口）
- **调用宏 no-op**：`OM_LOG_*` 展开 `((void)0)`（无参数检查/无 format 校验——与裁决一致）；`OM_LOG_MODULE` 不生成实例（引用 `_om_log_module` 的手写代码——如下游组合层——需自行 `#if OM_USE_LOG`，见 sample/fatal_panic 示范）
- **实现零成本**：log 服务全部 `.c` 以 `#if OM_USE_LOG` 隔离——开关关闭时零 RAM/栈/ROM（类型零占用）；组合层（后端接线/记录调用）由使用方 `#if`（示例即教学模板）

## 示例

**模块使用**：

```c
#include "services/log/log.h"

OM_LOG_MODULE(supercap, OM_LOG_LEVEL_INFO);

void supercap_update(Supercap *cap)
{
    OM_LOG_DEBUG("低于模块级别，编译期裁掉");
    OM_LOG_INFO("电压 %d.%02d V", mv / 1000, mv % 1000);
    OM_LOG_ERROR("电压跌落");
}
```

/** 调试现场调节：om_log_module_set_level("supercap", OM_LOG_LEVEL_WARN); */

**接后端**（组合层 3 行接线）：

```c
static void serial_push(OmLogBackend *backend, const char *seg, size_t len) { /* 推发送缓冲，不阻塞 */ }
static void serial_panic(OmLogBackend *backend, const char *seg, size_t len) { /* 寄存器级轮询写（DMA 死可用） */ }
static OmLogBackend g_serial_backend = { "serial", serial_push, NULL, serial_panic };

static OmRet serial_log_init(void)
{
    return om_log_backend_register(&g_serial_backend, OM_LOG_LEVEL_INFO);
}
OM_INIT_DRIVER(serial_log_init);  /* 早于任何打日志的业务代码 */
```

（RTT 后端零接线版——宏配置即自动注册，组合层无代码；通道 0 固定；观测经调试接口 J-Link RTT Viewer / RTTClient）：

```c
/* om_appcfg.h：OM_LOG_RTT=1（默认后端）；OM_LOG_RTT_NAME/OM_LOG_RTT_LEVEL 可调节 */
/* 注册由后端自行完成（OM_INIT_DRIVER 隐藏注册，早于业务日志）；多后端广播语义不变 */
```

显式 API 版（`om_rtt_backend_register`）保留：多实例/自定义名场景；与 `OM_LOG_RTT=1` 的默认后端同名时注册表查重返回 `OM_ERR_ALREADY`（二选一使用）。

**故障组合**（fatal handler 覆盖——记录现场 + 恢复）：

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
    for (;;) {}  /* 恢复策略（亮灯/复位/回退）——不得返回 */
}
```

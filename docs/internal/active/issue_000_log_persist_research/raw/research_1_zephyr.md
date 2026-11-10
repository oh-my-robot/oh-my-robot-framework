# Zephyr RTOS 日志子系统与持久化后端 — 架构调研

**调研范围**：Zephyr 主线（`zephyrproject-rtos/zephyr`）日志子系统 log core、后端模型、持久化后端（FCB / 文件系统 / ZMS / coredump）。
**基准版本**：主线 `main` 分支（抓取日 2026-09-15）；历史节点以 tag 标注。当前最新稳定版为 **Zephyr 4.4.0（2026-04-14 发布）**，LTS 线为 3.7。
**方法**：以官方文档、主线源码（raw.githubusercontent.com）、Kconfig 原文、GitHub issue/PR 页面与 API 为准；凡未能从一手来源证实者，一律列入「存疑与未证实」节。

---

## 事实

### 1. log core 架构

**F1.1 三段式结构。** 官方文档定义：「Logging consists of 3 main parts:」**Frontend**（过滤、分配缓冲、构建并提交消息）、**Core**（分发到各后端）、**Backends**（各自格式化/输出）。日志源称为 source，可以是 module 或 module 的 instance。
出处：https://docs.zephyrproject.org/latest/services/logging/index.html

**F1.2 三种模式的 Kconfig 定义与取舍。** `LOG_MODE` 是一个 choice（prompt `"Mode"`，依赖 `!LOG_FRONTEND_ONLY`），默认值解析顺序为：`ARCH_POSIX` → `LOG_MODE_IMMEDIATE`；否则 `LOG_DEFAULT_MINIMAL` → `LOG_MODE_MINIMAL`；否则 `LOG_MODE_DEFERRED`。
出处（Kconfig 原文）：https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/Kconfig.mode

- `LOG_MODE_DEFERRED`（prompt `"Deferred logging"`，**selects `MPSC_PBUF`**）：消息被缓冲、稍后处理；对应用影响最小（"least impact"），耗时处理被推迟到已知上下文。
- `LOG_MODE_IMMEDIATE`（prompt `"Synchronous"`）：在调用者上下文里处理，影响性能（例如在高优先级中断中）；**后端必须支持互斥访问**，因为一次日志操作可能被更高优先级上下文打断。
- `LOG_MODE_MINIMAL`（prompt `"Minimal-footprint"`，**selects `PRINTK`**）：在 `printk()` 之上增加极少开销；只支持**编译期过滤**，无运行期过滤；无时间戳、前缀、颜色、异步——一切直接送 `printk()`。

**F1.3 栈开销实测（官方文档表格，2 个整型参数）。** Cortex-M3：deferred **40** 字节 vs immediate **412** 字节；x86：12 vs 388；riscv32：24 vs 456；xtensa：72 vs 504；x86_64：32 vs 1088。immediate 的开销取决于启用了哪些后端。
出处：https://docs.zephyrproject.org/latest/services/logging/index.html

**F1.4 消息队列 = mpsc_pbuf。** 消息存放在循环的 "Multi Producer Single Consumer Packet Buffer" 中。「Each message is a self-contained, continuous block of memory」，且「Messages must be sequentially freed」。消息头字段包括：MPSC bits、trace/log flag、3-bit domain ID、3-bit level、10-bit cbprintf package length、12-bit data length、source descriptor 指针、32/64-bit 时间戳。
出处：https://docs.zephyrproject.org/latest/services/logging/index.html

**F1.5 溢出策略。** `CONFIG_LOG_MODE_OVERFLOW` 在 `mpsc_config.flags` 中置 `MPSC_PBUF_MODE_OVERWRITE`；`CONFIG_LOG_MEM_UTILIZATION` 置 `MPSC_PBUF_MAX_UTILIZATION`。底层存储是静态对齐数组 `buf32[CONFIG_LOG_BUFFER_SIZE / sizeof(int)]`。
出处（源码）：https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/log_core.c

**F1.6 丢消息统计。** `static atomic_t dropped_cnt`；`z_log_dropped(bool buffered)` 自增（buffered 时同时递减 `buffered_cnt`）；`z_log_dropped_read_and_clear()` 返回 `atomic_set(&dropped_cnt, 0)`；`dropped_notify()` 把计数通过 `log_backend_dropped(backend, dropped)` 报给每个 active 后端。上报受 `CONFIG_LOG_FAILURE_REPORT_PERIOD` 与 `last_failure_report` 限流。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/log_core.c

**F1.7 低优先级日志线程与唤醒策略。** Kconfig：`CONFIG_LOG_PROCESS_THREAD`（创建内部处理线程）、`CONFIG_LOG_PROCESS_TRIGGER_THRESHOLD`（缓冲条数达到阈值即唤醒）、`CONFIG_LOG_PROCESS_THREAD_STARTUP_DELAY_MS`。无线程时由用户驱动：`log_thread_set()`、`LOG_INIT()`、`LOG_PROCESS()`、`log_process()`。
源码行为：阈值为 1 时「timer is never needed」——每投递一条就给一次信号量；否则在 `cnt == 0` 时启动 `log_process_thread_timer`（`CONFIG_LOG_PROCESS_THREAD_SLEEP_MS`），当 `cnt + 1` 达到阈值时停表并 give 信号量。`log_thread_trigger()` 停表并 give（在 `CONFIG_LOG_MODE_IMMEDIATE` 下为空操作）。
出处：https://docs.zephyrproject.org/latest/services/logging/index.html ；https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/log_core.c

**F1.8 编译期过滤。** `CONFIG_LOG_DEFAULT_LEVEL`（模块默认级别）、`CONFIG_LOG_OVERRIDE_LEVEL`（仅在模块级别未设或更低时覆盖）、`CONFIG_LOG_MAX_LEVEL`（编译进镜像的最高级别）。
出处：https://docs.zephyrproject.org/latest/services/logging/index.html

**F1.9 运行期过滤的数据结构。** `CONFIG_LOG_RUNTIME_FILTERING` 开启后，每个 source 在 RAM 中有一个过滤器：「Such filter is using 32 bits divided into ten 3 bit slots.」slot 0（bits 0-2）是该 source 的聚合最大设置；slot 1–9 对应各后端。文档示例：slot0=INF, slot1=ERR, slot2=INF，其余 OFF。
出处：https://docs.zephyrproject.org/latest/services/logging/index.html

**F1.10 运行期过滤的固有限制。** 只有编译进去的级别才能被运行期放开（`LOG_MAX_LEVEL` 之上不存在）。另外 `LOG_FMT_SECTION_STRIP` 依赖 `!LOG_ALWAYS_RUNTIME`——因为始终运行期打包需要格式串留在镜像里（提交 cb0a3ce "logging: dictionary: keep string section if always runtime"）。
出处：https://github.com/zephyrproject-rtos/zephyr/issues/42840 ；https://github.com/tejlmand/zephyr/commit/cb0a3ce6aa211b95544e1a75fa9790bdd9ad142f

**F1.11 字典式日志（dictionary-based logging）与 `log_strings` 段。** 字典模式「instead of human readable texts, outputs the log messages in binary format」；构建期生成 `log_dictionary.json`，离线用 `scripts/logging/dictionary/log_parser.py` 解析（可选 `--hex`），在线用 `live_log_parser.py`（`serial` / `jlink-rtt` / `file` 三种模式）。
当 `CONFIG_LOG_FMT_SECTION_STRIP` 开启时，格式串与模块名被放进 ELF 的 **`log_strings`** 段并从镜像中剥离；`scripts/logging/dictionary/database_gen.py` 通过 `REMOVED_STRING_SECTIONS` 读取该段。已知缺陷：在 LTO 下 DWARF 位置信息缺失，`log_dictionary.json` 的 `string_mappings` 不完整，解码端显示 `<string@…>`。
出处：https://docs.zephyrproject.org/latest/services/logging/index.html ；https://github.com/zephyrproject-rtos/zephyr/issues/115815

**F1.12 后端输出格式在 Kconfig 模板中参数化。** `subsys/logging/Kconfig.template.log_format_config` 为每个后端生成 `LOG_BACKEND_$(backend)_OUTPUT` choice（default `_OUTPUT_TEXT`）：`_OUTPUT_TEXT`（select `LOG_OUTPUT`）、`_OUTPUT_SYST`（depends on `LOG_MIPI_SYST_ENABLE`）、`_OUTPUT_DICTIONARY`（select `LOG_DICTIONARY_SUPPORT`，imply `LOG_FMT_SECTION`）、`_OUTPUT_CUSTOM`（"Extern"，自定义格式化函数）。另有隐藏 int `LOG_BACKEND_$(backend)_OUTPUT_DEFAULT`，取值 0/1/2/3，必须与 C 侧 `LOG_OUTPUT_XXX` 一致，且 TEXT 必须编号 0。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/Kconfig.template.log_format_config



### 2. 后端模型与 per-backend 过滤

**F2.1 注册与扇出。** 「Logging backends are registered using `LOG_BACKEND_DEFINE`.」「The macro creates an instance in the dedicated memory section.」「Backends can be dynamically enabled (`log_backend_enable()`) and disabled.」「Logging supports up to 9 concurrent backends.」宏原型：`LOG_BACKEND_DEFINE(_name, _api, _autostart, ...)`（可变参数用于设置 `.ctx`）。
出处：https://docs.zephyrproject.org/latest/services/logging/index.html ；https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/logging/log_backend.h

**F2.2 `struct log_backend_api` 成员。** 必选：`process`（`void (*)(const struct log_backend *const, union log_msg_generic *)`）、`panic`。可选：`dropped`、`init`、`is_ready`、`format_set`、`notify`。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/logging/log_backend.h

**F2.3 事件枚举。** `enum log_backend_evt` 仅有 `LOG_BACKEND_EVT_PROCESS_THREAD_DONE` 与 `LOG_BACKEND_EVT_MAX`；参数联合 `union log_backend_evt_arg` 只有一个成员 `void *raw`。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/logging/log_backend.h

**F2.4 启用/禁用 API 的归属。** `log_backend.h` 只提供 `log_backend_activate/deactivate/is_active`；面向用户的 `log_backend_enable/disable` 声明在 `log_ctrl.h`：
```c
void log_backend_enable(struct log_backend const *const backend, void *ctx, uint32_t level);
void log_backend_disable(struct log_backend const *const backend);
const struct log_backend *log_backend_get_by_name(const char *backend_name);
```
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/logging/log_ctrl.h

**F2.5 `log_filter_set` 签名与语义（per-backend 过滤核心）。**
```c
__syscall uint32_t log_filter_set(struct log_backend const *const backend,
                                  uint32_t domain_id, int16_t source_id, uint32_t level);
uint32_t log_filter_get(struct log_backend const *const backend, uint32_t domain_id,
                        int16_t source_id, bool runtime);
```
`backend == NULL` 表示**所有后端 + frontend**；返回实际生效级别（「may be limited by compiled level」）。文档：「When `LOG_RUNTIME_FILTERING` is enabled, `log_filter_set` can be used to dynamically change filtering of a module logs for given backend. Module is identified by source ID and domain ID.」frontend 另有 `log_frontend_filter_get/set(int16_t source_id, uint32_t level)`。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/logging/log_ctrl.h ；https://docs.zephyrproject.org/latest/services/logging/index.html

**F2.6 内核对过滤的优化（per-backend 过滤成立的关键）。** 只有当**至少一个**后端为该 module 打开了该级别，消息才会被构建。reviewer nordic-krch 在 PR #28892 中的原话：「No, logger supports independent filtering for each backend.」并进一步说明：运行期过滤开启时，「a message is built only if some backend enables that level for that module」。因此被全部后端屏蔽的消息不消耗 mpsc_pbuf 空间。
出处：https://github.com/zephyrproject-rtos/zephyr/pull/28892

**F2.7 shell 命令（官方唯一配置 per-backend 过滤的手段）。** 源码 `subsys/logging/log_cmds.c`，usage 字符串原文：
- `log backend <name> enable <level> <module_0> ... <module_n>` —「enables logs up to given level in specified modules (all if no modules specified)」（`SHELL_CMD_ARG(enable, ..., 2, 255)`：level 必填，最多 255 个模块）
- `log backend <name> disable <module_0> .. <module_n>` —「disables logs in specified modules」
- `log backend <name> status` —「Logger status」
- `log backend <name> go` —「Resume logging」（`cmd_log_backend_go` 会 `log_backend_enable(backend, backend->cb->ctx, CONFIG_LOG_MAX_LEVEL)` 再 `log_backend_activate()`，可用于启动未 autostart 的后端）
- `log backend <name> halt` —「Halt logging」（`cmd_log_backend_halt` → `log_backend_deactivate(backend)`）
- `log list_backends` —「Lists logger backends.」
- `log enable/disable/go/halt/status`（不带 backend 参数，作用于"当前 shell 实例关联的后端"，实现为 `cmd_log_self_*`，受 `SHELL_HAS_DEFAULT_BACKEND` 条件编译）
- `log mem` —「Logger memory usage」，仅 `CONFIG_LOG_MODE_DEFERRED` 下存在
- 后端名与模块名均为**动态补全**（`SHELL_DYNAMIC_CMD_CREATE(dsub_backend_name_dynamic, backend_name_get)` / `dsub_module_name`）
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/log_cmds.c

**F2.8 shell 命令的条件编译。** `CONFIG_SHELL_LOG_BACKEND` / `CONFIG_SHELL_LOG_BACKEND_CUSTOM` / `CONFIG_SHELL_REMOTE_CLI` 三者之一定义 `SHELL_HAS_DEFAULT_BACKEND`；`MULTI_LOG_BACKEND` 在 `!LOG_FRONTEND_ONLY && !SHELL_REMOTE_CLI` 时开启（即 `log backend ...` 与 `log list_backends` 需要它）。`CONFIG_LOG_CMDS` 是总开关。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/log_cmds.c

**F2.9 frontend 被当作一个"后端"来管。** PR #67107 为 frontend 加上运行期过滤：`log list_backends` 会把 frontend（如 `shell_uart_backend`）一并列出，`log backend frontend status` 与 `log frontend status` 等价，`log frontend enable inf hello_world` 可用。
出处：https://github.com/zephyrproject-rtos/zephyr/pull/67107

**F2.10 shell/后端初始化缺陷（2024–2025 修复）。** PR #84955（backport #85160、#89318）：修复 `filter_get`/`filter_set` 取错索引导致 `log backend <uninitialized_backend> status` 显示错误级别；修复 `log list_backends` 中所有未初始化后端都显示 `ID = 0`。
出处：https://github.com/zephyrproject-rtos/zephyr/pull/84955 ；https://github.com/zephyrproject-rtos/zephyr/pull/89318

**F2.11 后端输出助手（`log_backend_std.h`）。** 当前 main 只有三个 inline：`log_backend_std_get_flags()`（把 `CONFIG_LOG_BACKEND_SHOW_TIMESTAMP` / `_SHOW_LEVEL` / `_SHOW_COLOR` / `_FORMAT_TIMESTAMP` / `_CRLF_NONE` / `_CRLF_LFONLY`、`CONFIG_LOG_THREAD_ID_PREFIX`、`CONFIG_LOG_BACKEND_SKIP_SOURCE` 翻译成 `LOG_OUTPUT_FLAG_*` 位掩码）、`log_backend_std_panic(output)`（实际只是 `log_output_flush(output)`）、`log_backend_std_dropped(output, cnt)`（转发 `log_output_dropped_process`）。
**注意**：v2.6.0 时代还存在 `log_backend_std_put()`（`include/logging/log_backend_std.h` 第 46 行；PR #28892 的评审讨论引用过它），当前 main 头文件里已无此函数；现代后端直接调 `log_output_msg_process()`。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/logging/log_backend_std.h ；https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/v2.6.0/include/logging/log_backend_std.h

**F2.12 输出层 API。** `LOG_OUTPUT_DEFINE(_name, _func, _buf, _size)` 展开为 `LOG_OUTPUT_EXT_DEFINE(_name, _func, NULL, _buf, _size)`；`log_output_msg_process(const struct log_output *, struct log_msg *, uint32_t flags)` 是格式化标准入口；`log_output_flush()` 把 `control_block->offset` 字节刷给 `output->func` 并归零。格式类型常量：`LOG_OUTPUT_TEXT 0`、`LOG_OUTPUT_SYST 1`、`LOG_OUTPUT_DICT 2`、`LOG_OUTPUT_CUSTOM 3`。输出 flag：`LOG_OUTPUT_FLAG_COLORS` BIT(0) … `LOG_OUTPUT_FLAG_CORE` BIT(9)（**不存在 `LOG_OUTPUT_FLAG_SOURCE` / `_DOMAIN`**）。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/logging/log_output.h

**F2.13 对照：LOG_BACKEND_NET。** 依赖 `NETWORKING && (NET_UDP || NET_TCP) && NET_SOCKETS && !LOG_MODE_IMMEDIATE`——**明确排除 immediate 模式**，因为否则发出去的 rsyslog 消息会畸变。`LOG_BACKEND_NET_SERVER` 接受 `192.0.2.1:514` / `[2001:db8::1]:514`，前缀 `tcp://` 切 TCP，不给 scheme 则 UDP。`LOG_BACKEND_NET_MAX_BUF_SIZE` range 64–1180，默认 `1180 if NET_IPV6` / `480 if NET_IPV4` / `256` 否则（对齐 RFC 5426）。`LOG_BACKEND_NET_AUTOSTART` 默认 `y if NET_CONFIG_NEED_IPV4 || NET_CONFIG_NEED_IPV6`，help 警告：启动时若到日志服务器无路由，**日志线程可能阻塞**，应关掉 autostart 稍后再启。另有 `LOG_BACKEND_NET_USE_CONNECTION_MANAGER`（默认 y）与 DHCPv4 Log Server Option(7)。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/backends/Kconfig.net

**F2.14 主线后端清单（main）。** `subsys/logging/backends/Kconfig` 的 rsource 列表：`Kconfig.adsp`、`adsp_mtrace`、`ble`、`efi_console`、`fs`、`mqtt`、`native_posix`、`net`、`ws`、`rtt`、`spinel`、`swo`、`uart`、`xtensa_sim`、`multidomain`、`semihost`——**共 16 个，其中没有 `Kconfig.fcb`**；目录内亦无 `log_backend_fcb.c`。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/backends/Kconfig ；https://github.com/zephyrproject-rtos/zephyr/tree/main/subsys/logging/backends

### 3. 持久化后端 A：`log_backend_fcb`（FCB）— 未进主线

**F3.1 PR #28892 的结局：closed / NOT merged。** 标题「log_backend_fcb: Flash logging backend」，作者 **zagor**，开于 **2020-10-03**，3 个 commit、3 个文件（base `master`）；reviewer nordic-krch 与 nvlsianpu 均 request changes；**2021-01-05 被 bot 标记 stale，2021-01-19 自动关闭**，`merged_at = null`。作者在 2020-10-28 表示会「soon」吸收反馈，此后 60+ 天无活动。线程中没有 maintainer 明确说出拒绝理由。
出处：https://github.com/zephyrproject-rtos/zephyr/pull/28892

**F3.2 作者自陈的两个开放问题。** zagor 说这是「a flash logging backend」，用 FCB 做环形缓冲，部分解决 issue #25907；他提出两个未决点：**过滤**（flash 寿命/写入量使 verbose 日志浪费，代码里用的是「a hacky filter based on looking for the `<wrn>` and `<err>` tokens in each log line」）和**取回**（日志是事后读的，需要 API；代码只是在 boot 时把全部条目 print 出来）。
出处：https://github.com/zephyrproject-rtos/zephyr/pull/28892

**F3.3 【核心争辩一：过滤该谁管】** 原文引用：
- nordic-krch 评那个 token 扫描过滤器：「this is not needed, it should be done on logger level (logger runtime filtering)」
- zagor 反驳：「Filtering on the front-end means all backends get filtered the same.」——只有 flash 后端有空间限制；他的场景是：接串口时输出 INF/DEB，不接串口时只把 WRN/ERR 存 flash。
- nordic-krch 纠正：「**No, logger supports independent filtering for each backend.**」并给出 shell 命令示例（对同一 module 在 A 后端关闭、在 B 后端保留），随后开了 issue #29147 讨论把 logger 配置持久化。
**结论**：zagor 的顾虑建立在"过滤是全局的"这一误解上；主线早在当时就已有 per-backend 运行期过滤。「过滤该谁管」的答案在主线是**后端不自己过滤，交给 logger 的 per-backend filter**。
出处：https://github.com/zephyrproject-rtos/zephyr/pull/28892

**F3.4 【核心争辩二：存格式化结果还是原始参数】** nordic-krch 就 `log_backend_std_put` 的用法提问：「**have you considered storing just the message? it would use much less space.**」他同时指出唯一缺点——**重复字符串会丢失，因为 `%s` 指向 RAM**；建议第一版可以退而求其次改成指向常量字符串的指针，并补充「printing/processing would be simpler entry by entry」。此外他要求错误路径「use logger instead of printk」。
出处：https://github.com/zephyrproject-rtos/zephyr/pull/28892

**F3.5 其他评审意见（对设计有实质影响）。**
- 扇区配置：建议加 `LOG_BACKEND_FCB_SECTOR_COUNT`（nordic-krch 追问「default 1?」），并用 `UTIL_LISTIFY` / `ELEMENT_INIT` 宏生成扇区数组，而非逐扇区手配。
- nvlsianpu：「I would not configure each sector apart but rather use dedicated flash area for logging」，给出用 `flash_area_get_sectors` + `ARRAY_SIZE` 的写法；又强调「better us the label instead of the ID」（ID 跨构建不稳定），建议 `LOG_BACKEND_FCB_FLASH_AREA_LABEL` + `fcb_init(FLASH_AREA_ID(...), &fcb)`。
- nvlsianpu 另外三点：删掉中间局部变量 `struct fcb *fcbp`；「ensure that write size is aligned to flash write-block-size」（用 `flash_area_align`）；「introduce rotation count saturation」。
- marinjurjevic 问读路径是否也需要偏移：「Shouldn't FCB_ENTRY_FA_DATA_OFF go here as well?」
- koffes 建议 boot 时若有数据只打一行 WRN，并加 shell 命令 `flog_clear, flog_print_all, flog_print_err, flog_print_wrn`；zagor 同意需要访问函数，但反问这些该属于 FCB 后端还是 logging API。
- pabigot 指向 PR #27486（文件系统日志后端），问是否两者都需要。
出处：https://github.com/zephyrproject-rtos/zephyr/pull/28892

**F3.6 FCB 后端至今不在主线（已核实）。** `subsys/logging/backends/` 目录内无 `log_backend_fcb.c`；`subsys/logging/backends/Kconfig` 的 16 个 rsource 中无 `Kconfig.fcb`；`subsys/logging/backends/Kconfig.fcb` 与 `subsys/logging/log_backend_fcb.c` 在 main 上均返回 404。
出处：https://github.com/zephyrproject-rtos/zephyr/tree/main/subsys/logging/backends

**F3.7 FCB 自身的数据结构与恢复语义（即"若要用 FCB 做后端"所依赖的底座）。**
- `int fcb_init(int f_area_id, struct fcb *fcbp);` —— **调用者必须先填好 `struct fcb` 的前半部分**（`f_magic`、`f_version`、`f_sector_cnt`、`f_scratch_cnt`、`f_sectors`），后半部分是 FCB 内部状态（`f_mtx`、`f_oldest`、`f_active`、`f_active_id`、`f_align`、`fap`、`f_erase_value`）。
- `f_magic`：「Magic value, should not be `0xFFFFFFFF`」，与 `f_erase_value` 的反码异或后写在该 FCB 扇区开头，用于判断扇区是否含有效数据。`f_scratch_cnt`：「Number of sectors to keep empty (usable as scratch space for garbage collection when FCB fills up)」。`f_sectors` 必须**连续**。
- `struct fcb_entry { struct flash_sector *fe_sector; uint32_t fe_elem_off; uint32_t fe_data_off; uint16_t fe_data_len; }`；读数据用 `flash_area_read()`，偏移由 `FCB_ENTRY_FA_DATA_OFF(entry)` 给出，即 `entry.fe_sector->fs_off + entry.fe_data_off`。
- 写入流程：`fcb_append()`（返回 entry 位置）→ `flash_area_write()` → `fcb_append_finish()`。空间不足时调 `fcb_rotate()`「to erase the oldest sector which will make the space」再重试。
- 读取：`fcb_walk()`（回调式）或 `fcb_getnext()`（迭代式，首次传 0 offset 取最老条目）。
- **掉电/撕裂写语义**：文档明确「Entries in the flash are checksummed」「That is how FCB detects whether writing entry to flash completed ok」，且「FCB will skip over entries which don't have a valid checksum」——即**逐条校验和 + 跳过坏条目**，不做原子提交。
- **恢复扫描**：`fcb_init()` **不擦除任何扇区**，而是遍历所有扇区调 `fcb_sector_hdr_read()`：读到 `MK32(fcbp->f_erase_value)` → 扇区未用（返回 0）、magic 不符 → `-ENOMSG`、否则有效（返回 1）。**最新扇区由 `fda.fd_id` 比较（`FCB_ID_GT`）决定，而非地址顺序**；随后 `f_active_id = newest`，并循环 `fcb_getnext_in_sector()` 直到 `-ENOTSUP`（被转成 0）以定位最后一条有效条目。`f_scratch_cnt`/`f_sector_cnt - f_scratch_cnt < 1` 会直接 `-EINVAL`；`f_align` 为 0 或大于写缓冲会 `-ENOMEM`。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/fs/fcb.h ；https://docs.zephyrproject.org/latest/services/storage/fcb/fcb.html ；https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/fs/fcb/fcb.c

**F3.8 需求侧：issue #25907 / #26295 / #26296。**
- **#25907「Request for Flash Logging feature」**，rhardik 开于 **2020-06-02**，**至今 open**；指派 nordic-krch；label `area: Flash` / `area: Logging`。诉求原话：「It's very difficult to connect UART/Jlink to get logs in-field devices」，设备「getting crash after 3 to 4 days」，希望「store the normal/crash logs in flash」，对标 Nordic SDK 的 flash logging。
- **#26295「Enable persistent storage (ext flash/SD card) as logger backend」**，koffes 开于 **2020-06-19**，同日被 carlescufi 以「This is actually a duplicate of #25907.」关闭（closed as completed）。原文：「All log mechanisms today requires the user/developer to have either RTT/Serial/SWO connected to the board to see log output」；两个动机场景：(A) 现场出错时无后端连接，日志可事后用调试器从内部 flash 取、用 USB 从外部 flash 取、或把 SD 卡插到 PC 读；(B) 便携设备接 USB/UART/RTT 不现实，例如「logging battery level over a long period of time」。
- **#26296** 是 #26295 的复制品，同日由 koffes 自己关闭（「Duplicate of #26295」）。
- koffes 提出的方案：新增 `LOG_BACKEND_PERSISTENT_STORAGE`，或拆成 `LOG_BACKEND_EXT_FLASH` / `LOG_BACKEND_SD_CARD` /（可选）`LOG_BACKEND_INT_FLASH`；配置缺失时「you will get an error」；并明说要自备文件系统、文件名与「some kind of full storage handling」。**替代方案（渐进式第一步）**：「provide a LOG_BACKEND_USER or LOG_BACKEND_BLANK」——与 UART 后端同时启用，日志同时去两处，「the specifics of storing to flash or SD card can be left to the user」。
- zagor 在 #25907 下 2020-10-04 留言：「I have submitted #28892 which implements one version of this. But due to the different nature of persistent flash logs versus normal ephemeral logs, I think we want to introduce some new APIs.」——**"持久日志与临时日志性质不同，可能需要新 API"是本议题最有价值的一句判断**。
- manoj153 于 2022-01-23 在 #25907 追问 PR #32973 是否已解决该问题（无人回答）。
出处：https://github.com/zephyrproject-rtos/zephyr/issues/25907 ；https://github.com/zephyrproject-rtos/zephyr/issues/26295 ；https://github.com/zephyrproject-rtos/zephyr/issues/26296

**F3.9 配套需求：issue #29147「Store logger filtering data in persistent memory」至今 open。** nordic-krch 开于 **2020-10-13**，label `area: Flash` / `area: Logging` / `area: File System`，指派给自己。问题：过滤是 per-backend 且可运行期用 shell 改，但「after reboot current configuration is lost」。他举的例子正好是本主题场景：UART 后端多打日志、**file-system crash-log 后端少打日志**。提议加 API `log_config_store()` / `log_config_restore()` 与相应 shell 命令，并指出过滤器本来就「in one memory section so data is there」。**该 issue 自 2020 年起长期停滞。**
出处：https://github.com/zephyrproject-rtos/zephyr/issues/29147

### 4. 持久化后端 B：`log_backend_fs`（文件系统）— 进了主线

**F4.1 PR #32973「logging: File system backend」于 2021-03-22 被 nashif 合并（commit `eb94546`）**，作者 nvlsianpu（Andrzej Puzdrowski），commit 与 Mateusz Syc 共同署名，5 个 commit。开于 2021-03-05，nordic-krch 于 03-09 approve（附注「Reviewed only logging interface」），aunsbjerg 与 de-nordic 的 review 在合并时仍为 "Awaiting requested review"。**它取代了 PR #27486（m-syc 的早期版本）。**
文件落在 **`subsys/logging/log_backend_fs.c`**（已核实 v2.6.0 / v2.7.0 / v3.0.0 三个 tag 均存在），后迁到 `subsys/logging/backends/log_backend_fs.c`。**即：Zephyr 2.6.0 起对外提供文件系统日志后端。**
出处：https://github.com/zephyrproject-rtos/zephyr/pull/32973 ；https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/v2.6.0/subsys/logging/log_backend_fs.c

**F4.2 PR #32973 的评审内容（很轻）。** maintainer nordic-krch：「From logging perspective it looks ok.」，并提出未来方向而非问题：「One thing to consider in the future: you could store log messages」——等日志系统改造后，自包含的消息「take less space and be faster」。koffes 问是否测过 FAT/exFAT/LittleFS，nvlsianpu 答：「Was tested using LittleFS. Will work with any FS supported by VFS API.」lukasbrchl 问怎么知道该下载哪个文件，答：mcumgr 缺该功能，但可用 FS shell 的 `fs list`。**文件轮转/覆盖策略只在 PR 描述里出现（「There is a possibility to overwrite old files or drop new ones」），评审没有辩论它；递归问题在 PR 页面中完全没被讨论。**
出处：https://github.com/zephyrproject-rtos/zephyr/pull/32973

**F4.3 后来去掉了 littlefs 依赖。** 提交 `05a1a5c`「logging: fs: Remove littlefs dependency」使后端对**任意文件系统**可用（只要已挂载或 automount）；Kconfig prompt 改为 "Enable file system backend"，依赖仅 `FILE_SYSTEM`。help 原文：消息「discarded as long as the file system is not mounted」。
出处：https://github.com/zephyrproject-rtos/zephyr/commit/05a1a5c41d2e7c8a1de8dd70c73a70b0a89ce8e7

**F4.4 Kconfig（`subsys/logging/backends/Kconfig.fs`，main）。**
| 符号 | 类型/prompt | 默认 | 说明 |
|---|---|---|---|
| `LOG_BACKEND_FS` | bool "File system backend" | — | 依赖 `FILE_SYSTEM`；**selects `LOG_BACKEND_SUPPORTS_FORMAT_TIMESTAMP`** |
| `LOG_BACKEND_FS_AUTOSTART` | bool "Automatically start fs backend" | `y` | 应用启动时自动启动后端 |
| `LOG_BACKEND_FS_OVERWRITE` | bool "Old log files overwrite" | `y` | 关闭时空间耗尽则**丢弃新消息**（返回 `-ENOSPC`） |
| `LOG_BACKEND_FS_APPEND_TO_NEWEST_FILE` | bool "Append to the newest log file" | `y` | 关闭时**每次启动都新建文件** |
| `LOG_BACKEND_FS_FILE_PREFIX` | string "Log file name prefix" | `"log."` | 前缀后接文件编号 |
| `LOG_BACKEND_FS_DIR` | string "Log directory" | `"/lfs1"` | |
| `LOG_BACKEND_FS_FILE_SIZE` | int "User defined log file size" | `4096`，range `128 1073741824` | 单文件字节上限 |
| `LOG_BACKEND_FS_FILES_LIMIT` | int "Max number of files containing logs" | `10` | 与文件大小共同限定日志占用空间 |
该文件还 `source "subsys/logging/Kconfig.template.log_format_config"`，因此 FS 后端同样可选 TEXT / MIPI SyS-T / **DICTIONARY** / 自定义输出格式（见 F1.12）。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/backends/Kconfig.fs

**F4.5 实现细节（`log_backend_fs.c`，main）。**
- **命名**：`get_log_path()` 用 `snprintf` 拼 `"%s/%s%04d"`，即 `<DIR>/<PREFIX><4位十进制>`；`FILE_NUMERAL_LEN 4`，`MAX_FILE_NUMERAL 9999`，编号从 9999 **回绕到 0**。`get_log_file_id()` 只接受 `FS_DIR_ENTRY_FILE`、长度等于 `LOG_PREFIX_LEN + FILE_NUMERAL_LEN`、`memcmp` 前缀匹配、`atoi` 落在 0..9999 的目录项。
- **状态机**：`BACKEND_FS_NOT_INITIALIZED` → `BACKEND_FS_OK` 或 `BACKEND_FS_CORRUPTED`。
- **挂载检查**：首次写入时 `check_log_volume_available()` 循环 `fs_readmount()`，用 `log_fs_dir_on_mount()` 比对（注释：「Path prefix, not character prefix: same rule as `fs_get_mnt_point()`.」——下一字符必须是 `'/'` 或 `'\0'`）。没有匹配则返回 `-ENOENT`，`write_log_to_file()` **直接 return length，数据被丢弃**。随后 `create_log_dir()` 用 `fs_mkdir()` 逐级创建（注释：「the fist directory name is the mount point」）。
- **轮转**：`write_log_to_file()` 先 `fs_tell(f)`（为负则置 `CORRUPTED`）；当 `(size + length) > CONFIG_LOG_BACKEND_FS_FILE_SIZE` 时调 `allocate_new_file()`。首次分配会扫描目录统计 `file_ctr` 并记录 `max`/`min`，带一个**回绕启发式**：若 `(max - min) > 2 * CONFIG_LOG_BACKEND_FS_FILES_LIMIT`，则认为「oldest log is in the range around the min」，重新扫描确定 `newest`/`oldest`。新编号为 `newest + 1` 并回绕到 0。`APPEND_TO_NEWEST_FILE` 开启时若最新文件还有空间则复用它（注释：「There is space left to log to the latest file, no need to create a new one or delete old ones at this point.」）。
- **容量与覆盖**：分配新文件前循环判断「Check if there is enough space to write file or max files number is not exceeded.」，条件是 `file_ctr >= CONFIG_LOG_BACKEND_FS_FILES_LIMIT` **或** 空闲字节（`stat.f_bfree * stat.f_frsize`）不足一个文件。开 `OVERWRITE` 时调 `del_oldest_log()` 并重跑 `fs_statvfs()`；关 `OVERWRITE` 时返回 `-ENOSPC`。**短写**（`rc != length`）且开启覆盖 → `del_oldest_log()` 并返回 0；否则返回实际长度，注释：「If overwrite is disabled, full memory cause the log record abandonment.」
- **写失败与自我禁用**：`fs_write()` 失败时调 `check_log_file_exist(newest)`——返回 0 意味着「file was lost somehow」→ `file_ctr--` 后重新分配；返回负则「fs is corrupted」→ 跳 `on_error`，置 `BACKEND_FS_CORRUPTED` 并返回 length。**该状态下只剩 OK 分支会写，等于静默停写，但后端并未被正式 deactivate**。唯一显式停用发生在 `panic()` 里：`log_backend_deactivate(backend)`，理由注释：「better to keep current data rather than log new and risk of failure」。`notify()` 收到 `LOG_BACKEND_EVT_PROCESS_THREAD_DONE` 时调 `fs_sync()`，失败则置 corrupted。
- **输出**：`LOG_OUTPUT_DEFINE(log_output, write_log_to_file, buf, MAX_FLASH_WRITE_SIZE)`，缓冲 256 字节、4 字节对齐；`process()` 会剥掉 `LOG_OUTPUT_FLAG_COLORS`。`api` 成员：`.process` / `.panic` / `.init` / `.dropped` / `.format_set` / `.notify`。
- **不支持 immediate 模式**：文件内有 `BUILD_ASSERT`，原话「Immediate logging is not supported by LOG FS backend.」
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/backends/log_backend_fs.c

**F4.6 递归风险（flash 驱动自身打日志）与官方规避手段。** Nordic DevZone 线程：提问者担心底层 flash/SPI 驱动代码里的 LOG 调用会「get's into a recursive loop」。Nordic 的 Elfving 给出的缓解手段是：同时开 `LOG_BACKEND_UART` 与 `LOG_BACKEND_FS`，然后用 Zephyr 的「filter for a certain module at a given backend」能力做**按模块 × 按后端**过滤（参考 `samples/subsys/logging/logger`）；简单粗暴的办法是 `CONFIG_FS_LOG_LEVEL_OFF=y`（代价是丢失可见性）。
**`log_backend_fs.c` 里没有防递归的自禁用逻辑。** 提问者实测的失败不对称性很关键：(1) 只让 flash 写失效（读仍正常）→ **系统挂死**；(2) 读写都失效 → 后端成功自我禁用、系统不挂。他自己改代码加了"连续 `fs_write` 失败即禁用后端"的计数逻辑。该 DevZone 线程未给出 Zephyr 侧修复。
出处：https://devzone.nordicsemi.com/f/nordic-q-a/126106/best-way-to-avoid-recursion-in-zephyr-when-enabling-logging-to-backend-with-littlefs-to-flash-memory

**F4.7 与后端输出模式混用的已知崩溃（2024）。** issue #75452：同时开 `CONFIG_LOG_BACKEND_FS`（dictionary 输出）与 `CONFIG_SHELL_LOG_BACKEND`，在 nrf9160 上 BusFault——因为格式串被从镜像剥离，指针落到无效 flash。规避：`CONFIG_LOG_FMT_SECTION_STRIP=n`；提议的修复是给 `SHELL_LOG_BACKEND` 加 `depends on !LOG_FMT_SECTION_STRIP`。
另 issue #75736：过载一段时间后**所有**后端停摆（STM32H743，UART + 外置 flash littlefs 双后端），补丁改 `log_core.c` 恢复 trigger threshold 可绕过。
出处：https://github.com/zephyrproject-rtos/zephyr/issues/75452 ；https://github.com/zephyrproject-rtos/zephyr/issues/75736

### 5. ZMS / NVS 能否当日志后端

**F5.1 ZMS 是什么。** Zephyr Memory Storage，键值存储，「designed to work with all types of non-volatile storage technologies」，含传统 NOR flash 与可**直接覆写**的 RRAM/MRAM。内存切成至少两个扇区；key 写入 ID-ATE（从扇区底部向上），value 从顶部向下裸存，扇区最后位置放 header ATE 记录状态与 ZMS 版本。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/services/storage/zms/zms.rst

**F5.2 ATE 格式与 CRC。** 「An entry uses 16 bytes to encode its information.」32-bit ID 布局：byte0 `crc8`、byte1 `cycle_cnt`、byte2–3 `len`、byte4–7 `id`，`len <= 8` 时数据内联，另有 `offset`、`data_crc`、`metadata`。`CONFIG_ZMS_ID_64BIT` 时 `id` 占 byte4–11、内联数据限 `len <= 4`，且 **Settings 后端不支持该格式**。格式在运行期靠空 ATE 的 metadata 字段识别；切换格式或对旧数据启用 CRC「will make all existing data invalid」。
CRC 限制原文：「The CRC of the data is checked only when a full read of the data is made.」「The CRC of the data is not checked for a partial read, as it is computed for the whole element.」
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/services/storage/zms/zms.rst

**F5.3 ZMS 的 GC 与擦除（掉电语义的核心）。** 扇区满时：确认下一扇区为空 → 把有效 ATE 从 N+2 搬到 N+1 → 擦 N+2 → 写 GC-done 与 close ATE 后推进当前扇区；到分区末尾后回绕。每扇区有一个 `uint8_t` lead cycle counter 存放在空 ATE 中，用于校验其它 ATE：「Each time an ATE is moved from a sector to another it must get the cycle counter of the destination sector.」擦除方式：「To erase a sector, the cycle counter of the empty ATE is incremented and a single write of the empty ATE is done.」——**一次 16 字节写就作废整个扇区**。close 时用垃圾填满剩余空间，防止旧 ATE 看起来仍有效。**始终保留一个空扇区用于 GC**，且它轮换、从不存用户数据。对免擦除器件这是相对 NVS 的关键优势：一次 16 字节写 vs 4096 字节扇区的 256 次写。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/services/storage/zms/zms.rst

**F5.4 ZMS 的挂载恢复——官方**没有**给出掉电原子性保证。** 挂载是扫描：「it must find the last sector and the last pointer of the entry where it stopped the last time」，定位一个已 close 的扇区及其后的 open 扇区并恢复最后写入的 ATE；「After that, it checks that the sector after this one is empty, or it will erase it.」「By default, `zms_mount` returns an error if the partition cannot be mounted.」；`zms_mount_force`「can be used to automatically wipe and reinitialize the partition when the first mount attempt fails」。**文档把通用恢复列为未来工作：「Add a recovery function that can recover a storage partition if something went wrong.」** 全文没有 "power loss"/"power-fail" 的显式保证语句。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/services/storage/zms/zms.rst

**F5.5 ZMS API。** `int zms_mount(struct zms_fs *fs)`（`-ENOTSUP` 非 ZMS、`-EPROTONOSUPPORT` 版本不支持、`-EINVAL` 参数/扇区布局非法、`-ENXIO`、`-EIO`）；`zms_mount_force`；挂载 flag `ZMS_MOUNT_FLAG_NO_FORMAT = BIT(0)`；`ssize_t zms_write(struct zms_fs *fs, zms_id_t id, const void *data, size_t len)`（**单条最大 64 KiB**；重复写同一份数据返回 0 且不落盘；`-ENOSPC` 满）；`zms_read`、`zms_read_hist`（0=最新，1=次新…）、`zms_delete`、`zms_clear`（**之后必须重新 mount**）、`zms_get_data_length`、`zms_calc_free_space`、`zms_active_sector_free_space`、`zms_sector_use_next`、`zms_set_lookup_cache`（须在 `zms_mount()` 之前调）。
出处：https://docs.zephyrproject.org/latest/doxygen/html/kvss_2zms_8h.html ；https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/services/storage/zms/zms.rst

**F5.6 ZMS vs NVS 的官方取舍。** 文档明确：对 RRAM/MRAM 选 ZMS，因为「designed to avoid emulating erase operation using large block writes」；对**传统 flash 则推荐 NVS**——「is recommended as it has low footprint (smaller ATEs and smaller header ATEs)」，且擦除快、无额外写。ZMS 胜出的场景：64-bit ID（>64K 个 ID）、大的 `write_block_size`、自定义扇区大小。**NVS 未被弃用**：「As of Zephyr release 4.1 the recommended backends for non-filesystem storage are NVS and ZMS」（两者并列）；ZMS 于 4.1 周期新增为 settings 后端（`CONFIG_SETTINGS_ZMS`）。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/services/storage/zms/zms.rst ；https://docs.zephyrproject.org/latest/services/storage/settings/

**F5.7 明确结论：不存在 `log_backend_zms`。** Nordic 官方在 DevZone 回复：ZMS「developed and intended to be used to store data in RRAM and not as a logging backend to a file system」；nRF54L15 上把日志写成文件只能用现有 FS 后端（但会「introduces extra wear and tear on RRAM because it doesn't use ZMS's algorithms」），要 ZMS 落日志**只能自写 custom backend**，且无迹象表明 Nordic 会优先做官方支持。主线后端清单（F2.14）亦无 ZMS。
出处：https://devzone.nordicsemi.com/f/nordic-q-a/124989/for-the-nrf54-series-how-to-write-logs-into-zms-just-by-changing-prj-conf

### 6. panic 落盘 / 崩溃转储

**F6.1 panic 时 log core 做什么（源码 `log_core.c` / `log_ctrl.h`）。** `__syscall void log_panic(void)`，文档：「switches to panic mode；backends must switch to blocking mode or halt」。实现 `z_impl_log_panic()`：已在 panic_mode 则直接返回；否则 `z_log_init(true, false)` 强制初始化（blocking 但不 sleep，注释「If panic happened early logger might not be initialized.」）；若有 `CONFIG_LOG_FRONTEND` 则 `log_frontend_panic()`；然后 `STRUCT_SECTION_FOREACH(log_backend, backend)` 对每个 active 后端调 `log_backend_panic(backend)`；随后是 `/* Flush */` 段——`while (log_process() == true) { }` 忙等排空。**`panic_mode` 标志在 flush 之后才置位**，此后 `z_log_msg_post_finalize()` 中若 `panic_mode` 为真，投递方自己拿 `process_lock` 自旋锁并直接调 `log_process()`——**panic 后的新日志变成同步处理**。
与阻塞相关的 Kconfig：`CONFIG_LOG_BLOCK_IN_THREAD` 与 `CONFIG_LOG_BLOCK_IN_THREAD_TIMEOUT_MS`。
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/log_core.c ；https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/logging/log_ctrl.h

**F6.2 log core 中**没有**通用的"后端正在处理"防递归标志。** 源码里能起作用的只有：`panic_mode`（防 `log_panic` 重入 + 强制内联处理）、`process_lock`/`process_lock_owner_cpu`（仅在 `CONFIG_LOG_IMMEDIATE_CLEAN_OUTPUT` 下，`process_lock_acquire_if_needed`/`release_if_needed`，以 `LOG_NO_CPU_OWNER` 释放）、以及 `backend_attached`（只用于 gate `z_impl_log_process`，注释「Wakeup logger thread after attaching first backend.」）。**没有任何机制在"后端处理消息期间"阻止重入日志。**
出处：https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/logging/log_core.c

**F6.3 官方崩溃落盘方案 = coredump 子系统，不是日志后端。** `DEBUG_COREDUMP_BACKEND_FLASH_PARTITION`「Use flash partition for coredump」，依赖 `FLASH`，selects `FLASH_MAP` 与 `STREAM_FLASH`，用 DTS alias `"coredump-partition"` 指定分区。同级还有 `DEBUG_COREDUMP_BACKEND_LOGGING`、`DEBUG_COREDUMP_BACKEND_NULL`、`DEBUG_COREDUMP_BACKEND_OTHER`。
出处：https://docs.zephyrproject.org/latest/services/debugging/coredump.html ；https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/debug/coredump/Kconfig

**F6.4 coredump flash 后端的记录格式与恢复/导出流程。** `subsys/debug/coredump/coredump_backend_flash_partition.c` 注册 `struct coredump_backend_api`，回调 `.start` / `.end` / `.buffer_output` / `.query` / `.cmd`。通过 **stream flash 接口**写（不必手动跟踪偏移）；因为内存（如线程栈）仍在变化，`coredump_flash_backend_buffer_output()` 把数据分块拷进临时缓冲并累积校验和，使校验和与实际写入内容一致；**分区开头写一个带 checksum 与 error 字段的 coredump header**。
- 查询：`COREDUMP_QUERY_GET_ERROR`、`COREDUMP_QUERY_HAS_STORED_DUMP`、`COREDUMP_QUERY_GET_STORED_DUMP_SIZE`
- 命令：`COREDUMP_CMD_CLEAR_ERROR`、`COREDUMP_CMD_VERIFY_STORED_DUMP`、`COREDUMP_CMD_ERASE_STORED_DUMP`、`COREDUMP_CMD_COPY_STORED_DUMP`（参数 `coredump_cmd_copy_arg` 含 offset/buffer/length）、`COREDUMP_CMD_INVALIDATE_STORED_DUMP`
- shell 命令由 `CONFIG_DEBUG_COREDUMP_SHELL` 提供
**这是主线里唯一"掉电/复位后仍留存现场 + 可主动拉取导出（COPY_STORED_DUMP / VERIFY / ERASE）"的官方范式。**
出处：https://docs.zephyrproject.org/latest/services/debugging/coredump.html

**F6.5 崩溃时直接写外置 flash 的官方态度：不推荐。** Nordic DevZone 线程：在 fatal handler 里直接用 QSPI 是被劝阻的，因为「the driver/system state is unknown」；建议的替代路径正是「store the exception frame in a `__noinit`/retained RAM section and write it to flash on reboot」，或基于 `coredump_backend_empty.c` 写自定义 backend（`CONFIG_DEBUG_COREDUMP_BACKEND_OTHER`），或用 Memfault 的方案。另外 nRF 上 flash 后端可能开箱不可用——`nordic,qspi-nor.yaml` 缺 `soc-nv-flash.yaml` 的 `erase-block-size` / `write-block-size` 属性，而实现需要它们。
出处：https://devzone.nordicsemi.com/f/nordic-q-a/101071/saving-coredumps-to-external-flash

### 7. 时间线与社区结论

**F7.1 关键时间线。**
| 日期 | 事件 | 结局 |
|---|---|---|
| 2020-06-02 | issue #25907「Request for Flash Logging feature」 | **至今 open** |
| 2020-06-19 | issue #26295 / #26296 提出持久化存储后端 | 当日被 maintainer 判为 #25907 的重复而关闭 |
| 2020-10-03 | PR #28892 `log_backend_fcb`（zagor） | request changes → stale → **2021-01-19 自动关闭，未合并** |
| 2020-10-13 | issue #29147 把 logger 过滤配置持久化 | **至今 open** |
| 2021-03-05 | PR #32973「logging: File system backend」（nvlsianpu，取代 #27486） | **2021-03-22 合并** |
| 2021 年内 | `05a1a5c` 去掉 littlefs 依赖；增加 dictionary 输出到文件；移除 LOG1 支持 | 已合并 |
| 2022-01-23 | issue #25907 下追问 PR #32973 是否解决问题 | 无人回答 |
| 2024 | issue #75452（多后端不同输出模式崩溃）、#75736（过载后全部后端停摆） | 报告/规避 |
| 2025-04-30 | PR #89318 backport #84955/#87097 的后端初始化与 shell 命令修复到 v3.7 | 已合并 |
| 2026-04-14 | Zephyr 4.4.0 发布（最新稳定版）；LTS 线为 3.7 | — |

出处：https://github.com/zephyrproject-rtos/zephyr/issues/25907 ；https://github.com/zephyrproject-rtos/zephyr/pull/28892 ；https://github.com/zephyrproject-rtos/zephyr/pull/32973 ；https://github.com/zephyrproject-rtos/zephyr/issues/29147 ；https://github.com/zephyrproject-rtos/zephyr/pull/89318 ；https://github.com/zephyrproject-rtos/zephyr/releases/tag/v4.4.0

**F7.2 哪些进了主线、哪些没有。**
- **进了**：文件系统日志后端 `LOG_BACKEND_FS`（2.6.0 起）；per-backend 运行期过滤 + shell 命令；dictionary 输出模式；flash 分区 coredump 后端；ZMS（4.1 起作为 settings 后端，**不是**日志后端）。
- **没进 / 停滞**：FCB 日志后端（PR #28892 自动关闭）；任何名为 `LOG_BACKEND_PERSISTENT_STORAGE` / `LOG_BACKEND_EXT_FLASH` / `LOG_BACKEND_SD_CARD` / `LOG_BACKEND_INT_FLASH` 的符号（issue #26295 的原始提案未被采纳）；`LOG_BACKEND_USER` / `LOG_BACKEND_BLANK`（备选提案，未见落地）；logger 过滤配置持久化（#29147）；"持久日志需要新 API" 这一 zagor 的判断（#25907 至今 open）。

**F7.3 社区对"该不该用文件系统存日志"的共识与反对理由（可证实的部分）。**
- **共识面（已落地）**：PR #32973 的评审中**没有 reviewer 反对用文件系统**（nordic-krch 只说「From logging perspective it looks ok.」），且 koffes 追问的只是 FAT/exFAT 兼容性。文件系统路径被接受。
- **从代码/文件可知的反对理由**：
  1. 文件系统是**重量级依赖**——需要 VFS + FS + 挂载 + 目录创建，Kconfig 依赖 `FILE_SYSTEM`，且未挂载期间日志**直接丢弃**。
  2. **空间占用高**：格式化文本 + 4 位十进制文件名 + 每个文件 4096 字节默认粒度，对片内 flash 不经济。
  3. **没有原子性语**：FS 后端崩溃后只是尝试"再建一个文件"，没有事务/掉电原子性；`BACKEND_FS_CORRUPTED` 状态下会静默停写而不通知。
  4. **递归风险无内建防护**：flash 驱动自身的日志可能打到同一 flash 上（F4.6）。
  5. **nordic-krch 的长期方向**：未来应存自包含消息而非格式化文本，因为「take less space and be faster」——与 FCB PR 中「have you considered storing just the message? it would use much less space」是同一条技术主张的两处表述。
- **FCB 路线被放弃的可归因理由**（PR 页面未明文给出，但可核实的事实链）：评审提的 4 类必改项（过滤归属、扇区用 flash area/label 而非 ID、写对齐 `write-block-size`、rotation count 饱和）无一被作者回应，60+ 天无活动后被 stale bot 关闭。**即：不是被"否决"，而是被"耗死"。**

## 设计取舍

本节回答「业界为什么这么选，正反理由」。每条都以 F 编号引用事实。

**D1. 为什么把"过滤"放在 core 而不放在后端。**
- 正方（主线立场）：后端是**输出通道**，不是策略层。若每个后端自己过滤，则 (a) 同一条消息会被多个后端重复判定，(b) 过滤逻辑要处理"消息已被构建但我想丢弃"的浪费——因为 deferred 模式下消息在**构建前**就该被拦掉；(c) 用户需要记住 N 套后端私有过滤语法。主线的做法让 core 在构建消息前检查「是否至少有一个后端要它」（F2.6），从而**从源头省掉 mpsc_pbuf 空间与 CPU**。
- 反方（zagor 的原始直觉）：只有 flash 后端有空间/寿命约束，让全局过滤为它服务很别扭。他担心的是"前端过滤 = 一刀切"。
- **实际结论**：zagor 的担心是**基于对能力矩阵的误判**——主线当时已有 per-backend 过滤（F2.5），他想要的行为本来就支持。这是一个"提 PR 前没读透现有 API"的典型代价。
- **对我们的启示**：我们的"per-backend 按模块过滤"与主线同构，是正确方向；要额外注意的是**过滤器必须在消息构建前生效**，否则省不下 RAM 与 flash 写入量。

**D2. 为什么 `LOG_BACKEND_FS` 选了"多文件轮转 + 覆盖最老"而不是"单环形缓冲"。**
- 正方：文件系统的**用户态可读性**最好——拔 SD 卡、`fs list`/`fs download`、mcumgr 拉取即可，不需要自定义导出协议（F4.2）。文件编号 4 位十进制、顺序递增，人工判断新旧很直观。轮转粒度是"文件"而非"扇区"，无需自己实现 GC。
- 反方：FLASH 空间效率差；写放大（每次 `fs_sync`、目录项更新）；默认 4096 字节文件 + 10 个文件 = 40 KB 起跳，对片内 flash 偏大；文件系统本身是个大依赖；未挂载就**丢日志**（F4.4）。
- **对我们的启示**：我们的目标是黑匣子语义（环形覆盖保新），**FS 的"覆盖最老"选项（`LOG_BACKEND_FS_OVERWRITE=y`）在语义上完全等价**，可以直接借用它的策略语义，但不必用文件系统这个载体。

**D3. 为什么 FCB 路线在技术上更贴合 flash，却被耗死。**
- 正方（技术层面）：FCB 的语义天然就是"环形覆盖"——`fcb_append()` → 满 → `fcb_rotate()` 擦最老扇区（F3.7），无需文件系统，写放大低，掉电靠**逐条 checksum + 跳过坏条目**（F3.7）。与我们的"黑匣子"目标几乎一一对应。
- 反方（工程层面）：(a) 扇区必须连续且调用者要手填 `f_magic`/`f_sector_cnt`/`f_scratch_cnt`/`f_sectors`，集成成本高；(b) 需要自己解决"读回来"的 API（zagor 自己也把它列为开放问题，F3.2）；(c) 需要自己解决 wear 与 rotation 饱和（F3.5）。
- **为什么被耗死**：评审提的都是**可改的工程细节**（用 flash area label 而非 ID、写对齐、宏生成扇区数组），并非设计否决。作者失联 60+ 天后被 stale bot 自动关闭（F3.1/F7.3）。**结论：在 Zephyr 这类项目里，"技术正确"不等于"能进主线"，响应速度与维护者带宽同样是决定因素。**

**D4. 为什么"存格式化文本"是默认，而"存原始参数/字典"是公认的更优方向。**
- 正视原因：格式化文本**自包含、可离线直接读**，不需要构建产物配合。dictionary 模式虽省空间，但解码必须拿到同一构建的 `log_dictionary.json`（F1.11），且与 `LOG_FMT_SECTION_STRIP` 组合极易踩坑（F1.11 的 LTO 缺陷、F4.7 的 BusFault 崩溃）。对一个"现场黑匣子"来说，**离线可读性是刚需**——出问题时你未必拿得到当时的构建数据库。
- 反方理由（nordic-krch，两处提出）：自包含消息「take less space and be faster」（F4.2）、「it would use much less space」（F3.4）。但**同一段话里他自己承认代价**：`%s` 指向 RAM 时重复字符串会丢（F3.4）。
- **结论**：这是一组真实的权衡，主线最终**同时提供两条路**（Kconfig 模板的 TEXT / DICTIONARY / CUSTOM / SYST 四选一，F1.12），把选择权交给产品。**对我们的启示**：黑匣子场景建议默认文本（可离线读），把 dictionary 作为"高压缩比、但必须保留构建数据库"的可选项。

**D5. panic 路径的设计取向：先 flush 再置位，之后全部同步。**
- 主线选择：`log_panic()` 先强制初始化 → 通知所有后端 → 忙等排空缓冲 → **之后**才置 `panic_mode`，此后新日志由投递方自己持锁同步处理（F6.1）。
- 正：保证"崩溃前已缓冲的消息"不丢；之后的新消息也一定能落。
- 反：flush 是 `while (log_process() == true)` 的**忙等**（F6.1），在 panic 上下文是不可控的耗时；且它依赖后端在 panic 模式下"switch to blocking mode or halt"（F6.1 的 API 文档原话）——这是**后端的义务，不是 core 能强制的**。
- **对我们的启示**：我们的器件层 flash 抽象要求"写操作必须让出 CPU，不能在中断/panic 上下文直接写"，意味着我们**不能**照抄"panic 时同步 flush 到 flash"这条路；主线自己给出的替代路径恰好就是我们想要的——**panic 现场先存 retained RAM，复位后再转存**（F6.5 中 Nordic 官方对 coredump 的建议原话）。

**D6. 崩溃现场为什么走 coredump 子系统而不是日志后端。**
- 主线把"崩溃现场落盘"做成了**独立子系统**（`DEBUG_COREDUMP_BACKEND_FLASH_PARTITION`），而不是复用 log backend（F6.3）。理由：两者数据模型完全不同——coredump 是**一次性、大块、有头有尾、可校验、可失效**的对象（header + checksum + INVALIDATE/VERIFY/COPY 命令），而日志是**流式、持续、可丢弃**的。
- **对我们的启示**：黑匣子日志与 panic 现场快照**应当是两条独立通道**（前者环形覆盖流式，后者一次性带校验的块），而不是硬塞进同一个后端。这与我们的项目目标（"panic 现场先 RAM 暂存再复位转存"）天然吻合。

**D7. 覆盖最老 vs 丢弃最新：主线把选择权做成开关。**
- `LOG_BACKEND_FS_OVERWRITE` 默认 `y`（覆盖最老文件，F4.4）；关闭时 `-ENOSPC` + 「the log record abandonment」（F4.5）。
- 同理 mpsc_pbuf 层有 `CONFIG_LOG_MODE_OVERFLOW` 决定"溢出时覆盖还是拒绝"（F1.5）。
- **对我们的启示**：主线在**两个层级**都用同一个开关模型（DRAM 队列 / 持久存储各一个），说明"保新"不是唯一正确答案；即使我们默认保新，也应把策略做成编译期可选，便于现场调试期改成"保旧 + 报错"。

## 可借鉴 / 应避免

### 值得借鉴

**K1. 分层职责：core 管策略、后端管输出、过滤在 core 侧前移。** 主线三件套（Frontend / Core / Backends，F1.1）把"要不要这条消息"（core 过滤，F2.5/F2.6）与"消息长什么样、写到哪"（后端 `process`，F2.2）彻底分开。我们的"多后端 log 服务 + per-backend 按模块过滤"已经是同一形状；需要补的是**过滤判定必须在消息构建之前**，否则省不下队列 RAM 与 flash 写入量。

**K2. per-backend 过滤的"过滤器位图"实现可直接照抄。** 每个 source 一个 32 位 RAM 过滤器、切成 10 个 3-bit slot（slot 0 聚合 + 9 个后端），异常简洁且 O(1)（F1.9）。我们要支持的"按模块 × 按后端"正好落在同一模型里，且上限天然是 9 个后端（F2.1）。

**K3. 后端能力通过"格式选择"参数化，而不是给每个后端写死格式。** `Kconfig.template.log_format_config` 用同一份模板给每个后端生成 TEXT / SYST / DICTIONARY / CUSTOM 四选一（F1.12）。对我们的价值：RTT / 串口 / flash 三种后端可以共享同一套格式选项，避免重复 Kconfig 与重复代码。

**K4. 后端的"标准输出"应有共享助手。** `log_backend_std_get_flags()` 把一堆 `CONFIG_LOG_BACKEND_SHOW_*` 编译期选项翻译成运行期位掩码，`log_backend_std_panic/dropped` 收敛公共行为（F2.11）。我们已有多个后端，这类公共层能显著减少重复。

**K5. shell 命令用"动态补全后端名 + 模块名"的形态。** `log backend <name> enable <level> <modules...>` 配合动态补全（F2.7），运维体验好且实现成本低（`SHELL_DYNAMIC_CMD_CREATE` + 遍历 memory section）。我们的分区表 v2 是句柄式的，天然适配同样的动态补全模式。

**K6. "go / halt" 与 autostart 分离。** `LOG_BACKEND_FS_AUTOSTART`（F4.4）+ `log backend <name> go`（F2.7，内部 `log_backend_enable(..., CONFIG_LOG_MAX_LEVEL)` + `log_backend_activate()`）提供"先注册、稍后按需启动"的完整生命周期。这正对应我们"文件系统/分区就绪前不能写"的场景——**但主线 FS 后端的做法是"未挂载就丢日志"（F4.4），这一点我们应当改进而非照抄**。

**K7. coredump 的"块对象 + 校验 + 可失效 + 可导出"是 panic 现场的正确模型。** `COREDUMP_QUERY_HAS_STORED_DUMP` / `VERIFY_STORED_DUMP` / `COPY_STORED_DUMP(offset, buffer, length)` / `INVALIDATE_STORED_DUMP` / `ERASE_STORED_DUMP`（F6.4）几乎是我们"可主动拉取导出"需求的现成接口清单：查询存在性 → 校验完整性 → 分段拷贝导出 → 显式作废/擦除。建议我们的 panic 快照通道直接采用这套原语命名与语义。

**K8. 掉电 / 撕裂写的正确防线是"逐条校验和 + 跳过坏条目"，而非原子提交。** FCB 的原文：「Entries in the flash are checksummed」「FCB will skip over entries which don't have a valid checksum」（F3.7）；ZMS 也是每条 ATE 带 `crc8` + `data_crc`（F5.2）。**恢复 = 扫描 + 丢弃不可信尾部**，不需要复杂的事务机制。

**K9. "保留一个空扇区做 GC"是廉价且有效的设计。** FCB 的 `f_scratch_cnt`（F3.7）与 ZMS 的"始终保留一个空扇区、且它轮换"（F5.3）是同一个技巧：保证任何时刻都有可写空间，避免"擦除中掉电"导致完全不可写。

**K10. 擦除粒度可以小到"一次元数据写"。** ZMS 的「To erase a sector, the cycle counter of the empty ATE is incremented and a single write of the empty ATE is done.」——**一次 16 字节写就作废整个扇区**（F5.3）。若我们的 flash 抽象支持在扇区内做小粒度元数据写，这种"代际计数器"技巧能把掉电窗口压到最小。

### 应当避免

**A1. 不要在后端里做日志内容过滤（尤其是靠解析文本 token）。** PR #28892 的 token 扫描方案被 maintainer 直接否掉：「this is not needed, it should be done on logger level (logger runtime filtering)」（F3.3）。我们的后端若做同样的事，会同时踩"重复实现"与"消息已被构建才丢弃"两个坑。

**A2. 不要让持久化后端在失败时静默停写而不上报。** `log_backend_fs.c` 进入 `BACKEND_FS_CORRUPTED` 后**只是不再写，不通知任何人**，也未 deactivate（F4.5）。对一个"黑匣子"来说这是最危险的失败模式——**你以为在记，其实早就没记了**。我们必须显式上报（计数 + 回调 + 状态查询）。

**A3. 不要留下"写入持续失败导致系统挂死"的路径。** DevZone 实测：只让 flash 写失效（读仍正常）时**系统挂死**；读写都失效时反而能自我禁用存活（F4.6）。我们的 flash 抽象"写必须让出 CPU"意味着挂死风险更高（可能持锁等待）。必须设计超时 + 熔断 + 显式上报。

**A4. 不要在同一个 flash 上让驱动层日志与持久化后端互递归。** 这是 FS 后端的真实风险（F4.6），主线的规避手段是"按模块 × 按后端过滤"或 `CONFIG_FS_LOG_LEVEL_OFF=y`。我们需要在架构上就切断：**flash 驱动与分区层的日志模块，必须对所有持久化后端关闭**（比"记得配置"更可靠的是让后端在初始化时主动 `log_filter_set(persist_backend, ..., 该模块, 0)`）。

**A5. 不要依赖"格式串被剥离的镜像 + 运行期解码"做现场黑匣子。** 两条真实事故：`LOG_FMT_SECTION_STRIP` 与 `SHELL_LOG_BACKEND` 组合导致 BusFault（F4.7）；LTO 下 `database_gen.py` 生成的 `log_dictionary.json` 不完整、解码显示 `<string@…>`（F1.11）。黑匣子的数据必须**在设备侧就自包含可读**。

**A6. 不要让 shell 命令改动的配置"重启即失"。** issue #29147（F3.9）从 2020-10 挂到现在仍未解决，而它描述的正是我们的场景——"UART 后端多打日志、持久化后端少打日志"。**这个能力我们必须自己做**（把 per-backend 过滤表持久化到分区），不能指望上游。

**A7. 不要把"未就绪"实现成"静默丢数据"。** FS 后端在未挂载时 `return length`（假装成功）并丢弃数据（F4.5）。这会让上层完全无法感知。正确做法是区分"丢了多少条"并计入 dropped 统计（主线在 core 层有现成的 `log_backend_dropped` 机制，F1.6，但 FS 后端没有用它）。

**A8. 提 PR 前先把现有能力矩阵读透。** PR #28892 的核心争辩（F3.3）本可避免；提 PR 后**不要失联**——60+ 天无响应直接被 stale bot 关闭（F3.1），这是"技术上可行"的 FCB 路线最终没能进主线的直接原因。

**A9. 不要照抄 "panic 时同步 flush 到 flash"。** 主线的 `log_panic()` 用 `while (log_process() == true) {}` 忙等排空（F6.1），且依赖后端"switch to blocking mode"——这与我们"写操作必须让出 CPU、不能在中断/panic 上下文写"的约束**直接冲突**。主线自己给出的替代路径（retained RAM 暂存 + 复位后转存，F6.5）才是我们该走的。

**A10. 不要用文件系统做片内 flash 的黑匣子载体。** 40 KB 默认占用、目录项写放大、未挂载即丢、"文件丢失"只能靠"再建一个"处理（F4.4/F4.5）——对 Cortex-M4 级片内 NOR 而言性价比低。文件系统适合**外置 SPI NOR**（容量大、可插拔导出），片内应走 FCB/裸扇区风格。

### 对我们的启示（对应项目约束）

**P1. 环形覆盖保新 = "FCB 语义"，而不是"文件系统语义"。** 我们的目标（黑匣子、环形覆盖保新）与 FCB 的 `append → rotate` 模型逐字对应（F3.7），也与 `LOG_BACKEND_FS_OVERWRITE=y` 的策略语义一致（F4.4）。**建议采用 FCB 风格的裸扇区环形缓冲，只借用 FS 后端的"覆盖最老"策略语义。** 片内 NOR 与未来外置 SPI NOR 都可以用同一套扇区环形逻辑，只是扇区大小与擦除时间不同。

**P2. panic 路径：照抄 coredump 的"RAM 暂存 + 复位转存"，不要照抄 log core 的同步 flush。** 主线 coredump 后端虽然直接写 flash，但 Nordic 官方对"fatal handler 里碰 QSPI"给出的建议正是我们想要的：**把现场放进 `__noinit`/retained RAM，复位后再写 flash**（F6.5）。我们的"零状态分区表 v2 + 多后端 log 服务"可以这样落地：panic 时只往 retained RAM 里塞一块带 checksum 的紧凑快照并复位；启动早期（分区表就绪、flash 可写后）由持久化后端把它转存到黑匣子区，并用 coredump 风格的 query/cmd 原语暴露给导出通道（K7）。

**P3. flash 写必须让出 CPU → 后端必须有自己的串行化与背压。** 主线的 FS 后端用 `LOG_OUTPUT_DEFINE(..., buf, MAX_FLASH_WRITE_SIZE)` 的 256 字节对齐缓冲，并在 `LOG_BACKEND_EVT_PROCESS_THREAD_DONE` 时 `fs_sync()`（F4.5）——**"在日志线程上下文里、按块、可让出"是主线的既有形态**，与我们的约束兼容。建议：持久化后端只在自己的线程（即我们的"独立日志线程 = deferred 模式"）里写，**绝不在 `process()` 里同步等待**；队列满时按 `log_backend_dropped` 语义计数并上报（F1.6），而不是阻塞生产者。

**P4. 过滤要覆盖"驱动自身"，且要能持久化。** 结合 A4 与 A6：分区/flash 驱动模块应被持久化后端**在代码里**强制静音（而非依赖用户 Kconfig），且我们的 per-backend 过滤表应当持久化到分区，重启后自动恢复——这正是 issue #29147 挂了五年没人做的事（F3.9），是我们的差异化点。

**P5. 导出通道应当显式设计，别留给"打印全部"。** PR #28892 的取回方案只是"boot 时全部 print 出来"，作者自己也承认需要 API（F3.2）；主线后来补的是 shell `flog_*` 类命令（F3.5）与 coredump 的 COPY/VERIFY/ERASE 命令集（F6.4）。**建议我们的黑匣子后端从第一天起就提供：条目计数/游标查询、按序分段读取（offset+length）、完整性校验、显式擦除/作废。** 这直接满足"可主动拉取导出"。

**P6. 存储内容默认文本，dictionary 作为可选压缩档。** 依据 D4 与 A5：现场黑匣子必须离线可读。若未来要上 dictionary，务必保留构建产物与设备固件的版本绑定校验，否则会重演 F1.11 / F4.7。

**P7. 容量与轮转策略做成编译期可调，并把"满了"显式暴露。** 主线在两个层级都留了开关（`CONFIG_LOG_MODE_OVERFLOW` 与 `LOG_BACKEND_FS_OVERWRITE`，D7）。建议我们的黑匣子后端提供：分区/扇区数量、单条上限、轮转阈值（水位）、覆盖保新开关、以及一个"已覆盖旧数据条数"的统计量。

## 存疑与未证实

以下内容**未能从一手来源证实**，引用时请勿当作事实。

**U1. 未找到 Zephyr 官方对"日志后端不得写同一 flash"的成文约束。** 递归风险的讨论只见于 Nordic DevZone 问答与 NCS 语境（F4.6），Zephyr 官方文档与 `log_backend.h` 注释中未见明文约束。**「主线没有防递归机制」这一点是我从 `log_core.c` 源码阅读得出的结论（F6.2），属于对源码的解读而非官方声明，请谨慎对待。**

**U2. ZMS 的掉电原子性保证未在文档中显式声明。** ZMS 文档只描述了恢复扫描流程与 `zms_mount_force` 兜底，并把「Add a recovery function that can recover a storage partition if something went wrong」明确列为**未来工作**（F5.4）。**「ZMS 掉电安全」这一常见说法我未能在官方文档中找到直接依据**；crc8/data_crc 与 cycle counter 机制暗示了设计意图，但缺一句显式保证。

**U3. 「FCB 久经考验、被 settings 子系统长期使用」未核实。** 我只确认了 FCB 的 API 与实现语义（F3.7）。FCB 被 `settings` 用作后端一事来自搜索摘要中提到的 PR #6408（nvlsianpu 把 MyNewt 配置系统改造为 Zephyr settings），**我没有打开该 PR 核实其状态或后续**。若要依赖"FCB 在生产中很成熟"这一判断，需另行核实。

**U4. `log_backend_fs` 究竟在哪个版本首发，未获精确证实。** PR #32973 合并于 2021-03-22，且 `subsys/logging/log_backend_fs.c` 在 tag `v2.6.0` 上存在（已用 HTTP 200 核实）。但 **Zephyr 2.6.0 的 release notes 页面里没有提到这个后端**（我查了 `docs.zephyrproject.org/2.6.0/releases/release-notes-2.6.html` 的 Logging 段，只有 logging v2 与 UART/shell 后端更新的内容）。**「2.6.0 首发」是基于 tag 文件存在性的推断，不是 release notes 明文。**

**U5. PR #27486 与 PR #32973 的关系细节未完全核实。** 我确认 #27486 存在（m-syc 的「subsys: logging: Added File System backend log」，被 #32973 取代）以及 #32973 合并（F4.1），但**没有逐条比对两个 PR 的 commit 差异**；commit `8e05a9b` 归属 PR #32973 一事也未用一手页面逐字确认。

**U6. 「社区明确反对用文件系统存日志」——证据不足。** 我在 PR #32973 页面上**没有找到任何 reviewer 反对文件系统方案**（F7.3）。所有"反对理由"（空间、写放大、递归、无原子性）都是我**从代码与 Kconfig 事实推导出的**，而非社区原话。请勿表述为"社区反对用文件系统"。

**U7. 邮件列表原始讨论未能直接读取。** 一份搜索摘要引用了 Zephyr devel 邮件列表（"Re: Logging with a string via %s"）中关于"deferred 模式是为了支持慢后端如 flashlog"的说法；我尝试抓取该邮件列表页面时收到 HTTP 402，**未能取得原文、作者与日期**。该说法与 F1.2/F3.4 的设计动机一致，可作旁证，但**不应作为独立事实引用**。

**U8. `log_backend_std_put()` 是被移除还是重构掉的，未证实。** 我确认它在 `v2.6.0` 的 `include/logging/log_backend_std.h` 第 46 行存在、且当前 main 的同名头文件中不存在（F2.11），但**没有找到删除它的那个 commit**，因此无法断言它是被废弃还是被搬走了。

**U9. 版本发布日期与"最新版"判断来自搜索结果，非官方发布页。** 「4.4.0 于 2026-04-14 发布、LTS 线为 3.7」来自搜索摘要（引用 GitHub release tag 页与 `doc/releases/index.rst`），**我未逐一打开核对**。正式引用请以 https://github.com/zephyrproject-rtos/zephyr/releases 为准。

**U10. `LOG_BACKEND_FS_DIR` 默认值在不同版本可能不同。** main 上读到的是 `"/lfs1"`（F4.4）；搜索摘要另有一处出现 `"/lfs"`，但那是某 Nordic DevZone 用户的 `prj.conf` 内容、属用户配置而非默认值。**以 main 的 Kconfig 原文 `"/lfs1"` 为准**，目标版本较老时请复核。

**U11. 我们的"零状态分区表 v2（注册表+句柄）"与 Zephyr `flash_area` / partition 模型的对应关系未做调研。** 本报告只覆盖 Zephyr 侧，未评估我们的分区表如何满足 `fcb_init(f_area_id, ...)` 所需的"**连续**扇区数组"前提（F3.7）。**这是落地前必须补的一步。**

**U12. 「主线没有在途的持久化日志后端提案」是"未检索到"而非"确认没有"。** 我检索了 2024–2026 的 PR/issue，只找到日志后端相关的缺陷修复（F2.10、F4.7）。但本环境下 GitHub 代码/PR 搜索 API 受限（`api.github.com` 触发速率限制），**无法排除存在未被检索到的在途工作**。

---

*报告完。事实条目 F1–F7 均附一手出处 URL；设计取舍 D1–D7 与借鉴条目 K1–K10 / A1–A10 / P1–P7 均为基于 F 条目的推论，已在文中标注依据。*

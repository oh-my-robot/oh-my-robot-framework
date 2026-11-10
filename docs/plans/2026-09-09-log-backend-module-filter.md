# Log 后端按模块过滤实现计划

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为 log 服务消费编排新增"后端级 = 默认级别 + 按模块覆盖"过滤维（Zephyr filter set 同构），求值收敛为消息级位图，逐段零查表扇出。

**Architecture:** `LogBackendEntry` 增密集覆盖数组 `modLevel[OM_LOG_MAX_MODULES]`（0xFF=未覆盖→默认级）；backend.c 新增 `accept_mask(module, level) → uint8` 与 `push_mask(mask, seg, len)`，替换 `any_accepts`/`push_all(level,…)`；core.c `log_emit_args` 与 ring.c 同步现场判定两调用点随之改；管理 API `set/clear/get_module_level` 按名解析（模块惰性登记前 `NOT_FOUND`，与 `om_log_module_set_level` 同语义）。panic 路径不变（提满全出）。模块过滤属正常过滤、不占丢弃语义。

**Tech Stack:** C11、XMake（`samples/host/om_log_test` 主机测试族：无宿主框架的最小脚手架 + EXPECT 断言）、armclang/gnu-rm 交叉构建（logger_store_build 壳）。

**设计事实源：** `lib/services/docs/log_module_filter_design.md`（已提交 21ecb69）。机制细节（表形态/求值/API 语义/内存记账 64B/边界）以此为准，实现中不新增裁决。

---

## 通用命令（后续任务引用）

```sh
# host 测试（在 logger_store 工作树根执行；-P 后平台缺省=本机 mingw）
xmake f -c -P samples/host/om_log_test -m debug
xmake build -P samples/host/om_log_test
xmake run -P samples/host/om_log_test om_log_module_filter_test   # 新目标
# 回归（既有目标必须全绿）
xmake run -P samples/host/om_log_test om_log_formatter_test
xmake run -P samples/host/om_log_test om_log_filter_test
xmake run -P samples/host/om_log_test om_log_ring_test
# 真机构建（logger_store_build 壳，rm-a armclang release）
xmake f -c && xmake    # 在 logger_store_build 目录
# 格式门禁（提交前；勿用 PATH 首位 17.0.6 shim）
"D:/ProgramFiles/LLVM/bin/clang-format.exe" -i <改动 .c/.h>
```

注意：host 测试以 `OM_LOG_ASYNC=0`（同步模式，零 OSAL 桩）编译 services log 源；`-P` 配置会在工作树根产生 gitignored `.xmake/`（既有惯例，可留）。

---

### Task 1: 新测试目标骨架 + 覆盖语义用例组（红）

**Files:**
- Create: `samples/host/om_log_test/om_log_module_filter_test.c`
- Modify: `samples/host/om_log_test/xmake.lua`（新增 target，镜像 `om_log_filter_test` 的源清单）

**Step 1: 写测试骨架与用例 A**

镜像 `om_log_filter_test.c` 结构：CaptureBackend 捕获 buf+len+seg_count+panic_called、EXPECT 断言、静态 `OmLogBackend g_backend_a/b` + 捕获回调（校验 `backend == &g_backend_*`）。测试模块用 `OM_LOG_MODULE(mod_a, OM_LOG_LEVEL_DEBUG)` 等三个实例（mod_a/mod_b/mod_c）。

用例 A（默认级 + 覆盖生效 + OFF + get 生效值）：

```c
static void test_override_basic(void)
{
    /* 注册即登记：先打一条使 mod_* 入模块表（惰性——覆盖 API 依赖可解析 id） */
    OM_LOG_INFO("reg mod_a");
    OM_LOG_INFO("reg mod_b");

    /* 后端 a 默认 WARN */
    EXPECT(om_log_backend_register(&g_backend_a, OM_LOG_LEVEL_WARN) == OM_OK);
    g_cap_a.len = 0; g_cap_a.seg_count = 0;

    OM_LOG_INFO("info dropped by default");          /* WARN 默认 → 拒 */
    EXPECT(g_cap_a.seg_count == 0);
    OM_LOG_WARN("warn accepted");                     /* 默认级接受 */
    EXPECT(g_cap_a.len > 0);

    /* 覆盖放宽：mod_a → INFO（仅后端 a） */
    g_cap_a.len = 0; g_cap_a.seg_count = 0;
    EXPECT(om_log_backend_set_module_level("backend_a", "mod_a", OM_LOG_LEVEL_INFO) == OM_OK);
    OmLogLevel eff = OM_LOG_LEVEL_OFF;
    EXPECT(om_log_backend_get_module_level("backend_a", "mod_a", &eff) == OM_OK);
    EXPECT(eff == OM_LOG_LEVEL_INFO);
    EXPECT(om_log_backend_get_module_level("backend_a", "mod_b", &eff) == OM_OK);
    EXPECT(eff == OM_LOG_LEVEL_WARN);                /* 未覆盖 → 默认级 */
    OM_LOG_INFO("info accepted for mod_a now");       /* mod_a 的 INFO 过 */
    OM_LOG_WARN("warn accepted for mod_b");           /* mod_b 仍默认 WARN 接受 */
    EXPECT(g_cap_a.seg_count >= 2);

    /* 覆盖收紧 + OFF 显式拒：mod_b → OFF */
    g_cap_a.len = 0; g_cap_a.seg_count = 0;
    EXPECT(om_log_backend_set_module_level("backend_a", "mod_b", OM_LOG_LEVEL_OFF) == OM_OK);
    OM_LOG_WARN("warn from mod_b rejected");          /* OFF 覆盖 → 拒 */
    EXPECT(g_cap_a.seg_count == 0);

    /* clear 回退默认 */
    EXPECT(om_log_backend_clear_module_level("backend_a", "mod_b") == OM_OK);
    OM_LOG_WARN("warn from mod_b accepted again");
    EXPECT(g_cap_a.seg_count > 0);

    EXPECT(om_log_backend_unregister(&g_backend_a) == OM_OK);
}
```

注意 OM_LOG_* 调用出现在模块宏实例所在函数，需在调用点确认当前 `_om_log_module` 归属——三个测试模块要分开在不同测试函数调用（`OM_LOG_MODULE` 宏引用本 TU 的静态实例）。模块 a/b 的日志必须从各自模块实例走：把用例拆成按模块的测试函数或利用宏只能引用本文件单实例的现实——**测试文件用两个独立模块实例不可行（OM_LOG_MODULE 每 TU 一次）**。方案：测试模块实例直接手写结构体并传给内部？`OM_LOG_*` 宏引用 `_om_log_module`。过滤器语义验证需要"同一后端对不同模块不同门槛"，一条消息只能属一个模块。解决：**测试文件内声明多个模块 = 每测试模块一个 .c？** 过度。改用真实全链 API `om_log_log(&g_mod_a, ...)`（公开函数，模块实例手写静态结构体，`moduleId=-1` 惰性登记路径与宏一致）：

```c
static OmLogModule g_mod_a = {"mod_a", OM_LOG_LEVEL_DEBUG, -1};
static OmLogModule g_mod_b = {"mod_b", OM_LOG_LEVEL_DEBUG, -1};
static OmLogModule g_mod_c = {"mod_c", OM_LOG_LEVEL_DEBUG, -1};
/* 调用 om_log_log(&g_mod_a, OM_LOG_LEVEL_INFO, "...") */
```

（`om_log_log` 已在 log.h 公开；测试可直接用——既有 filter 测试若用宏则以宏为准，此处统一 `om_log_log`。）

**Step 2: 跑测试确认编译失败（API 未定义）**

Run: `xmake f -c -P samples/host/om_log_test -m debug && xmake build -P samples/host/om_log_test`
Expected: FAIL——`om_log_backend_set_module_level` 隐式声明/未定义（红 = 需求锚定）。

**Step 3: Commit**

```bash
git add samples/host/om_log_test/om_log_module_filter_test.c samples/host/om_log_test/xmake.lua
git commit -m "test(log): 后端按模块过滤用例组（覆盖放宽/收紧/OFF/clear/get 生效值）"
```

---

### Task 2: backend.c 覆盖表 + 管理 API + 消息级位图求值（合并原 Task 2/3）

> **执行说明（2026-09-09 修订）**：原计划 Task 2/3 拆分的时序有误——覆盖 API 只落表、不求值（push 仍按默认级）时，Task 1 用例无法变绿（覆盖生效需消费侧按 `eff_level` 裁判）。**本任务与下节"Task 3: 消息级位图求值重构"须一次实现会话内完成**：先按本节实现表+API，再按下节实现 accept_mask/push_mask 重构，跑全绿后按本节下方提交指令**合并为一个提交**（不再分两次——中间态会让特征提交带红测试）。

**Files:**
- Modify: `lib/services/src/log/backend.c`（entry 增 modLevel、register 初始化 0xFF、unregister 清除、eff/查找 helper、API 三件套）
- Modify: `lib/services/src/log/module.c`（导出模块名解析：`log_module_resolve_id`）
- Modify: `lib/services/src/log/log_internal.h`（声明 `log_module_resolve_id`）
- Modify: `lib/services/include/services/log/log.h`（公共 API 三件套声明 + 头注释过滤语义）
- Test: `samples/host/om_log_test/om_log_module_filter_test.c`

**Step 1: 写实现**

backend.c：

```c
typedef struct {
    OmLogBackend *backend;
    OmLogLevel    level;                              /* 默认级 */
    uint8_t       modLevel[OM_LOG_MAX_MODULES];       /* 覆盖：0xFF=未覆盖 → 用默认级 */
    uint8_t       used;
} LogBackendEntry;
```

- register：`memset(entry.modLevel, 0xFF, sizeof(...))` 后置 used=1（临界区内）；
- unregister：置 used=0 即可（used 门控，无需清数组；但清 16B 便宜且防陈旧——按设计稿"清除覆盖"：`memset(entry.modLevel, 0xFF, sizeof)`）；
- helper：`static OmLogLevel backend_eff_level(const LogBackendEntry *e, const OmLogModule *m)`——`m->moduleId >= 0 && m->moduleId < OM_LOG_MAX_MODULES && e->modLevel[m->moduleId] != 0xFF` → 覆盖值；否则 `e->level`；
- API（按名解析后端——复用现有 strcmp 遍历；模块经 `log_module_resolve_id(module_name)`，返回 `<0` → `OM_ERR_NOT_FOUND`）：

```c
OmRet om_log_backend_set_module_level(const char *backend_name, const char *module_name,
                                      OmLogLevel level)
{
    int id; size_t i; port_critical_key_t key;
    if (backend_name == NULL || module_name == NULL || level >= OM_LOG_LEVEL_MAX)
        return OM_ERR_INVALID_ARG;
    id = log_module_resolve_id(module_name);
    if (id < 0) return OM_ERR_NOT_FOUND;
    key = om_hw_disable_interrupt();
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++) {
        if (g_backends[i].used && strcmp(g_backends[i].backend->name, backend_name) == 0) {
            g_backends[i].modLevel[(size_t)id] = (uint8_t)level;
            om_hw_restore_interrupt(key);
            return OM_OK;
        }
    }
    om_hw_restore_interrupt(key);
    return OM_ERR_NOT_FOUND;
}
/* clear：modLevel[id] = 0xFF（查找同构）；get：*level = 覆盖命中 ? 覆盖 : entry.level */
```

module.c 导出：

```c
int log_module_resolve_id(const char *name)
{
    const OmLogModule *m = module_find(name);
    return (m != NULL && m->moduleId >= 0) ? m->moduleId : -1;
}
```

log.h（`#if OM_USE_LOG` 区，注释按设计稿语义）：

```c
OmRet om_log_backend_set_module_level(const char *backend_name, const char *module_name,
                                      OmLogLevel level);
OmRet om_log_backend_clear_module_level(const char *backend_name, const char *module_name);
OmRet om_log_backend_get_module_level(const char *backend_name, const char *module_name,
                                      OmLogLevel *level);
```

**Step 2: 补用例 B（白名单 + 多后端独立 + NOT_FOUND + 全拒零扇出断言）**——`test_whitelist_and_errors`：

```c
/* 后端 b：默认 OFF + 覆盖抬升 mod_c=INFO（白名单形态） */
om_log_backend_register(&g_backend_b, OM_LOG_LEVEL_OFF);
om_log_backend_set_module_level("backend_b", "mod_c", OM_LOG_LEVEL_INFO);
/* mod_a 任何级别都不进后端 b；mod_c INFO 进 */
OM_LOG_INFO via g_mod_a → EXPECT(g_cap_b.seg_count == 0);
OM_LOG_INFO via g_mod_c → EXPECT(g_cap_b.seg_count > 0);
/* 未登记模块按名设覆盖 → NOT_FOUND（惰性语义一致） */
EXPECT(om_log_backend_set_module_level("backend_b", "ghost_mod", OM_LOG_LEVEL_INFO) == OM_ERR_NOT_FOUND);
/* 后端名不存在 → NOT_FOUND */
EXPECT(om_log_backend_set_module_level("no_such_backend", "mod_c", OM_LOG_LEVEL_INFO) == OM_ERR_NOT_FOUND);
/* 越界 → INVALID_ARG */
EXPECT(om_log_backend_set_module_level("backend_b", "mod_c", OM_LOG_LEVEL_MAX) == OM_ERR_INVALID_ARG);
```

**Step 3: 跑测试确认全绿**

Run: `xmake build -P samples/host/om_log_test && xmake run -P samples/host/om_log_test om_log_module_filter_test`
Expected: 退出码 0（EXPECT 全过）。

**Step 4: 全绿后合并提交**（含 Task 3 重构——见任务顶部执行说明）

```bash
git add lib/services/src/log/backend.c lib/services/src/log/module.c \
        lib/services/src/log/log_internal.h lib/services/include/services/log/log.h \
        lib/services/src/log/core.c lib/services/src/log/ring.c \
        samples/host/om_log_test/om_log_module_filter_test.c
git commit -m "feat(log): 后端按模块覆盖——密集覆盖表+set/clear/get_module_level，求值收敛消息级位图（accept_mask/push_mask）"
```

---

### Task 3: 消息级位图求值重构（any_accepts/push_all → accept_mask/push_mask）

> **执行说明**：本任务内容并入 Task 2 一次实现（见 Task 2 顶部修订），不再单独提交；本节代码即实现指引。

**Files:**
- Modify: `lib/services/src/log/backend.c`（`log_backend_accept_mask` / `log_backend_push_mask`；删 `any_accepts`/`push_all` 或保留内部不用——按"删旧接口"收干净：删两函数）
- Modify: `lib/services/src/log/log_internal.h`（声明替换）
- Modify: `lib/services/src/log/core.c`（`log_emit_args` 门 + `emit_fanout` ctx=位图）
- Modify: `lib/services/src/log/ring.c:60`（现场判定 `accept_mask(...) != 0`）
- Test: 既有三目标回归 + 用例 C（mask==0 全拒时后端零段——功能断言，见 Task 2 已含？在 Task 2 的 whitelist 用例已断言 mod_a 对 backend_b 零段——补"全部后端全拒 → 无任何后端收段"用例：后端 a WARN + mod_a 覆盖 OFF，mod_a INFO 打日志 → g_cap_a 与 g_cap_b 均 0）

**Step 1: backend.c 重构**

```c
uint8_t log_backend_accept_mask(const OmLogModule *module, OmLogLevel level)
{
    uint8_t mask = 0; size_t i;
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++) {
        if (g_backends[i].used && level >= backend_eff_level(&g_backends[i], module))
            mask |= (uint8_t)(1u << i);
    }
    return mask;
}

void log_backend_push_mask(uint8_t mask, const char *seg, size_t len)
{
    size_t i;
    for (i = 0; i < OM_LOG_MAX_BACKENDS; i++) {
        if ((mask & (uint8_t)(1u << i)) != 0)
            g_backends[i].backend->push(g_backends[i].backend, seg, len);
    }
}
```

core.c：

```c
static void emit_fanout(void *ctx, const char *seg, size_t len)
{
    log_backend_push_mask((uint8_t)(uintptr_t)ctx, seg, len);
}

void log_emit_args(const OmLogMsg *msg)
{
    uint8_t mask = log_backend_accept_mask(msg->module, msg->level);
    if (mask == 0) return;                        /* 被全部后端拒绝 → 零格式化 */
    LogBufWriter w; char seg[OM_LOG_SEGMENT_SIZE];
    log_buf_writer_init(&w, emit_fanout, (void *)(uintptr_t)mask, seg, sizeof(seg));
    /* …原格式化链不变… */
}
```

ring.c:60：`bool fire = log_backend_accept_mask(msg->module, msg->level) != 0;`（注释同步）。

**Step 2: 回归 + 用例 C**

Run: 全部四个 host 目标
Expected: `om_log_formatter_test`/`om_log_filter_test`/`om_log_ring_test` 退出码 0（默认级行为零变化回归）；`om_log_module_filter_test` 0。

**Step 3: Commit**（已并入 Task 2 合并提交——见 Task 2 Step 4；本任务不单独提交）

---

### Task 4: 文档同步（README「过滤与分发」节 + backend.c 头注释）

**Files:**
- Modify: `lib/services/include/services/log/README.md`（「过滤与分发」三级 → 后端级 = 默认级 + 按模块覆盖；接口段补 API 三件套与示例一句）
- Modify: `lib/services/src/log/backend.c`（文件头 @details 更新：any_accepts/push_all 措辞 → accept_mask/push_mask + 覆盖表）

**Step 1: 按设计稿「机制/API」节改写 README 对应小节**（内容即 `log_module_filter_design.md` 的定稿文字，无新增裁决）。

**Step 2: Commit**

```bash
git add lib/services/include/services/log/README.md lib/services/src/log/backend.c
git commit -m "docs(log): README 过滤节与 backend.c 头注释同步按模块覆盖语义"
```

---

### Task 5: 全量验证 + 格式门禁 + 真机构建

**Step 1: 格式门禁（21.1.8 全路径，勿用 PATH 首位 shim）**

Run: `"D:/ProgramFiles/LLVM/bin/clang-format.exe" -i lib/services/src/log/backend.c lib/services/src/log/core.c lib/services/src/log/ring.c lib/services/src/log/module.c lib/services/src/log/log_internal.h lib/services/include/services/log/log.h samples/host/om_log_test/om_log_module_filter_test.c`
Expected: 无输出（若产生 diff，检查并重跑 host 测试——格式化不改语义）。

**Step 2: host 四目标全量**

Run: 四个 `xmake run -P samples/host/om_log_test om_log_*_test`
Expected: 全部退出码 0。

**Step 3: 真机交叉构建（logger_store_build 壳）**

Run: `cd /e/MyDocument/NUEDC/workspace/omr_develop/logger_store_build && xmake && find build -name "robot_project.elf"`
Expected: `build ok` + ELF 存在（rm-a armclang；日志口 demo 含 log 服务新代码编译链接通过）。

**Step 4: 工作树干净核对 + 推送**

```bash
git status --short   # 应无未提交（.xmake/ 与 build/ 已 ignore）
git push origin logger_store
```

---

### Task 6: 正式 sample（模块过滤真机演示）+ 真机验证（2026-09-09 增补）

> 用户裁决：正式 sample 入库（真机回归载体），UART 观测 COM17，J-Link 烧录已就绪。

**Files:**
- Create: `samples/pal/log_module_filter/main.c`（新 sample，镜像 `samples/pal/log_serial/main.c` 接线形态）
- Modify: 无仓库文件（构建接入走 logger_store_build 壳，不入库）

**Step 1: 写 sample**

结构镜像 `log_serial/main.c`：串口后端经板级日志口接线（`om_log_serial_backend_register` + `OM_INIT_DRIVER`）；演示模块过滤需要 ≥2 模块实例——每 TU 一次的 `OM_LOG_MODULE` 不够，sample 用**手写 `OmLogModule` 静态实例** + `om_log_log`（同 host 测试手法）：

```c
/* 模块实例（手写——过滤演示需多模块，OM_LOG_MODULE 宏每 TU 一次不敷用） */
static OmLogModule g_mod_key  = {"key",  OM_LOG_LEVEL_DEBUG, -1}; /* 关键进度，想留 INFO */
static OmLogModule g_mod_hb   = {"heartbeat", OM_LOG_LEVEL_DEBUG, -1}; /* 心跳噪音，想拒 */
static OmLogModule g_mod_warn = {"warn", OM_LOG_LEVEL_DEBUG, -1}; /* 常规告警 */

static OmRet filter_demo_setup(void)
{
    /* 后端已由 log_port_init（DRIVER 级）以 WARN 默认注册 → 覆盖表立白名单语义 */
    om_log_backend_set_module_level("serial", "key", OM_LOG_LEVEL_INFO);   /* 放宽：key 的 INFO 留 */
    om_log_backend_set_module_level("serial", "heartbeat", OM_LOG_LEVEL_OFF); /* 拒心跳 */
    /* warn 模块走默认 WARN */
    return OM_OK;
}
OM_INIT_APPLICATION(filter_demo_setup); /* SERVICE 后执行——此时后端已注册 */
```

业务线程轮发三模块日志（周期/档位宏同 log_serial 先例：`LOG_DEMO_PERIOD_MS` 默认 500），序列号供接收端完整性分析。文件头注释写明：观测预期（key INFO 出现、heartbeat 全部缺席、warn WARN 出现、演示了"默认档+放宽+显式拒"三种语义）。

**Step 2: 构建接入（logger_store_build 壳）**

- `logger_store_build/xmake.lua` 增 target `log_module_filter`（镜像现有 target：`add_files(path.join([[oh-my-robot]], [[samples/pal/log_module_filter/main.c]]))` + 5 规则）；preset `flash.target = "log_module_filter"`（或 `--target` 覆盖）。
- Run: `xmake f -c && xmake`（rm-a armclang）
- Expected: `build ok` + ELF。

**Step 3: 烧录 + 观测（J-Link / COM17）**

- Run: `xmake flash`（预设 jlink；按真机方法论：flash 后**独立复位**，避免自动 run 与复位波动干扰取证）
- 观测：串口 COM17 捕获输出，核对预期（Step 1 注释：key 的 INFO 在、heartbeat 任何级别缺席、warn 的 WARN 在）；取证文本留存（命名如 `cap_modfilter1.txt`）。
- 若输出与预期不符：停下报告差异（不得自行放宽断言）。

**Step 4: 提交 sample**

```bash
git add samples/pal/log_module_filter/main.c
git commit -m "feat(samples): 后端按模块过滤真机演示 sample（默认档+放宽+显式拒三语义，串口观测）"
```

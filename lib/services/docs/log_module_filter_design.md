# Log 后端按模块过滤设计（services）

> 状态：设计定稿（2026-09-09）。落定后并入 `log.h` 注释与 README「过滤与分发」节（事实源纪律），本稿为决策与演进记录。

## 定位

log 服务消费编排处新增过滤维度：**后端级 = 默认级别 + 按模块覆盖**（filter set 语义，Zephyr log filter set 同构）。消息仍按 (module, level) 在交付后端前判定一次，逐段零查表扇出。所有后端（RTT/串口/持久化）同享该能力；持久化后端写侧模块过滤 = 继承本能力（见 lib/drivers/docs/log_persist_design.md 裁决 10）。

## 机制

### 求值（消息级，交付前一次）

```
现状:  emit_args → any_accepts(msg.level) → 格式化一次 → 逐段 push_all(level, seg)
定稿:  emit_args → accept_mask(msg.module, msg.level) → 格式化一次 → 逐段 push_mask(mask, seg)
                     mask==0 → 零格式化快路径（被全部后端拒绝的模块零开销）
```

- ctx 从 level 编码升级为 mask 编码（`emit_fanout`）；段级推送纯位图位移，零查表；
- 保持"过滤一次 + 格式化一次 + 逐段扇出"管线不变式；格式化/扇出/环/panic 路径语义不变（panic 提满全出——崩溃证据保全）。

### 覆盖语义

- 每后端：`默认级`（注册时 level，语义不变）+ 覆盖表 `moduleId → level`；
- 生效级 `eff = 覆盖命中 ? 覆盖值 : 默认级`；`OFF` = 显式拒（模块对该后端全拒）；
- 组合形态：默认档 + 个别放宽；或注册 OFF + 覆盖抬升 = **白名单**（点名模块才收）；
- `moduleId < 0`（未登记/表满 -2）→ 覆盖表不可查 → 回退默认级（兜底文档化）；
- 模块过滤属正常过滤，不占丢弃语义（README 既有分类：级别过滤不属于丢弃）。

### 覆盖表（密集静态数组）

```c
/* backend.c */
typedef struct {
    OmLogBackend *backend;
    OmLogLevel    level;      /* 默认级（语义不变） */
    uint8_t       modLevel[OM_LOG_MAX_MODULES]; /* 覆盖：0xFF=未覆盖 → 用默认级 */
    uint8_t       used;
} LogBackendEntry;
```

- RAM = `OM_LOG_MAX_BACKENDS × OM_LOG_MAX_MODULES × 1B` = 64B 静态（记账）；
- 容量自动跟随既有上限宏，零新配置宏；表读写均在既有临界区内；
- unregister 清除覆盖（不残留）；register 不变（默认级入口）。

## API

```c
/* 设置后端对某模块的覆盖级别（OFF=显式拒；>= MAX 越界 INVALID_ARG；
 * 后端名/模块名未找到 NOT_FOUND；未登记模块与 om_log_module_set_level 同惰性语义） */
OmRet om_log_backend_set_module_level(const char *backend_name, const char *module_name,
                                      OmLogLevel level);
/* 清除覆盖 → 回退默认级 */
OmRet om_log_backend_clear_module_level(const char *backend_name, const char *module_name);
/* 查询当前生效级（覆盖命中返回覆盖值，否则默认级） */
OmRet om_log_backend_get_module_level(const char *backend_name, const char *module_name,
                                      OmLogLevel *level);
```

## 测试与文档

- om_log_test 新增用例组（mock 后端）：默认级接受 / 覆盖放宽 / 覆盖收紧 / 白名单（默认 OFF + 覆盖抬升）/ clear 回退默认 / 未登记 NOT_FOUND / OFF 显式拒 / 多后端覆盖独立 / mask==0 全拒（功能断言：后端未收到任何段）；
- `log.h` 头注释过滤语义更新；README「过滤与分发」节更新（后端级 = 默认级 + 按模块覆盖）。

## 边界（V1 不做）

- 编译期 per-backend 过滤（生产侧模块级已有，无需求）；
- 按域（domain）分组过滤（Zephyr domain 概念——OM 无域层）；
- 运行时模块登记表扩容联动（模块表满时覆盖能力回退默认级，见兜底）。

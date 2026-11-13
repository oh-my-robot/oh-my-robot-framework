# omcheck 工具

构建期与发布期检查工具。Python 脚本均以仓库根为工作目录运行。

| 文件 | 用途 |
| --- | --- |
| `link_guard.py` | 链接契约校验（强符号、初始化段边界） |
| `mem_report.py` | 内存占用报告 |
| `check_devices_deps.py` | 器件库依赖边界扫描 |
| `compat_lib.py` | `compat.yaml` 数据模型与校验 |
| `compat_check.py` | `compat.yaml` 命令行校验 |
| `compat_render.py` | 生成 Confluence 兼容页与 Release notes 片段 |
| `tests/test_compat.py` | 兼容性工具回归测试 |

## compat.yaml

`compat.yaml` 是发布期兼容事实源。CI 校验结构与声明一致性；Confluence 兼容矩阵和 GitHub Release 的“兼容性”一节由它生成，不手工维护第二份版本表。

### 字段

| 字段 | 必有 | 说明 |
| --- | --- | --- |
| `schema` | 是 | 当前为 `1` |
| `framework` | 是 | 框架版本，SemVer，不带 `v` |
| `devices` | 否 | 本次验证的器件库：`version`（SemVer）或 `ref`（commit）至少一项，`framework` 为支持的框架范围 |
| `app` | 否 | 应用方声明的 App 版本与框架范围；框架仓默认不声明 |
| `contracts` | 否 | 非身份版本轴，键为轴名，值为整数或 `major.minor`；未落地的轴不填 |
| `builds` | 是 | 已验证组合列表；每项含 `board`、`os`、`toolchain`、`status`、`evidence`，可选 `chip`、`toolchain_version` |

`status` 取值：

- `verified`：已通过构建、链接契约与相应验证；
- `experimental`：可构建，但不纳入发布验证；
- `excluded`：已知不可用或被排除。

版本范围写法为比较项合取，例如 `>=0.2 <0.3` 或 `>=0.2.0, <0.3.0`。

### 校验

```sh
python3 tools/omcheck/compat_check.py --compat tools/omcheck/compat.yaml
```

发布准备时额外校验 tag 与框架版本一致：

```sh
python3 tools/omcheck/compat_check.py --compat tools/omcheck/compat.yaml --tag v0.2.0
```

CI 同时传入器件库 pin，要求 `devices.ref`（commit）或 `devices.version`（tag）与 `.github/workflows/ci.yml` 的 `DEVICES_REF` 一致：

```sh
python3 tools/omcheck/compat_check.py --compat tools/omcheck/compat.yaml \
    --devices-ref "$DEVICES_REF"
```

### 生成发布视图

```sh
# Confluence 子页（正文 Markdown）
python3 tools/omcheck/compat_render.py --compat tools/omcheck/compat.yaml \
    --format confluence --tag v0.2.0 --out /tmp/compat-page.md

# Release notes 的「✅ 兼容性」一节
python3 tools/omcheck/compat_render.py --compat tools/omcheck/compat.yaml \
    --format release-notes --tag v0.2.0 --out /tmp/compat-release.md
```

`--commit` 未指定时，渲染器从 tag 解析短 hash；`--date` 未指定时使用当天日期。

## 扩展

新增版本轴时：

1. 在 `compat.yaml` 中补充条目（契约类轴加入 `contracts`，独立发布物新增顶层键）；
2. 在 `compat_lib.py` 中补充字段校验与一致性检查；
3. 在 `compat_render.py` 中补充展示；
4. 在 `tests/test_compat.py` 中补充回归用例。

契约轴出现公开头文件后，校验器应读取头文件中的契约版本并与 `contracts` 比较；轴未落地前不预置映射。

兼容策略不进入运行时或 core；这里只处理构建期与发布期数据。

# ADR-0030：版本轴与兼容策略

- 状态：已决策（2026-11-13，审核通过）
- 日期：2026-11-13
- 关联：OMR-29 / OMR-30 / OMR-28；ADR-0017 (project_config_layering)、ADR-0021 (boot_multi_strategy_skeleton)、ADR-0029 (driver_framework_device_library_boundary)；`docs/process/git_version_release_spec.md`；`docs/boot_ota/multi_strategy_boot_design.md`

## 背景 (Context)

OMR 的多个产物正在以不同节奏演进：

- 框架用 git tag 表达 SemVer，构建期注入 `FRAMEWORK_VERSION`，运行期经 `omVersion` 暴露；
- 器件库已独立为 `oh-my-robot-devices`，使用自己的 SemVer，与框架不锁步；
- App 由用户开发，当前没有框架级的 App 版本约定；
- Bootloader / 镜像格式 / OTA 尚未进入 integration，但设计已明确“镜像头契约是两个 binary 间的 ABI”；
- partition / bootcfg / appcfg / boardcfg 当前主要是编译期表或头文件，没有序列化版本字段。

问题不是“要不要版本号”，而是**哪些版本必须互相对上、由谁负责、什么时候升版**。如果只有一个框架版本，无法表达 App、器件库、boot 契约、配置 schema 的独立演进；如果一次性给所有未来产物建轴，又会在没有实现和消费者时固化规则。

此外，board/app 通常由用户开发，Boot/OTA 应是通用机制：核心逻辑由框架提供，用户通过扩展字段、协议命令组和钩子接入，二者不能绑死在同一根版本号上。

## 考虑过的方案 (Options)

### 方案 A：只保留框架 SemVer，所有产物共用

框架版本同时代表 App、bootloader、镜像格式和配置 schema。

否决：无法表达独立演进；OTA 预检无法判断镜像/配置/boot 契约；用户 App 版本会被框架版本淹没。

### 方案 B：现在为所有未来产物建立细粒度轴

立即为 bootloader、image format、boot meta、partition、bootcfg、config schema、protocol、board revision、SVN 分别定义版本号、升版规则和兼容窗口。

否决：其中多数在 v0.2.0 没有实现、没有消费者、没有测试面；规则会先于事实固化并迅速腐化。这是过度设计。

### 方案 C：最小活跃轴 + Boot/OTA 核心契约 + 演进路线

只为当前真实存在的产物建轴；Boot/OTA 作为一根核心契约轴，内部通过核心字段与用户扩展点解耦；未来产物只记录触发条件和 owner，等落地时再定义细节。

采纳。

### 方案 D：v0.2.0 实现运行时版本注册表

在 core 提供 `om_version_register` 一类的运行时注册表，供诊断/OTA 预检读取各轴版本。

暂不采纳：当前没有运行时消费者；静态库保活、API 面和自注册机制都会增加复杂度。先采用 `compat.yaml` 的构建/发布期数据注册，运行时注册表列入演进路线。

## 最终决策 (Decision)

### 1. 两类版本形态

- **身份版本**：SemVer，回答“这是哪个发布”，用于 framework、devices、app、未来 bootloader。
- **契约版本**：整数或 `major.minor`，回答“两边能不能互操作”，用于 Boot/OTA 核心契约、未来的数据 schema 与协议。

### 2. v0.2.0 的实际轴

| 轴 | Owner | 存储 | 版本形态 | 兼容规则 |
|---|---|---|---|---|
| Framework (OMR) | 框架维护者 | git tag；构建期注入 `FRAMEWORK_VERSION` | SemVer | 框架公开 API/源码兼容；pre-1.0 minor 可含不兼容变更，但必须给迁移说明 |
| Devices library | 器件仓库 | 器件仓库 git tag | SemVer | 独立发布；`compat.yaml` 声明支持的 framework 范围；不锁步、不承诺跨配置 ABI |
| App / firmware | 应用方 | 应用自持（元数据或构建宏） | SemVer | 框架不注入、不强制；需要时在 `compat.yaml` 记录 App 版本与 framework 范围 |

`FRAMEWORK_VERSION` / 运行期 `omVersion` 只属于 Framework 轴，不代表 App。`xmake/modules/oh-my-robot.lua` 中的 `version` 字段仅为信息展示，事实源是 git tag，不用于兼容判断。

### 3. Boot/OTA：一根核心契约轴 + 用户扩展

Boot/OTA 未来作为**一根**核心契约轴，不拆成 bootloader/image/meta/protocol 多根轴。核心契约版本为整数，覆盖：

- boot 状态机语义；
- 槽抽象与启动决策；
- 镜像头核心字段；
- boot meta 核心字段；
- confirm ABI；
- 协议核心命令组。

**职责边界**：

| 归属 | 内容 |
|---|---|
| Core（框架） | 状态机、槽抽象、镜像头核心、meta 核心、confirm ABI、协议核心命令组 |
| Board（用户/板级） | 槽→介质/基址映射、Flash ops、remap、板级数据；通过 ops 表/钩子接入 |
| App（用户） | App 版本、自检/确认策略、用户自定义镜像字段、用户协议命令组 |

**扩展与版本边界**：

- 核心契约只约束核心字段、核心 ABI 和核心状态机语义；board/app 扩展有自己的命名空间与版本，由扩展 owner 维护，不进入核心兼容判断。
- 镜像头/meta 保留 reserved 字段与扩展区；协议保留用户命令组命名空间；板级通过 ops 表/钩子接入。
- 不使用 weak 符号作为扩展点；沿用显式注册/ops 表。
- Core 遇到未知扩展字段/命令必须**显式忽略或优雅拒绝**，不得误读；reserved 字段写入时置零。

**兼容规则（等实现落地时启用）**：

- Core reader 接受声明的结构版本范围；未知或更新的核心版本必须优雅拒绝。
- 数据/契约 reader 支持当前 + 上一版 schema；writer 只写当前版；迁移路径显式文档化。
- 旧 bootloader 遇新镜像格式、旧 App 遇新 meta 布局，都必须能拒绝而不是误读。

### 4. 演进路线（现在只记录触发条件与 owner，不写规则）

| 演进项 | 触发条件 | Owner |
|---|---|---|
| Bootloader 二进制 SemVer | Bootloader 实现启动 | Boot owner |
| Partition / bootcfg schema | boot meta / bootcfg 以序列化布局落地 | 存储 / boot owner |
| Config schema | appcfg/boardcfg 需要机器校验，或运行时配置存储落地 | 配置 owner |
| Protocol wire version | 通信 / OTA / 后门协议落地 | services/comm owner |
| Board revision | 同一板卡出现多个硬件 revision | 板级 / 硬件 owner |
| Security version (SVN) | secure boot / 防回滚落地 | security / boot owner |
| 运行时版本注册表 | 诊断或 OTA 预检需要设备端读取版本 | kernel / tools owner |

这些轴在触发时再补 ADR 或设计文档；本 ADR 不预写它们的字段和升版规则。

### 5. 不建轴的模块

以下模块随 Framework SemVer，不单独发布兼容承诺：

- kernel、osal、sync、async、ipc；
- drivers 框架、services 内部实现；
- systems、samples、tools；
- `platform/`（当前随框架发布；未来若抽取 board/vendor pack，再单独立轴）。

器件库是唯一当前独立发布的代码库；其边界见 ADR-0029。

### 6. 兼容与支持窗口

**代码类轴（framework / devices）**

- pre-1.0：当前 minor 完整支持；上一 minor 只保证升级迁移路径，不承诺缺陷修复回补。
- 1.0 之后：最新 minor + 上一 minor 完整支持。
- “支持”= 安全/关键修复、兼容矩阵验证、迁移说明；不包含功能回补。
- v0.2.0 不承诺跨 board/os/toolchain 的预编译 ABI；框架与器件库必须使用同一构建配置。

**数据/契约类轴（Boot/OTA 及未来 schema）**

- writer 只写当前版；reader 支持当前 + 上一版；
- 未知或更新版本优雅拒绝；
- 破坏性布局变更必须升契约版本，并提供迁移路径。

**Protocol（未来）**

- major 不兼容，minor 向后兼容；握手协商最高共同版本。

**Board revision（未来）**

- 只表达硬件事实与软件支持范围；暂不进入镜像头，后续在 board 数据 / `compat.yaml` 兼容矩阵中表达。

**SVN（未来）**

- 单调递增，只升不降；bootloader 拒绝低于已记录下限的镜像；不设支持窗口。

### 7. `compat.yaml`：可注册的版本数据

`compat.yaml` 是构建/发布期的兼容事实源，由 OMR-28 设计 schema、CI 校验并生成 Confluence 兼容矩阵与 Release notes。

- 轴以数据键注册；新增轴只扩展 YAML 与校验器，core 不承载策略。
- 示意（非最终 schema）：

```yaml
schema: 1
framework: "0.2.0"
devices:
  version: "0.1.0"
  framework: ">=0.2 <0.3"
app:
  version: "1.4.2"
  framework: ">=0.2 <0.3"
build:
  board: "rm-a-board"
  os: "freertos"
  toolchain: "gnu-rm"
contracts: {} # boot/partition/config/protocol 轴落地后填入
```

运行时版本注册表不在 v0.2.0 实现；它列入演进路线，等诊断/OTA 预检出现真实消费者时再做。最小形态应是 core 只读表，不承载比较或策略。

### 8. 破坏性变更判定

- **Framework**：删除/重命名公开 API 或头文件、改函数签名、改枚举值、改语义、改构建目标/初始化级别、删除板级/工具链支持。pre-1.0 的 minor 可包含此类变更，但必须写迁移说明；patch 不包含。
- **Devices**：器件公开 API 破坏、删除器件支持、改变所需 framework 范围；按器件库 SemVer 处理。
- **Boot/OTA core contract**：任何核心字段、布局、ABI、状态机语义变化；升核心契约版本。board/app 扩展的变化由扩展 owner 自行升版，不触发核心契约升版。
- **未来数据/协议轴**：在对应轴落地时定义；本 ADR 只要求“破坏性变更必须显式升版并提供迁移路径”。

## 影响 (Consequences)

- **正面**：
  - v0.2.0 只需维护 Framework / Devices / App 三根实际轴，避免为未实现产物预写规则；
  - Boot/OTA 以“核心契约 + board/app 扩展”解耦，用户自定义字段、协议命令组和板级钩子不绑死核心版本；
  - `compat.yaml` 以数据方式注册轴，OMR-28 可以直接落地，不用改 core 策略；
  - 运行时版本注册表暂不引入，避免无消费者的 API 和静态库保活复杂度。
- **代价与风险**：
  - pre-1.0 仍允许 minor 破坏，用户必须阅读迁移说明；
  - 未来轴在触发时需要补 ADR/设计，不能指望本 ADR 预答案；
  - `compat.yaml` schema 必须可扩展，否则新增轴会再次改动基础设施；
  - 支持窗口需要发布流程纪律，尤其是上一 minor 只保证迁移路径的边界。
- **后续**：
  - OMR-28 按本 ADR 设计 `compat.yaml`、CI 校验与兼容矩阵生成；
  - Boot/OTA 落地时，由对应 Epic 定义核心契约的字段、结构版本和扩展点细节；
  - 运行时版本注册表、bootloader 版本、partition/bootcfg、config schema、protocol、board revision、SVN 按触发条件逐项启用。

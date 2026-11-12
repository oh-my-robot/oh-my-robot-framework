# ADR-0013：框架接管 main —— 弱符号默认入口 + 逃生通道

- 状态：已实施（init 子系统 Phase 2 收尾；rm-a/rm-c-board，gnu-rm + armclang 验证通过）
- 日期：2026-08-15
- 参考：Zephyr `kernel/main.c`（weak main）、Arduino `main.cpp`、ESP-IDF `app_main`、Zephyr `CONFIG_APP_LINK_WITH_MAIN`

## 背景 (Context)

init 子系统（ADR-0010）落地后，`om_system_startup()` 已内含完整启动编排（调度器前 `EARLIEST/BOARD/DRIVER` → init 线程跑 `SERVICE..LATE` → `osal_kernel_start()`），app 的 `main` 退化为一行样板：

```c
int main(void)
{
    om_system_startup();
    while (1)
    {
    }
}
```

成熟框架（Zephyr / Arduino / ESP-IDF）的共识是：**框架提供 main，用户不写**，用户开发走框架的注册/入口机制（`SYS_INIT` / `setup()+loop()` / `app_main`）。OMR 的目标形态应是：用户/测试例程仅经 `OM_INIT_LEVEL_*` 分散加载开发，完全不碰 `main`。

## 考虑过的方案 (Options)

- **强 main 框架提供（彻底接管）**：用户一律不得写 `main`，否则链接重复符号报错。问题：host 测试（宿主入口需要 argv/退出码/stdio 初始化）、bootloader（不启调度器）、第三方库化嵌入（宿主工程自带 main）都需要自定义入口，硬性剥夺不现实；Zephyr 也未这么做（保留 `CONFIG_APP_LINK_WITH_MAIN`）。
- **弱 main 框架提供 + 用户强 main 覆盖（采纳，Zephyr 同款）**：框架默认提供 main，用户定义强 main 自动替换；另加构建开关整体关闭注入，双保险覆盖"双弱符号共存"死角。
- **保持用户写 main（现状，否决）**：样板一行虽小，但"用户必须知道启动编排函数名并显式调用"违背分散加载哲学（模块自注册、启动顺序链接期解析，入口也应如此）；与 Zephyr 等主流方向不一致。

## 最终决策 (Decision)

- **新增 `lib/source/core/om_main.c`**（kernel 层，init 子系统）：`OM_WEAK int main(int argc, char *argv[]) { om_system_startup(); while (1) {} }`——框架默认入口，职责仅"默认接线"，零平台/板级/业务逻辑（那些一律经 `OM_INIT_LEVEL_*` 分散加载）。**签名带参是 armclang 必需**：armclang 会给定义无参 `main` 的 TU 自动生成强符号 `__ARM_use_no_argv`（C 库 argv 检测），框架弱 main 与用户强 main 并存时会重复定义（L6123E）；带参签名抑制该生成，参数被忽略。
- **注入机制**：经 `oh_my_robot.selfreg` 规则直编进所有 binary（与自注册源同机制，规避静态库按需抽取）；从 `tar_awcore` 剔除（`remove_files`）——main 是 binary 级入口，不进静态归档（归档消费者/宿主工程自带入口）。
- **构建开关**：新增 `om_framework_main` 选项（默认 on，`build/config/options.lua`）；off 时 selfreg 不注入 `om_main.c`——宿主工程自带 main / 双弱符号共存规避，对应 Zephyr `CONFIG_APP_LINK_WITH_MAIN` 的反向语义。
- **逃生通道分层**（weak main 只承担 L0→L1 的切换；L1/L2 的实质能力来自公开 API）：
  - L0 默认：用户不写 main，业务经 `OM_INIT_APPLICATION` 等注册（建业务线程等）；
  - L1 自定义启动：用户写强 main，内部调公开 API `om_system_startup()` 组装——**该函数保持公开、不内联**；
  - L2 完全接管：用户写强 main 不调框架启动，只链模块（host / bootloader / 库化嵌入）。
- **样例迁移**：`samples/<模块>` 全部删除 `main`，建线程逻辑平移进 `static OmRet xxx_app_setup(void)` + `OM_INIT_APPLICATION(xxx_app_setup)`（board 自举由 `BOARD` 级 initcall 自动完成，删除显式 `om_board_init()`/`om_core_init()` 调用）；`samples/host/` 保留各自 main——宿主入口归宿主，天然验证 L2 逃生通道。
- **weak 使用边界**：框架侧提供弱符号默认实现（main）是"框架定义的扩展点"，方向由框架给出、用户强符号覆盖——与应用层自行用 weak 承载业务逻辑（CLAUDE.md 约束，静态库抽取脆弱）性质不同。

## 影响 (Consequences)

- **正面**：用户/测试例程零样板——不写 main、不需要记得 `om_system_startup` 函数名；启动入口唯一化；与 Zephyr/Arduino 生态对齐；host 样例不改一行仍正常构建运行，证明逃生通道有效。
- **约束**：
  - 用户误写 main 不会报错（强覆盖弱）——这是特性（逃生通道）而非缺陷，文档写明；
  - `om_system_startup()` 保持公开 API，禁止内联/删除（L1 依赖）；
  - `samples/<模块>` 的 `main.c` 仍保留文件名与业务代码，仅移除 `main` 符号；宿主样例的 main 不受影响。
- **特例清单**（需要用户旁路 main 的场景，均走 L1/L2 或构建开关）：不启调度器（bootloader/裸机诊断）、启动失败分支/自定义序列、宿主 OS 入口（host/单元测试）、宿主工程嵌入（库化消费）。
- **后续演进**（不在本 ADR 实施范围，作为 init 子系统路线指引）：
  1. 启动步骤分解 API（`om_startup_early/thread/scheduler`，参考 RT-Thread `rtthread_startup` 分步）——自定义序列只需重排步骤，不必重写启动；
  2. 失败挂钩 `om_fatal_error(cause)`（weak，Zephyr `k_sys_fatal_error_handler` 同款）——initcall / 调度器启动失败进入可挂钩错误设施，替换当前 `(void)` 丢弃返回值；
  3. bare（无 RTOS）osal 端口——bootloader/引导段可"基于 OMR"构建（`EARLIEST..DRIVER` 无调度器运行，参考 Zephyr `CONFIG_MULTITHREADING=n` 与 MCUboot 裸机目标）。

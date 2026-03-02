# Git 发布与版本规范

## 1. 版本号规则

版本号格式：`v<major>.<minor>.<patch>[-suffix]`

1. `major`：不兼容架构变更。
2. `minor`：向下兼容的新功能。
3. `patch`：向下兼容的缺陷修复。
4. 当前孵化阶段默认使用 `v0.x.x`。

## 2. Tag 权限与保护

1. 仅管理员可在 `main` 打发布标签。
2. 所有 `v*` 标签必须启用保护，禁止移动与删除。

## 3. 打标签触发时机

1. 里程碑功能从 `integration` 合入 `main` 后打新次版本。
2. 紧急 `hotfix` 合入后打修订版本。

## 4. 固件版本注入要求

1. 版本号由构建系统自动注入，禁止手写硬编码版本宏。
2. 输出格式：`<tag-or-v0.0.0>+g<sha>[-dirty]`。
3. 无 Git 元信息时回退：`v0.0.0+gunknown-nogit`。
4. 应用启动日志必须打印 `FRAMEWORK_VERSION`。

## 5. Release 归档流程

1. 在 GitHub 创建对应 Tag 的 Release。
2. 生成并校对 release notes。
3. 附加标准测试固件产物（如 `.bin/.hex/.elf`）。
4. Release 内容必须可追溯到具体 Tag 与提交。

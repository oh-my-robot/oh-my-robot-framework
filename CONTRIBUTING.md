# Contributing

## 项目管理

Jira 工作流、版本规划、Epic / Issue 规则、DoR / DoD、Backlog / Board / Roadmap 见 Confluence：

- [OM 项目管理原则](https://bamboolab.atlassian.net/wiki/spaces/OMR/pages/589827)
- [OM 项目管理速查](https://bamboolab.atlassian.net/wiki/spaces/OMR/pages/622593)

## 工程流程

与代码、构建、发布直接相关的流程保留在仓库：

- [分支、提交、PR](docs/process/git_collaboration_spec.md)
- [Tag、Release、产物归档](docs/process/git_version_release_spec.md)
- [ADR、过程文档、版本归档](docs/process/document_governance_spec.md)

> Jira key 仅用于变更追溯；构建、测试和运行不依赖 Jira。项目管理规则以 Confluence `OM 项目管理原则` 为准。

## 提交前

- 代码风格遵循 `.clang-format` / `.clang-tidy`
- 分支、提交、PR 包含 Jira key（仅用于追溯）
- PR 合并前通过 CI、host 测试和目标板验证

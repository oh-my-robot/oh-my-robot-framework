# Internal 文档流转规范

本目录用于管理开发过程文档，不承载 API 手册与参考手册。

## 目录约定
- 进行中议题：`docs/internal/active/issue_[编号]_[slug]/`
- 版本归档：`docs/internal/archive/[版本号]_features/`

## 触发规则
- 需求讨论或 Bug 推演：写入对应 `issue_*` 沙盒目录。
- 形成稳定架构决策：提纯为 `docs/adr/*.md`。
- 收到“执行版本归档，当前版本号：vX.X.X”：将已完成 issue 从 `active` 迁移到 `archive`。

## 命名规范
- 统一使用小写 `snake_case`。
- 历史未绑定 Issue 的文档可临时归档到 `issue_000_*`。

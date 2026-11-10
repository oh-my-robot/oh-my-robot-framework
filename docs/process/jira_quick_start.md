# OM 项目管理速查

> 详细规范：[project_management_principles.md](project_management_principles.md)
> 当前工作主线、状态、字段、版本：在 Jira `OMR` 中查看，仓库不复制。

## 一分钟模型

```
工作主线 → Epic → Story / Task / Bug → Subtask
```

- 工作主线：长期分组
- Epic：有边界交付
- Issue：可执行任务
- 模块：代码位置
- 版本：发布节点

## 去哪里看

| 内容 | 位置 |
|---|---|
| 当前工作主线 | Jira `工作主线` 字段选项 |
| 状态流转 | 看板列 |
| 字段与选项 | 项目设置 → 工作类型 → 字段 |
| 版本 | Releases 页面 |
| 活跃 Epic | 看板 / Epic 列表 |

## 新人 10 分钟

1. 读规范第 1–5 节。
2. 打开 Jira `OMR`，看当前工作主线、看板列、字段。
3. 获取权限，选一个工作主线，认领一个 `good-first-issue`。
4. 建 Issue，填工作主线 / 模块 / 版本。
5. 按规范第 10 节命名分支和 PR。
6. 与 Owner 过一遍 DoR / DoD。

## 开始前 / 完成时

按规范第 4 节的 DoR / DoD 检查。

# internal archive 说明

当收到版本归档指令时，使用 `git mv` 迁移已完成 issue 目录。

## 示例脚本
```bash
git mv docs/internal/active/issue_015_motor_pid docs/internal/archive/v0.3.0_features/
```

## 建议
- 每个 issue 目录在归档前应存在状态文件（例如 `issue_status_merged.md`）。
- `active/` 仅保留正在进行中的 issue。

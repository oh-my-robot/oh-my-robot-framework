--- @file oh_my_robot/xmake/modules/binary_rules.lua
--- @brief binary 目标标准规则集（单一事实源）
--- @details 顶层 binary 目标（应用/示例）须启用的规则短名集合，按目标 add_rules 顺序：
--- context（构建上下文与工具链决策）、board_assets（板覆盖源与公共头）、image_convert（镜像生成）、
--- project_cfg（工程配置注入）、selfreg（自注册源保活 + 框架弱 main 注入）。
--- 本文件为规则集唯一事实源：init_workspace 生成的壳据此输出 add_rules，
--- CI 回归门禁据此断言生成壳完整性；构建手册示例的规则表述与之保持一致。
--- 注意：列表行格式（行首 4 空格 + "短名",）被 init_workspace 与 CI 门禁按模式解析，增删时勿改行格式。
return {
    "context",
    "board_assets",
    "image_convert",
    "project_cfg",
    "selfreg",
}

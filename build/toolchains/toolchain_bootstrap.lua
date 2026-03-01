--- @file oh_my_robot/build/toolchains/toolchain_bootstrap.lua
--- @brief 工具链引导修正
--- @details 在构建前统一补全工具链配置，保证首轮构建可用。
local build_root = path.join(os.scriptdir(), "..")
local config_module = import("toolchains.toolchain_config", {rootdir = build_root})

--- 执行工具链补全（可在描述域调用）
---@return table context 工具链上下文
function ensure_toolchain_ready()
    local context = config_module.resolve_toolchain_context()
    return config_module.apply_defaults(context)
end

return {
    ensure_toolchain_ready = ensure_toolchain_ready,
}

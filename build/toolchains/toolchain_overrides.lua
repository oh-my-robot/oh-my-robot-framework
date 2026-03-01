--- @file oh_my_robot/build/toolchains/toolchain_overrides.lua
--- @brief 工具链差异封装
--- @details 聚合各工具链的差异化处理逻辑。

--- 解析显式 toolset_as 配置
---@param toolchain_raw string|nil 原始 toolchain 字符串
---@return string|nil toolset_as
local function parse_explicit_toolset_as(toolchain_raw)
    if not toolchain_raw or toolchain_raw == "" then
        return nil
    end
    return toolchain_raw:match("toolset_as=([^,%]]+)")
end

--- 追加默认 toolset_as（保持原始参数不丢失）
---@param toolchain_raw string|nil 原始 toolchain 字符串
---@return string toolchain_raw
local function append_default_toolset_as(toolchain_raw)
    if not toolchain_raw or toolchain_raw == "" then
        return "armclang[toolset_as=armclang]"
    end
    if not toolchain_raw:find("%[", 1, true) then
        return toolchain_raw .. "[toolset_as=armclang]"
    end
    if toolchain_raw:match("%[%s*%]$") then
        return toolchain_raw:gsub("%[%s*%]$", "[toolset_as=armclang]")
    end
    if toolchain_raw:sub(-1) == "]" then
        return toolchain_raw:sub(1, -2) .. ",toolset_as=armclang]"
    end
    raise("invalid toolchain expression: " .. tostring(toolchain_raw))
end

--- 补全并校验 ArmClang 的 toolset_as
---@param toolchain_raw string 原始 toolchain 字符串
---@param sdkdir string|nil SDK 根目录
---@param bindir string|nil 工具链 bin 目录
---@return string toolchain_raw 补全后的 toolchain 字符串
local function ensure_armclang_toolset(toolchain_raw, sdkdir, bindir)
    local explicit_toolset_as = parse_explicit_toolset_as(toolchain_raw)
    if explicit_toolset_as then
        if explicit_toolset_as ~= "armclang" then
            raise(
                "armclang toolset_as unsupported: " .. explicit_toolset_as ..
                ". default chain only supports armclang integrated assembler (toolset_as=armclang)"
            )
        end
        return toolchain_raw
    end
    return append_default_toolset_as(toolchain_raw)
end

--- ArmClang 特殊处理入口
---@param selection table 选中信息
---@param paths table 路径信息
---@return string toolchain_raw
local function adjust_armclang(selection, paths)
    return ensure_armclang_toolset(selection.raw, paths.sdkdir, paths.bindir)
end

local toolchain_adjusters = {
    armclang = adjust_armclang,
}

--- 应用工具链差异化处理
---@param selection table 选中信息
---@param paths table 路径信息
---@return string toolchain_raw
function apply_toolchain_overrides(selection, paths)
    local adjuster = toolchain_adjusters[selection.name]
    if adjuster then
        return adjuster(selection, paths)
    end
    return selection.raw
end

return {
    apply_toolchain_overrides = apply_toolchain_overrides,
}

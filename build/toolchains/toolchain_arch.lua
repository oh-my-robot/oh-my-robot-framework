--- @file oh_my_robot/build/toolchains/toolchain_arch.lua
--- @brief 架构参数映射（traits -> flags）
--- @details 由工具链决定接收哪些架构参数与注入阶段。

--- 追加单个参数
---@param flags table 目标参数列表
---@param flag string|nil 参数
---@return nil
local function append_flag(flags, flag)
    if flag and flag ~= "" then
        flags[#flags + 1] = flag
    end
end

--- 确保字符串有效
---@param value any 值
---@param label string 标签
---@return string value 规范化后的值
local function ensure_string(value, label)
    if type(value) ~= "string" or value == "" then
        raise(label .. " not set")
    end
    return value
end

--- 校验架构 traits
---@param traits table|nil 架构 traits
---@return nil
function validate_arch_traits(traits)
    if type(traits) ~= "table" then
        raise("arch traits invalid: not a table")
    end
    ensure_string(traits.cpu, "arch traits cpu")
end

--- 构建通用编译参数
---@param traits table 架构 traits
---@return string[] flags 参数列表
local function build_common_flags(traits)
    local flags = {}
    append_flag(flags, "-mcpu=" .. ensure_string(traits.cpu, "arch traits cpu"))
    if traits.thumb == true then
        append_flag(flags, "-mthumb")
    end
    if traits.fpu and traits.fpu ~= "" then
        append_flag(flags, "-mfpu=" .. traits.fpu)
    end
    if traits.float_abi and traits.float_abi ~= "" then
        append_flag(flags, "-mfloat-abi=" .. traits.float_abi)
    end
    return flags
end

--- 构建汇编参数
---@param traits table 架构 traits
---@return string[] flags 参数列表
local function build_asm_flags(traits)
    local flags = {}
    append_flag(flags, "-mcpu=" .. ensure_string(traits.cpu, "arch traits cpu"))
    if traits.thumb == true then
        append_flag(flags, "-mthumb")
    end
    return flags
end

--- 判断是否注入架构 ldflags
---@param entry table 工具链条目
---@return boolean include_ldflags
local function should_include_ldflags(entry)
    if entry.linker_accepts_arch_flags == nil then
        return true
    end
    return entry.linker_accepts_arch_flags
end

--- GNU-RM 映射策略
---@param traits table 架构 traits
---@param entry table 工具链条目
---@return table flags
local function map_gnu_rm(traits, entry)
    local include_ldflags = should_include_ldflags(entry)
    return {
        cflags = build_common_flags(traits),
        cxflags = build_common_flags(traits),
        asflags = build_asm_flags(traits),
        ldflags = include_ldflags and build_common_flags(traits) or {},
    }
end

--- ArmClang 映射策略
---@param traits table 架构 traits
---@param entry table 工具链条目
---@return table flags
local function map_armclang(traits, entry)
    local include_ldflags = should_include_ldflags(entry)
    return {
        cflags = build_common_flags(traits),
        cxflags = build_common_flags(traits),
        asflags = build_asm_flags(traits),
        ldflags = include_ldflags and build_common_flags(traits) or {},
    }
end

local toolchain_mappers = {
    ["gnu-rm"] = map_gnu_rm,
    ["armclang"] = map_armclang,
}

--- 解析工具链条目
---@param toolchain_config ToolchainsConfig 工具链配置
---@param toolchain_name string 工具链名称
---@return table entry 工具链条目
local function resolve_toolchain_entry(toolchain_config, toolchain_name)
    local entry = toolchain_config.toolchains
        and toolchain_config.toolchains[toolchain_name]
        or nil
    if not entry then
        raise("toolchain not supported: " .. tostring(toolchain_name))
    end
    return entry
end

--- 将架构 traits 映射为工具链 flags
---@param toolchain_config ToolchainsConfig 工具链配置
---@param toolchain_name string 工具链名称
---@param traits table 架构 traits
---@return table flags
function map_arch_flags(toolchain_config, toolchain_name, traits)
    if not toolchain_name or toolchain_name == "" then
        raise("toolchain not set")
    end
    validate_arch_traits(traits)
    local entry = resolve_toolchain_entry(toolchain_config, toolchain_name)
    local mapper = toolchain_mappers[toolchain_name]
    if not mapper then
        raise("toolchain arch mapper not found: " .. tostring(toolchain_name))
    end
    return mapper(traits, entry)
end

return {
    validate_arch_traits = validate_arch_traits,
    map_arch_flags = map_arch_flags,
}

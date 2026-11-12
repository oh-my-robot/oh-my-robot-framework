--- @file oh_my_robot/xmake/toolchains/toolchain_validate.lua
--- @brief 工具链校验
--- @details 校验工具链可用性、最低版本与硬浮点支持。

local MIN_VERSIONS = {
    armclang = "6.14",
    tiarmclang = "3.2",
}

local checked_versions = {}

--- 归一化工具链名称（去掉参数后缀）
---@param name string|nil
---@return string|nil
local function base_name(name)
    return (name or ""):match("^(.-)%[") or name
end

--- 解析配置中的工具链名称
---@return string|nil
local function configured_toolchain_name()
    local config = import("core.project.config")
    config.load()
    local name = config.get("toolchain")
    if type(name) == "table" then
        name = name[1]
    end
    return name
end

--- 合并命令行/配置中的工具链名称
---@param name string|nil
---@return string|nil
local function resolve_toolchain_name(name)
    local configured = configured_toolchain_name()
    if configured and configured ~= "" then
        if not name or name == "" then
            return configured
        end
        if not name:find("%[", 1, true) and configured:find(name .. "[", 1, true) == 1 then
            return configured
        end
    end
    return name
end

--- 加载并检查工具链实例
---@param name string
---@return toolchain
local function load_toolchain(name)
    local config = import("core.project.config")
    config.load()
    local toolchain = import("core.tool.toolchain")
    local instance = toolchain.load(name, {
        plat = config.get("plat"),
        arch = config.get("arch"),
    })
    if not instance then
        raise("toolchain not loaded: " .. tostring(name))
    end
    instance:check()
    return instance
end

--- 从 --version 输出解析版本
---@param base string
---@param output string
---@return string|nil
local function parse_version(base, output)
    if base == "armclang" then
        return output:match("Arm Compiler for Embedded%s+(%d+%.%d+%.?%d*)")
    end
    if base == "tiarmclang" then
        return output:match("TI Arm Clang Compiler%s+(%d+%.%d+%.%d+)")
    end
    return nil
end

--- 校验工具链可用性与最低版本
---@param toolchain_name string|nil
---@return nil
function ensure_toolchain_checked(toolchain_name)
    local name = resolve_toolchain_name(toolchain_name)
    if not name or name == "" then
        raise("toolchain not set")
    end
    local instance = load_toolchain(name)
    local base = base_name(name)
    local minimum = MIN_VERSIONS[base]
    if not minimum or checked_versions[name] then
        return
    end
    local cc = instance:tool("cc")
    if not cc then
        raise("compiler not found for toolchain: " .. tostring(name))
    end
    local version = parse_version(base, os.iorunv(cc, {"--version"}))
    if not version then
        raise(base .. " version parse failed")
    end
    local semver = import("core.base.semver")
    if semver.compare(version, minimum) < 0 then
        raise(string.format("%s version not supported: %s, require >= %s", base, version, minimum))
    end
    checked_versions[name] = version
end

--- 校验硬浮点参数是否被工具链支持
---@param target target
---@param flags string[]|nil
---@param toolchain_name string
---@return nil
function validate_hard_float_support(target, flags, toolchain_name)
    for _, flag in ipairs(flags or {}) do
        if flag == "-mfloat-abi=hard" then
            ensure_toolchain_checked(toolchain_name)
            if not target:has_cflags(flags or {}) then
                raise("toolchain does not support hard-float flags: " .. tostring(toolchain_name))
            end
            return
        end
    end
end

return {
    ensure_toolchain_checked = ensure_toolchain_checked,
    validate_hard_float_support = validate_hard_float_support,
}

--- @file oh_my_robot/build/toolchains/toolchain_validate.lua
--- @brief 工具链校验
--- @details 负责工具链相关校验逻辑。

local MIN_ARMCLANG_VERSION = "6.14"
local version_check_cache = {}

--- 归一化工具链名称（去掉参数后缀）
---@param toolchain_name string|nil 工具链名称
---@return string|nil base_name 基础名称
local function normalize_toolchain_name(toolchain_name)
    if type(toolchain_name) ~= "string" or toolchain_name == "" then
        return toolchain_name
    end
    local base = toolchain_name:match("^(.-)%[")
    return base or toolchain_name
end

--- 判断列表中是否包含指定参数
---@param list table|nil 参数列表
---@param value string 目标参数
---@return boolean found 是否命中
local function list_contains_flag(list, value)
    for _, item in ipairs(list or {}) do
        if item == value then
            return true
        end
    end
    return false
end

--- 解析配置中的 toolchain 名称
---@param config table 配置对象
---@return string|nil toolchain_name
local function resolve_config_toolchain_name(config)
    local value = config.get("toolchain")
    if type(value) == "table" then
        return value[1]
    end
    return value
end

--- 解析 armclang 版本号
---@param output string|nil `armclang --version` 输出
---@return string|nil version
local function parse_armclang_version(output)
    if type(output) ~= "string" or output == "" then
        return nil
    end
    return output:match("Arm Compiler for Embedded%s+(%d+%.%d+%.?%d*)")
end

--- 读取 armclang 版本号
---@param toolchain_instance toolchain 工具链实例
---@return string version armclang 版本
local function resolve_armclang_version(toolchain_instance)
    local cc_program = toolchain_instance:tool("cc")
    if not cc_program then
        raise("armclang compiler not found after toolchain check")
    end
    local output_or_error = os.iorunv(cc_program, {"--version"})
    local version = parse_armclang_version(output_or_error)
    if not version then
        raise("armclang version parse failed from --version output")
    end
    return version
end

--- 校验 armclang 版本下界
---@param toolchain_instance toolchain 工具链实例
---@return nil
local function ensure_armclang_version_supported(toolchain_instance)
    if version_check_cache.armclang ~= nil then
        return
    end
    local semver = import("core.base.semver")
    local version = resolve_armclang_version(toolchain_instance)
    if semver.compare(version, MIN_ARMCLANG_VERSION) < 0 then
        raise("armclang version not supported: " .. version .. ", require >= " .. MIN_ARMCLANG_VERSION)
    end
    version_check_cache.armclang = version
end

--- 统一校验工具版本
---@param toolchain_name string|nil 工具链名称
---@param toolchain_instance toolchain 工具链实例
---@return nil
local function ensure_tool_versions_supported(toolchain_name, toolchain_instance)
    if normalize_toolchain_name(toolchain_name) == "armclang" then
        ensure_armclang_version_supported(toolchain_instance)
    end
end

--- 加载并检查工具链实例
---@param toolchain_name string|nil 工具链名称
---@return toolchain instance 工具链实例
function ensure_toolchain_checked(toolchain_name)
    local config = import("core.project.config")
    local toolchain = import("core.tool.toolchain")
    local plat = config.get("plat")
    local arch = config.get("arch")
    local config_toolchain = resolve_config_toolchain_name(config)
    local resolved_name = toolchain_name
    if config_toolchain and config_toolchain ~= "" then
        if not toolchain_name or toolchain_name == "" then
            resolved_name = config_toolchain
        elseif not toolchain_name:find("%[", 1, true) and config_toolchain:find(toolchain_name .. "[", 1, true) == 1 then
            resolved_name = config_toolchain
        end
    end
    local instance = toolchain.load(resolved_name, {plat = plat, arch = arch})
    if not instance then
        raise("toolchain not loaded: " .. tostring(resolved_name))
    end
    instance:check()
    ensure_tool_versions_supported(resolved_name, instance)
    return instance
end

--- 校验硬浮点参数是否被工具链支持
---@param target target 目标对象
---@param flags table|nil 编译参数列表
---@param toolchain_name string 工具链名称
---@return nil
function validate_hard_float_support(target, flags, toolchain_name)
    if not list_contains_flag(flags, "-mfloat-abi=hard") then
        return
    end
    ensure_toolchain_checked(toolchain_name)
    if not target:has_cflags(flags or {}) then
        raise("toolchain does not support hard-float flags: " .. toolchain_name)
    end
end

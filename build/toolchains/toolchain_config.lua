--- @file oh_my_robot/build/toolchains/toolchain_config.lua
--- @brief 工具链配置与默认值处理
--- @details 统一处理工具链选择、路径解析、配置落地与能力判定。
local toolchain_config_cache = nil
local toolchains_root = os.scriptdir()
local build_root = path.join(toolchains_root, "..")
local modules_root = path.join(build_root, "modules")

--- 归一化工具链名称（去掉参数后缀）
---@param name string|nil 工具链名称
---@return string|nil base_name 基础名称
local function normalize_toolchain_name(name)
    if not name or name == "" then
        return name
    end
    local base = name:match("^(.-)%[")
    return base or name
end

--- 读取配置项的标量值
---@param config table 配置对象
---@param key string 配置键
---@return any value
local function read_config_scalar(config, key)
    local value = config.get(key)
    if type(value) == "table" then
        return value[1]
    end
    return value
end

--- 写入配置项（可选择强制）
---@param config table 配置对象
---@param name string 配置键
---@param value any 配置值
---@param force boolean 是否强制写入
---@return nil
local function set_config_value(config, name, value, force)
    if force then
        config.set(name, value, {force = true})
        return
    end
    config.set(name, value)
end

--- 获取默认工具链预设
---@return table|nil toolchain_preset
local function get_preset_toolchain()
    local om_robot = import("oh-my-robot", {rootdir = modules_root})
    local preset = om_robot.get_preset()
    return preset and preset.toolchain_default or nil
end

--- 获取工具链路径预设映射
---@return table|nil toolchain_presets
local function get_preset_toolchain_presets()
    local om_robot = import("oh-my-robot", {rootdir = modules_root})
    local preset = om_robot.get_preset()
    return preset and preset.toolchain_presets or nil
end

--- 获取工具链配置数据
---@return ToolchainsConfig
function get_toolchain_config()
    if toolchain_config_cache then
        return toolchain_config_cache
    end
    local toolchain_config = import("toolchains.data", {rootdir = build_root}).get()
    if type(toolchain_config) ~= "table" then
        raise("toolchain config invalid")
    end
    toolchain_config_cache = toolchain_config
    return toolchain_config
end

--- 解析工具链条目
---@param toolchain_config ToolchainsConfig 工具链配置
---@param toolchain_name string 工具链名称
---@return table entry
local function resolve_toolchain_entry(toolchain_config, toolchain_name)
    local entry = toolchain_config.toolchains
        and toolchain_config.toolchains[toolchain_name]
        or nil
    if not entry then
        raise("toolchain not supported: " .. tostring(toolchain_name))
    end
    return entry
end

local overrides_module = import("toolchains.toolchain_overrides", {rootdir = build_root})

--- 解析工具链选择（CLI/预设/默认）
---@param toolchain_config ToolchainsConfig 工具链配置
---@param config table 配置对象
---@return table selection
local function resolve_toolchain_selection(toolchain_config, config)
    local config_toolchain = read_config_scalar(config, "toolchain")
    local cli_toolchain = config.readonly("toolchain") and config_toolchain or nil
    local preset_toolchain = get_preset_toolchain()
    local preset_toolchain_name = preset_toolchain and preset_toolchain.name or nil
    local selected_raw = cli_toolchain
        or config_toolchain
        or preset_toolchain_name
        or toolchain_config.default
    if not selected_raw or selected_raw == "" then
        raise("toolchain not set: please pass --toolchain=<name> or configure default toolchain.")
    end
    local selected_name = normalize_toolchain_name(selected_raw)
    local entry = resolve_toolchain_entry(toolchain_config, selected_name)
    return {
        raw = selected_raw,
        name = selected_name,
        entry = entry,
        cli_toolchain = cli_toolchain,
        preset_toolchain = preset_toolchain,
    }
end

--- 解析工具链路径（sdk/bin）
---@param selection table 选中信息
---@param config table 配置对象
---@return table paths
local function resolve_toolchain_paths(selection, config)
    local config_sdk = read_config_scalar(config, "sdk")
    local config_bin = read_config_scalar(config, "bin")
    local cli_sdk = config.readonly("sdk") and config_sdk or nil
    local cli_bin = config.readonly("bin") and config_bin or nil
    local preset_toolchain = selection.preset_toolchain
    local preset_toolchain_presets = get_preset_toolchain_presets()
    local preset_by_name = preset_toolchain_presets and preset_toolchain_presets[selection.name] or nil
    local sdkdir = cli_sdk
        or (preset_by_name and preset_by_name.sdk)
        or config_sdk
        or selection.entry.sdk
    local bindir = cli_bin
        or (preset_by_name and preset_by_name.bin)
        or config_bin
        or selection.entry.bin
    if not sdkdir and not bindir then
        raise("toolchain path not set: " .. selection.name)
    end
    if sdkdir and not os.isdir(sdkdir) then
        raise("toolchain sdk not found: " .. sdkdir)
    end
    if bindir and not os.isdir(bindir) then
        raise("toolchain bin not found: " .. bindir)
    end
    return {
        sdkdir = sdkdir,
        bindir = bindir,
        cli_sdk = cli_sdk,
        cli_bin = cli_bin,
    }
end

--- 应用工具链差异化处理
---@param selection table 选中信息
---@param paths table 路径信息
---@return string toolchain_raw
local function apply_toolchain_overrides(selection, paths)
    return overrides_module.apply_toolchain_overrides(selection, paths)
end

--- 解析并返回工具链上下文
---@param toolchain_config ToolchainsConfig|nil 工具链配置
---@return table context
function resolve_toolchain_context(toolchain_config)
    local config = import("core.project.config")
    local config_data = toolchain_config or get_toolchain_config()
    local selection = resolve_toolchain_selection(config_data, config)
    local paths = resolve_toolchain_paths(selection, config)
    local toolchain_raw = apply_toolchain_overrides(selection, paths)
    local force_toolchain = selection.cli_toolchain and toolchain_raw ~= selection.cli_toolchain or false
    return {
        config = config,
        toolchain_config = config_data,
        selection = selection,
        entry = selection.entry,
        toolchain_name = selection.name,
        toolchain_raw = toolchain_raw,
        force_toolchain = force_toolchain,
        paths = paths,
    }
end

--- 解析当前工具链名称（基础名）
---@return string toolchain_name
function resolve_toolchain_name()
    local config = import("core.project.config")
    local toolchain_config = get_toolchain_config()
    local selection = resolve_toolchain_selection(toolchain_config, config)
    return selection.name
end

--- 应用工具链默认值并落地到配置
---@param toolchain_context table|nil 工具链上下文
---@return table context
function apply_defaults(toolchain_context)
    local context = toolchain_context
    if not context or not context.toolchain_config then
        context = resolve_toolchain_context(context)
    end
    local config = context.config
    local entry = context.entry
    local toolchain_raw = context.toolchain_raw
    local cli_toolchain = context.selection.cli_toolchain
    if cli_toolchain and cli_toolchain ~= "" then
        set_config_value(config, "toolchain", toolchain_raw, true)
    else
        set_config_value(config, "toolchain", toolchain_raw, false)
    end
    if entry.plat and not config.readonly("plat") then
        config.set("plat", entry.plat)
    end
    if context.paths.sdkdir and not config.readonly("sdk") then
        config.set("sdk", context.paths.sdkdir)
    end
    if context.paths.bindir and not config.readonly("bin") then
        config.set("bin", context.paths.bindir)
    end
    if entry.linker_flag then
        config.set("toolchain_linker_flag", entry.linker_flag)
    end
    config.save()
    return context
end

--- 解析平台 arch（工具链可覆盖，默认来自板级 CPU）
---@param toolchain_config ToolchainsConfig 工具链配置
---@param toolchain_name string 工具链名称
---@param board_arch string|nil 板级 arch
---@return string platform_arch
function resolve_platform_arch(toolchain_config, toolchain_name, board_arch)
    local entry = resolve_toolchain_entry(toolchain_config, toolchain_name)
    if entry.arch and entry.arch ~= "" then
        return entry.arch
    end
    if not board_arch or board_arch == "" then
        raise("board arch not set")
    end
    return board_arch
end

--- 写入平台 arch
---@param platform_arch string 平台 arch
---@return nil
function apply_platform_arch(platform_arch)
    if not platform_arch or platform_arch == "" then
        raise("platform arch not set")
    end
    local config = import("core.project.config")
    if config.readonly("arch") then
        return
    end
    config.set("arch", platform_arch)
    config.save()
end

--- 判断是否应将 BSP arch ldflags 注入链接阶段
---@param toolchain_config ToolchainsConfig 工具链配置
---@param toolchain_name string|nil 工具链名称
---@return boolean include_ldflags
function should_include_arch_ldflags(toolchain_config, toolchain_name)
    if not toolchain_name or toolchain_name == "" then
        return true
    end
    local entry = toolchain_config.toolchains
        and toolchain_config.toolchains[toolchain_name]
        or nil
    if not entry then
        return true
    end
    if entry.linker_accepts_arch_flags == nil then
        return true
    end
    return entry.linker_accepts_arch_flags
end

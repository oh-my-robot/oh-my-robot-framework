--- @file oh_my_robot/build/config/options.lua
--- @brief OM 构建选项定义
--- @details 负责 board/os/sync_accel/semihosting 选项与上下文写入。
local om_root = path.join(os.scriptdir(), "..", "..")
local modules_root = path.join(om_root, "build", "modules")
local bsp_root = path.join(om_root, "platform", "bsp")
local osal_root = path.join(om_root, "platform", "osal")
includes(path.join(bsp_root, "data", "boards", "index.lua"))
includes(path.join(osal_root, "index.lua"))
local defaults = om_defaults or {}
if type(defaults) ~= "table" then
    raise("defaults invalid: expected table")
end

--- 判断列表是否包含指定值
---@param values table|nil 值列表
---@param value any 目标值
---@return boolean found 是否命中
local function list_contains(values, value)
    for _, item in ipairs(values or {}) do
        if item == value then
            return true
        end
    end
    return false
end

--- 校验并规范化名称列表
---@param values table|nil 名称列表
---@param label string 标签
---@return string[] names 名称列表
local function normalize_name_list(values, label)
    if type(values) ~= "table" then
        raise(label .. " invalid: expected list")
    end
    local names = {}
    for _, value in ipairs(values) do
        if type(value) ~= "string" or value == "" then
            raise(label .. " invalid item: " .. tostring(value))
        end
        names[#names + 1] = value
    end
    table.sort(names)
    return names
end

--- 获取 BSP 板级名称列表（静态索引）
---@return string[] names 板级名称列表
local function list_board_names()
    return normalize_name_list(om_board_index, "board index")
end

--- 获取 OSAL OS 名称列表（静态索引）
---@return string[] names OS 名称列表
local function list_os_names()
    return normalize_name_list(om_os_index, "os index")
end

--- 应用工具链默认值并返回上下文
---@param toolchain_lib ToolchainLib 工具链工具库
---@return table context 工具链上下文
local function apply_toolchain_defaults(toolchain_lib)
    local toolchain_context = toolchain_lib.resolve_toolchain_context()
    toolchain_lib.apply_defaults(toolchain_context)
    return toolchain_context
end

--- 处理预设项覆盖
---@param option option 选项对象
---@param config table 配置对象
---@param available_values table 可选值列表
---@param preset_value string|nil 预设值
---@param option_name string 选项名
---@return nil
local function apply_preset_option(option, config, available_values, preset_value, option_name)
    if not preset_value then
        return
    end
    if not list_contains(available_values, preset_value) then
        raise("preset " .. option_name .. " not found: " .. tostring(preset_value))
    end
    if not config.readonly(option_name) then
        option:set_value(preset_value)
    end
end

--- 解析选项值（为空则注入默认值）
---@param option option 选项对象
---@param default_value string|nil 默认值
---@param option_label string 选项标签
---@return string value 选项值
local function resolve_option_value(option, default_value, option_label)
    local value = option:value()
    if (not value or value == "") and default_value then
        option:set_value(default_value)
        value = option:value()
    end
    if not value or value == "" then
        raise(option_label .. " not set")
    end
    return value
end

--- 校验并解析默认值
---@param value string|nil 默认值
---@param values table 可选值列表
---@param label string 标签
---@return string resolved 默认值
local function resolve_default_value(value, values, label)
    if not value or value == "" then
        return values[1]
    end
    if not list_contains(values, value) then
        raise("default " .. label .. " not found: " .. tostring(value))
    end
    return value
end

--- 解析同步加速默认值
---@param value string|nil 默认值
---@return string resolved
local function resolve_sync_default(value)
    local normalized = value or "auto"
    if normalized ~= "auto" and normalized ~= "none" then
        raise("default sync_accel invalid: " .. tostring(normalized))
    end
    return normalized
end

--- 解析 semihosting 默认值
---@param value string|nil 默认值
---@return string resolved
local function resolve_semihosting_default(value)
    local normalized = value or "off"
    if normalized ~= "off" and normalized ~= "on" then
        raise("default semihosting invalid: " .. tostring(normalized))
    end
    return normalized
end

--- 持久化 flash 预设到配置
---@param config table 配置对象
---@param preset table|nil 预设配置
---@return nil
local function persist_flash_preset(config, preset)
    local flash = preset and preset.flash or nil
    if type(flash) ~= "table" then
        return
    end
    local jlink = type(flash.jlink) == "table" and flash.jlink or flash
    if type(jlink) ~= "table" then
        return
    end
    local has_value = false
    --- 写入单个字段
    ---@param key string 字段名
    ---@param value any 字段值
    ---@return nil
    local function set_flash_value(key, value)
        if value == nil then
            return
        end
        config.set("om_flash_" .. key, value)
        has_value = true
    end
    set_flash_value("device", jlink.device)
    set_flash_value("interface", jlink.interface)
    set_flash_value("speed", jlink.speed)
    set_flash_value("program", jlink.program)
    set_flash_value("target", jlink.target)
    set_flash_value("firmware", jlink.firmware)
    set_flash_value("file", jlink.file)
    set_flash_value("prefer_hex", jlink.prefer_hex)
    set_flash_value("reset", jlink.reset)
    set_flash_value("run", jlink.run)
    if has_value then
        config.save()
    end
end

local board_values = list_board_names()
local default_board = resolve_default_value(defaults.board, board_values, "board")
local os_values = list_os_names()
local default_os = resolve_default_value(defaults.os, os_values, "os")
local default_sync_accel = resolve_sync_default(defaults.sync_accel)
local default_semihosting = resolve_semihosting_default(defaults.semihosting)

--- @option board
--- @brief 选择板级
--- @details 从 BSP 数据目录枚举板级，并在配置期写入上下文。
option("board")
    if default_board then
        set_default(default_board)
    end
    set_showmenu(true)
    set_description("select board")
    if #board_values > 0 then
        set_values(table.unpack(board_values))
    end
    --- 板级选项校验与上下文写入回调
    ---@param option option 选项对象
    after_check(function(option)
        -- 应用工具链默认值，并确保上下文具备 toolchain 基础字段
        local config = import("core.project.config")
        local oh_my_robot = import("oh-my-robot", {rootdir = modules_root})
        local toolchain_lib = import("toolchain_lib", {rootdir = modules_root})
        local toolchain_context = apply_toolchain_defaults(toolchain_lib)
        if #board_values == 0 then
            raise("board list empty: no board data found")
        end
        -- 读取预设并覆盖 board
        local preset = oh_my_robot.get_preset()
        local preset_board = preset and preset.board and preset.board.name or nil
        apply_preset_option(option, config, board_values, preset_board, "board")
        persist_flash_preset(config, preset)

        -- 解析板级与架构参数
        local board_name = resolve_option_value(option, default_board, "board")
        local toolchain_name = toolchain_context.toolchain_name
        local bsp = import("bsp", {rootdir = bsp_root})
        local profile = bsp.get_profile(board_name)
        local arch = bsp.resolve_arch(profile)
        local arch_traits = bsp.get_arch_traits(profile)
        local platform_arch = toolchain_lib.resolve_platform_arch(
            toolchain_context.toolchain_config,
            toolchain_name,
            arch
        )
        toolchain_lib.apply_platform_arch(platform_arch)
        toolchain_lib.validate_arch_traits(arch_traits)
        local arch_flags = toolchain_lib.map_arch_flags(
            toolchain_context.toolchain_config,
            toolchain_name,
            arch_traits
        )
        local toolchain_flags = toolchain_lib.get_flags(
            toolchain_context.toolchain_config,
            toolchain_name
        )
        -- 写入上下文与板级 OS 配置
        oh_my_robot.set_context_extra({
            toolchain_name = toolchain_name,
            board_name = board_name,
            chip_name = profile.board.chip,
            arch = arch,
            arch_flags = arch_flags,
            toolchain_flags = toolchain_flags,
        })
        bsp.update_board_os_context(oh_my_robot, board_name, get_config("os"))
    end)
option_end()

--- @option os
--- @brief 选择 OS/RTOS
--- @details 从 OSAL 目录枚举 OS，并在配置期写入上下文。
option("os")
    if default_os then
        set_default(default_os)
    end
    set_showmenu(true)
    set_description("select os/rtos")
    if #os_values > 0 then
        set_values(table.unpack(os_values))
    end
    --- OS 选项校验与上下文写入回调
    ---@param option option 选项对象
    after_check(function(option)
        -- 应用工具链默认值，保证 toolchain 上下文字段一致
        local config = import("core.project.config")
        local oh_my_robot = import("oh-my-robot", {rootdir = modules_root})
        local toolchain_lib = import("toolchain_lib", {rootdir = modules_root})
        local toolchain_context = apply_toolchain_defaults(toolchain_lib)
        if #os_values == 0 then
            raise("os list empty: no os data found")
        end
        -- 读取预设并覆盖 OS
        local preset = oh_my_robot.get_preset()
        local preset_os = preset and preset.os and preset.os.name or nil
        apply_preset_option(option, config, os_values, preset_os, "os")

        local os_name = resolve_option_value(option, default_os, "os")
        local toolchain_name = toolchain_context.toolchain_name
        local os_dir = path.join(osal_root, os_name)
        if not os.isdir(os_dir) then
            raise("os not found: " .. os_name)
        end
        -- 写入上下文，并在 board 已选时同步 OS 配置
        oh_my_robot.set_context_extra({
            toolchain_name = toolchain_name,
            os_name = os_name,
        })
        local board_name = get_config("board")
        if board_name and board_name ~= "" then
            local bsp = import("bsp", {rootdir = bsp_root})
            bsp.update_board_os_context(oh_my_robot, board_name, os_name)
        end
    end)
option_end()

--- @option sync_accel
--- @brief 同步原语加速策略
--- @details 仅支持 auto/none，用于选择同步加速后端。
option("sync_accel")
    set_default(default_sync_accel)
    set_showmenu(true)
    set_description("select sync accel backend")
    set_values("auto", "none")
option_end()

--- @option semihosting
--- @brief semihosting 策略
--- @details 仅支持 off/on；off 时启用 armclang 非 semihosting syscall 覆盖桩。
option("semihosting")
    set_default(default_semihosting)
    set_showmenu(true)
    set_description("select semihosting mode")
    set_values("off", "on")
option_end()


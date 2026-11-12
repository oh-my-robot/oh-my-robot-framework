--- @file oh_my_robot/platform/osal/freertos/xmake.lua
--- @brief FreeRTOS OSAL 构建脚本
--- @details 负责 FreeRTOS 源码与端口文件的注入。
local freertos_root = os.scriptdir()

local include_dir = path.join(freertos_root, "FreeRTOS", "include")

--- 解析 FreeRTOS 配置与端口路径
---@param board_config_dir string 板级 OS 配置目录
---@param toolchain_name string 工具链名称
---@param arch string 架构名称
---@return table|nil freertos_paths 路径信息
---@return string|nil err_msg 错误信息
local freertos_port_toolchain_aliases = {
    ["tiarmclang"] = "gnu-rm",  -- TI Clang 兼容 GCC 内联汇编，复用 gnu-rm 端口
}

local function resolve_freertos_paths(board_config_dir, toolchain_name, arch)
    -- 将工具链名映射到实际端口目录（Clang 系复用 GCC 端口）
    local port_toolchain = freertos_port_toolchain_aliases[toolchain_name] or toolchain_name
    local config_path = path.join(board_config_dir, "FreeRTOSConfig.h")
    if not os.isfile(config_path) then
        return nil, "FreeRTOSConfig.h not found: " .. config_path
    end
    local portable_dir = path.join(freertos_root, "portable", port_toolchain, arch)
    local port_file = path.join(portable_dir, "port.c")
    local portmacro_file = path.join(portable_dir, "portmacro.h")
    if not os.isfile(port_file) then
        return nil, "FreeRTOS port.c not found: " .. port_file
    end
    if not os.isfile(portmacro_file) then
        return nil, "FreeRTOS portmacro.h not found: " .. portmacro_file
    end
    return {
        config_dir = board_config_dir,
        portable_dir = portable_dir,
        port_file = port_file,
    }, nil
end

--- @target tar_awapi_osal
--- @brief FreeRTOS 端口公共头路径注入
target("tar_awapi_osal")
    add_rules("oh_my_robot.project_cfg")
    add_includedirs(freertos_root, {public = true})  -- om_osal_portdef.h
target_end()

--- @target tar_os
--- @brief FreeRTOS 静态库
--- @details 注入 FreeRTOS 内核与端口实现。
target("tar_os")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_rules("oh_my_robot.context")
    add_deps("tar_awapi_osal", {public = false})
    add_includedirs(include_dir, {public = false})
    add_includedirs(freertos_root, {public = true})  -- om_osal_portdef.h
    add_files("osal_*_freertos.c")
    add_files("FreeRTOS/*.c")
    add_files("FreeRTOS/portable/*.c")
    --- 配置阶段注入 FreeRTOS 端口与配置文件
    ---@param target target 目标对象
    on_load(function(target)
        -- 读取上下文并解析 FreeRTOS 端口路径
        local oh_my_robot = import("oh-my-robot")
        local context = oh_my_robot.get_context()
        local freertos_paths, err = resolve_freertos_paths(context.board_os_config_dir, context.toolchain_name, context.arch)
        if not freertos_paths then
            raise(err)
        end
        target:add("includedirs", freertos_paths.config_dir, {public = false})
        target:add("includedirs", freertos_paths.portable_dir, {public = false})
        target:add("files", freertos_paths.port_file)
        -- Cortex-M0+ GCC port has a separate portasm.c
        local portasm_file = path.join(freertos_paths.portable_dir, "portasm.c")
        if os.isfile(portasm_file) then
            target:add("files", portasm_file)
        end
    end)
target_end()

--- @file oh_my_robot/platform/sync/xmake.lua
--- @brief SYNC 构建脚本
--- @details 负责同步原语默认实现与加速后端的编译注入。

local sync_root = os.scriptdir()
local lib_root = path.join(sync_root, "..", "..", "lib")
local sync_source_glob = path.join(lib_root, "sync", "src", "*.c")

--- 解析并注入统一的 SYNC 加速宏入口
---@param target target 目标对象
---@param accel_enabled boolean 是否启用任意加速后端
---@return nil
local function apply_sync_accel_defines(target, accel_enabled)
    if accel_enabled then
        target:add("defines", "OM_SYNC_ACCEL=1", {public = true})
    else
        target:add("defines", "OM_SYNC_ACCEL=0", {public = true})
    end
end

--- 规范化 capability 名称（转为 `OM_SYNC_ACCEL_CAP_*` 宏后缀）
---@param capability_name any capability 名称
---@return string|nil normalized 标准化后的宏后缀
local function normalize_sync_capability_name(capability_name)
    if capability_name == nil then
        return nil
    end

    local normalized = tostring(capability_name):upper()
    normalized = normalized:gsub("[^A-Z0-9]+", "_")
    normalized = normalized:gsub("^_+", "")
    normalized = normalized:gsub("_+$", "")
    if normalized == "" then
        return nil
    end
    return normalized
end

--- 注入分原语 capability 宏
---@param target target 目标对象
---@param accel_info table|nil 加速后端信息
---@return nil
local function apply_sync_capability_defines(target, accel_info)
    if not accel_info or type(accel_info) ~= "table" then
        return
    end
    if type(accel_info.capabilities) ~= "table" then
        return
    end

    for capability_key, capability_value in pairs(accel_info.capabilities) do
        local capability_name = nil
        local capability_enabled = false

        if type(capability_key) == "number" then
            capability_name = normalize_sync_capability_name(capability_value)
            capability_enabled = true
        else
            capability_name = normalize_sync_capability_name(capability_key)
            capability_enabled = (capability_value == true) or (capability_value == 1) or (capability_value == "1")
        end

        if capability_name and capability_enabled then
            target:add("defines", "OM_SYNC_ACCEL_CAP_" .. capability_name .. "=1", {public = true})
        end
    end
end

--- @target tar_sync
--- @brief 同步原语静态库
--- @details 注入同步原语与可选加速后端实现。
target("tar_sync")
    add_rules("oh_my_robot.project_cfg")
    set_kind("static")
    add_deps("tar_awapi_sync", {public = true})
    add_deps("tar_awcore", {public = true})
    add_deps("tar_osal", {public = true})
    add_rules("oh_my_robot.context")
    add_files(sync_source_glob)
    --- 配置阶段选择加速后端并注入编译项
    ---@param target target 目标对象
    on_config(function(target)
        -- 读取上下文与同步加速策略
        local oh_my_robot = import("oh-my-robot")
        local sync_accel_lib = import("sync_accel_lib", {rootdir = sync_root})
        local context = oh_my_robot.get_context()
        local os_name = context.os_name
        local accel_mode = get_config("sync_accel") or "auto"
        if accel_mode ~= "auto" and accel_mode ~= "none" then
            raise("sync_accel invalid: " .. accel_mode)
        end

        local os_dir = path.join(sync_root, os_name)
        local accel_files = os.files(path.join(os_dir, "*.c"))
        local accel_enabled = (accel_mode == "auto") and (#accel_files > 0)
        local accel_info = nil

        if accel_enabled then
            local err = nil
            accel_info, err = sync_accel_lib.try_get_accel_info(os_dir)
            if not accel_info then
                if accel_mode == "auto" then
                    accel_enabled = false
                else
                    raise(err)
                end
                print(err)
            end
        end

        if accel_enabled then
            target:add("files", accel_files)
            if accel_info and accel_info.include_dirs then
                for _, include_dir in ipairs(accel_info.include_dirs) do
                    target:add("includedirs", include_dir, {public = false})
                end
            end
            -- 注入 FreeRTOS 头文件路径（accel 后端需要直接使用 FreeRTOS API）
            local freertos_root = path.join(sync_root, "..", "osal", os_name)
            target:add("includedirs", path.join(freertos_root, "FreeRTOS", "include"), {public = false})
            if context.board_os_config_dir then
                target:add("includedirs", context.board_os_config_dir, {public = false})
            end
            if context.toolchain_name and context.arch then
                local portable_dir = path.join(freertos_root, "portable", context.toolchain_name, context.arch)
                if os.isdir(portable_dir) then
                    target:add("includedirs", portable_dir, {public = false})
                end
            end
            apply_sync_accel_defines(target, true)
            apply_sync_capability_defines(target, accel_info)
            return
        end

        apply_sync_accel_defines(target, false)
    end)
target_end()

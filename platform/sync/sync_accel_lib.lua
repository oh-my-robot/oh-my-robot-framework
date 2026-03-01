--- @file oh_my_robot/platform/sync/sync_accel_lib.lua
--- @brief SYNC 加速后端解析工具
--- @details 提供加速后端模块加载与错误降级处理。

--- SYNC 加速后端工具库
---@class SyncAccelLib
local ERROR_PREFIX = "[sync_accel]"

--- 统一错误信息格式
---@param reason string 错误原因
---@param detail any 细节信息
---@return string msg 错误信息
local function format_error(reason, detail)
    local detail_text = tostring(detail or "")
    if detail_text ~= "" then
        return string.format("%s %s: %s", ERROR_PREFIX, reason, detail_text)
    end
    return string.format("%s %s", ERROR_PREFIX, reason)
end

--- 规范化错误信息（避免重复前缀）
---@param reason string 错误原因
---@param detail any 细节信息
---@return string msg 错误信息
local function normalize_error(reason, detail)
    local detail_text = tostring(detail or "")
    if detail_text:find(ERROR_PREFIX, 1, true) then
        return detail_text
    end
    return format_error(reason, detail_text)
end

--- 解析加速后端模块并返回信息
---@param os_dir string OS 目录
---@return table accel_info 加速信息
function resolve_accel_info(os_dir)
    local accel_module_path = path.join(os_dir, "sync_accel.lua")
    if not os.isfile(accel_module_path) then
        raise(format_error("module not found", accel_module_path))
    end
    local accel_module = import("sync_accel", {rootdir = os_dir})
    local accel_info = accel_module.get_accel_info()
    if not accel_info then
        raise(format_error("accel info missing", accel_module_path))
    end
    return accel_info
end

--- 以降级方式获取加速后端信息
---@param os_dir string OS 目录
---@return table|nil accel_info 加速信息
---@return string|nil err_msg 错误信息
function try_get_accel_info(os_dir)
    local accel_info = nil
    local err_msg = nil
    try {
        function()
            accel_info = resolve_accel_info(os_dir)
        end,
        catch {
            function(errors)
                err_msg = normalize_error("accel load failed", errors)
            end
        }
    }
    return accel_info, err_msg
end

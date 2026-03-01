--- @file oh_my_robot/platform/sync/freertos/sync_accel.lua
--- @brief FreeRTOS 同步加速后端信息
--- @details 当前版本不提供可用的 SYNC 加速 capability。

--- 获取加速后端信息
---@return table accel_info 加速信息
function get_accel_info()
    return {
        include_dirs = {},
        capabilities = {},
    }
end

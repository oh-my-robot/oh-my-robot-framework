--- @file oh_my_robot/platform/sync/freertos/sync_accel.lua
--- @brief FreeRTOS 同步加速后端信息
--- @details 声明 completion capability，支持 CAS + Task Notification 加速后端。

--- 获取加速后端信息
---@return table accel_info 加速信息
function get_accel_info()
    return {
        include_dirs = {},
        capabilities = {
            completion = true,
        },
    }
end

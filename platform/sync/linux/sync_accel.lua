--- @file oh_my_robot/platform/sync/linux/sync_accel.lua
--- @brief Linux 同步加速后端骨架
--- @details Phase 0 仅保留结构，不声明任何可用 capability。

function get_accel_info()
    return {
        include_dirs = {},
        capabilities = {},
    }
end

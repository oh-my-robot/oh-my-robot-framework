--- @file oh_my_robot/build/config/defaults.lua
--- @brief OM 默认构建选项
--- @details 定义 board/os/sync_accel/semihosting 的默认选择。

--- 默认选项结构
---@class Defaults
---@field board string 默认板级名称
---@field os string 默认操作系统名称
---@field sync_accel string 默认同步加速策略（auto/none）
---@field semihosting string 默认 semihosting 策略（off/on）

--- 默认选项实例
---@type Defaults
om_defaults = {
    board = "rm-c-board",
    os = "freertos",
    sync_accel = "auto",
    semihosting = "off",
}

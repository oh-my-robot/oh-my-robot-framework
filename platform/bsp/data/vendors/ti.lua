--- @file oh_my_robot/platform/bsp/data/vendors/ti.lua
--- @brief Texas Instruments 供应商数据
--- @details TI MSPM0 系列共用定义。
--- SDK 路径通过 om_preset.lua 的 toolchain_presets 中 sdk 字段注入。

--- 供应商数据结构
---@class BspVendor
---@field name string 供应商名称
---@field defines string[] 预处理宏
---@field includedirs string[] 头文件目录
---@field components table<string, table> 组件映射表
---@type BspVendor
local vendor = {
    name = "TI",
    defines = {},
    includedirs = {},
    components = {},
}

--- 获取供应商数据
---@return BspVendor
function get()
    return vendor
end

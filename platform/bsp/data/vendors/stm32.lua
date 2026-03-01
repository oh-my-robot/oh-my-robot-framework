--- @file oh_my_robot/platform/bsp/data/vendors/stm32.lua
--- @brief STM32 供应商数据
--- @details 描述供应商级的通用宏、头文件与组件信息。

--- 供应商数据结构
---@class BspVendor
---@field name string 供应商名称
---@field defines string[] 预处理宏
---@field includedirs string[] 头文件目录
---@field components table<string, table> 组件映射表
---@type BspVendor
local vendor = {
    name = "STM32",
    defines = {},
    includedirs = {},
    components = {
        cmsis = {
            includedirs = {
                "vendor/STM32/CMSIS/Include",
            },
            headerfiles = {
                "vendor/STM32/CMSIS/Include/**.h",
            },
        },
    },
}

--- 获取供应商数据
---@return BspVendor
function get()
    return vendor
end

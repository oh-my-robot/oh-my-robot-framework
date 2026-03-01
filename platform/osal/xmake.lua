--- @file oh_my_robot/platform/osal/xmake.lua
--- @brief OSAL 构建脚本
--- @details 负责 OSAL 统一目标与 OS 路由。

local osal_root = os.scriptdir()
includes(path.join(osal_root, "index.lua"))

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

--- 获取 OS 名称列表（静态索引）
---@return string[] names OS 名称列表
local function list_os_names()
    return om_os_index or {}
end

local os_values = list_os_names()
local os_name = get_config("os")

if os_name and os_name ~= "" then
    if not list_contains(os_values, os_name) then
        raise("os not found: " .. os_name)
    end
    includes(os_name)
end

--- @target tar_osal
--- @brief OSAL 聚合目标
--- @details 仅做依赖聚合与传播，不直接产出静态库。
target("tar_osal")
    set_kind("phony")
    add_deps("tar_awapi_osal", {public = true})
    add_deps("tar_os", {public = true})
    add_rules("oh_my_robot.context")
target_end()

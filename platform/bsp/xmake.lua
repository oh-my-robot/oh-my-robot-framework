--- @file oh_my_robot/platform/bsp/xmake.lua
--- @brief BSP 构建路由脚本
--- @details 负责加载板级模块并汇总 BSP 目标依赖。

includes(path.join(os.scriptdir(), "data", "boards", "index.lua"))

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

--- 获取板级名称列表（静态索引）
---@return string[] names 板级名称列表
local function list_board_names()
    return om_board_index or {}
end

local board_values = list_board_names()
local board_name = get_config("board")
if board_name and board_name ~= "" then
    if not list_contains(board_values, board_name) then
        raise("board not found: " .. board_name)
    end
    includes(path.join("boards", board_name))
end

--- @target tar_bsp
--- @brief BSP 聚合静态库
--- @details 依赖选中板级目标 tar_board。
target("tar_bsp")
    set_kind("static")
    add_deps("tar_board", {public = true})
target_end()

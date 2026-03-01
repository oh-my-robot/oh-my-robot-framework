--- @file oh_my_robot/platform/bsp/components.lua
--- @brief BSP 组件合并
--- @details 负责板级组件数据的合并策略。
local bsp_root = os.scriptdir()
local utils = import("utils", {rootdir = bsp_root})

local COMPONENT_FIELDS = {
    "defines",
    "includedirs",
    "sources",
    "headerfiles",
    "extrafiles",
}

--- 合并组件字段（board 覆盖优先）
---@param dst table 目标列表
---@param field string 字段名
---@param vendor_component table|nil vendor 组件数据
---@param chip_component table|nil chip 组件数据
---@param board_override table|nil board 覆盖数据
---@return nil
local function append_component_field(dst, field, vendor_component, chip_component, board_override)
    if board_override and board_override[field] then
        utils.append_list(dst, utils.clone_list(board_override[field]))
        return
    end
    utils.append_list(dst, vendor_component and vendor_component[field])
    utils.append_list(dst, chip_component and chip_component[field])
end

--- 收集并合并组件数据（board 白名单驱动）
---@param profile table Profile 数据
---@return table components 合并后的组件数据
function collect_component_data(profile)
    local board = profile.board
    local component_names = board.components
    if type(component_names) ~= "table" or #component_names == 0 then
        raise("board components not defined: " .. tostring(board.name))
    end

    local merged = {
        defines = {},
        includedirs = {},
        sources = {},
        headerfiles = {},
        extrafiles = {},
    }

    local vendor_components = profile.vendor.components or {}
    local chip_components = profile.chip.components or {}
    local board_overrides = board.component_overrides or {}

    for _, name in ipairs(component_names) do
        local vendor_component = vendor_components[name]
        local chip_component = chip_components[name]
        local board_override = board_overrides[name]
        if not vendor_component and not chip_component and not board_override then
            raise("component not found: " .. tostring(name))
        end
        for _, field in ipairs(COMPONENT_FIELDS) do
            append_component_field(merged[field], field, vendor_component, chip_component, board_override)
        end
    end

    return merged
end

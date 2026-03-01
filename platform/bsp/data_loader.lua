--- @file oh_my_robot/platform/bsp/data_loader.lua
--- @brief BSP 数据加载
--- @details 负责加载 board/chip/vendor 数据。
local bsp_root = os.scriptdir()
local utils = import("utils", {rootdir = bsp_root})

--- 载入数据条目
---@param kind string 数据类型（boards/chips/vendors）
---@param name string 条目名称
---@return table entry 数据条目
local function load_entry(kind, name)
    utils.require_string(name, kind .. " name")
    local module_path = "data." .. kind .. "." .. name
    local module = import(module_path, {rootdir = bsp_root})
    if type(module) ~= "table" or type(module.get) ~= "function" then
        raise(kind .. " module invalid: " .. tostring(name))
    end
    local entry = module.get()
    if type(entry) ~= "table" then
        raise(kind .. " data invalid: " .. tostring(name))
    end
    return entry
end

--- 获取板级 profile（board + chip + vendor）
---@param board_name string 板级名称
---@return table profile Profile 数据
function get_profile(board_name)
    local board = load_entry("boards", board_name)
    local chip_name = utils.require_string(board.chip, "board chip")
    local chip = load_entry("chips", chip_name)
    local vendor_name = chip.vendor or board.vendor
    vendor_name = utils.require_string(vendor_name, "vendor")
    local vendor = load_entry("vendors", vendor_name)
    return {
        board = board,
        chip = chip,
        vendor = vendor,
    }
end

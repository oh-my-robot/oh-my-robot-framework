--- @file oh_my_robot/platform/bsp/arch.lua
--- @brief BSP 架构解析
--- @details 负责架构名称与架构参数解析。
local bsp_root = os.scriptdir()
local utils = import("utils", {rootdir = bsp_root})

--- 解析架构名称
---@param profile table Profile 数据
---@return string arch 架构名称
function resolve_arch(profile)
    if not profile or not profile.chip then
        raise("profile not loaded")
    end
    return utils.require_string(profile.chip.arch, "chip arch")
end

--- 获取架构 traits（芯片仅提供数据，工具链负责映射）
---@param profile table Profile 数据
---@return table traits 架构 traits
function get_arch_traits(profile)
    if not profile or not profile.chip then
        raise("profile not loaded")
    end
    local traits = profile.chip.arch_traits
    if type(traits) ~= "table" then
        raise("arch traits not defined for chip: " .. tostring(profile.chip.name))
    end
    return traits
end

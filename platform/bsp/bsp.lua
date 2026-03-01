--- @file oh_my_robot/platform/bsp/bsp.lua
--- @brief BSP 数据访问与构建辅助
--- @details 负责对外暴露 BSP 数据与构建接口。
local bsp_root = os.scriptdir()
local data_loader = import("data_loader", {rootdir = bsp_root})
local arch = import("arch", {rootdir = bsp_root})
local inputs = import("inputs", {rootdir = bsp_root})
local assets = import("assets", {rootdir = bsp_root})

--- 获取板级 profile（board + chip + vendor）
---@param board_name string 板级名称
---@return table profile Profile 数据
function get_profile(board_name)
    return data_loader.get_profile(board_name)
end

--- 解析架构名称
---@param profile table Profile 数据
---@return string arch 架构名称
function resolve_arch(profile)
    return arch.resolve_arch(profile)
end

--- 获取架构 traits
---@param profile table Profile 数据
---@return table traits 架构 traits
function get_arch_traits(profile)
    return arch.get_arch_traits(profile)
end

--- 获取板级构建输入（头文件、源文件、宏与额外资源）
---@param board_name string 板级名称
---@return table inputs 构建输入
function get_board_build_inputs(board_name)
    return inputs.get_board_build_inputs(board_name)
end

--- 获取板级强覆盖源文件（直接并入 binary）
---@param board_name string 板级名称
---@return string[] sources 覆盖源文件
function get_board_override_sources(board_name)
    return inputs.get_board_override_sources(board_name)
end

--- 更新板级 OS 配置上下文
---@param oh_my_robot table oh_my_robot 模块
---@param board_name string 板级名称
---@param os_name string OS 名称
---@return nil
function update_board_os_context(om_robot, board_name, os_name)
    return assets.update_board_os_context(om_robot, board_name, os_name)
end

--- 获取板级构建资源（启动文件与链接脚本）
---@param board_name string 板级名称
---@param toolchain_name string 工具链名称
---@return table assets 资源信息
function get_board_build_assets(board_name, toolchain_name)
    return assets.get_board_build_assets(board_name, toolchain_name)
end

return {
    get_profile = get_profile,
    resolve_arch = resolve_arch,
    get_arch_traits = get_arch_traits,
    get_board_build_inputs = get_board_build_inputs,
    get_board_override_sources = get_board_override_sources,
    update_board_os_context = update_board_os_context,
    get_board_build_assets = get_board_build_assets,
}

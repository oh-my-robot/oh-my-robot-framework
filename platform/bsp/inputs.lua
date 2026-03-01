--- @file oh_my_robot/platform/bsp/inputs.lua
--- @brief BSP 构建输入组装
--- @details 负责整合 board/chip/vendor 输入并规范化路径。
local bsp_root = os.scriptdir()
local utils = import("utils", {rootdir = bsp_root})
local data_loader = import("data_loader", {rootdir = bsp_root})
local components = import("components", {rootdir = bsp_root})

--- 收集 profile 中声明的强覆盖源文件
---@param profile table Profile 数据
---@return string[] sources 覆盖源文件列表
local function collect_override_sources(profile)
    local sources = {}
    utils.append_list(sources, profile.vendor.override_sources)
    utils.append_list(sources, profile.chip.override_sources)
    utils.append_list(sources, profile.board.override_sources)
    return sources
end

--- 过滤需要从静态库中排除的源文件
---@param all_sources string[] 全量源文件
---@param excluded_sources string[] 需排除源文件
---@return string[] sources 过滤后源文件
local function filter_excluded_sources(all_sources, excluded_sources)
    if not excluded_sources or #excluded_sources == 0 then
        return all_sources
    end
    local excluded_map = {}
    for _, source in ipairs(excluded_sources) do
        excluded_map[source] = true
    end
    local sources = {}
    for _, source in ipairs(all_sources or {}) do
        if not excluded_map[source] then
            sources[#sources + 1] = source
        end
    end
    return sources
end

--- 获取板级强覆盖源文件（用于直接并入 binary，保证 weak 可被 strong 覆盖）
---@param board_name string 板级名称
---@return string[] sources 覆盖源文件
function get_board_override_sources(board_name)
    local profile = data_loader.get_profile(board_name)
    local override_sources = collect_override_sources(profile)
    return utils.normalize_paths(bsp_root, override_sources)
end

--- 获取板级构建输入（头文件、源文件、宏与额外资源）
---@param board_name string 板级名称
---@return table inputs 构建输入
function get_board_build_inputs(board_name)
    local profile = data_loader.get_profile(board_name)

    local inputs = {
        defines = {},
        includedirs = {},
        sources = {},
        headerfiles = {},
        extrafiles = {},
    }

    utils.append_list(inputs.defines, profile.vendor.defines)
    utils.append_list(inputs.defines, profile.chip.defines)
    utils.append_list(inputs.defines, profile.board.defines)

    utils.append_list(inputs.includedirs, profile.vendor.includedirs)
    utils.append_list(inputs.includedirs, profile.chip.includedirs)
    utils.append_list(inputs.includedirs, profile.board.includedirs)

    utils.append_list(inputs.sources, profile.vendor.sources)
    utils.append_list(inputs.sources, profile.chip.sources)
    utils.append_list(inputs.sources, profile.board.sources)

    local component_data = components.collect_component_data(profile)
    utils.append_list(inputs.defines, component_data.defines)
    utils.append_list(inputs.includedirs, component_data.includedirs)
    utils.append_list(inputs.sources, component_data.sources)
    utils.append_list(inputs.headerfiles, component_data.headerfiles)
    utils.append_list(inputs.extrafiles, component_data.extrafiles)

    inputs.includedirs = utils.normalize_paths(bsp_root, inputs.includedirs)
    inputs.sources = utils.normalize_paths(bsp_root, inputs.sources)
    inputs.override_sources = get_board_override_sources(board_name)
    inputs.sources = filter_excluded_sources(inputs.sources, inputs.override_sources)
    inputs.headerfiles = utils.normalize_paths(bsp_root, inputs.headerfiles)
    inputs.extrafiles = utils.normalize_paths(bsp_root, inputs.extrafiles)

    return inputs
end

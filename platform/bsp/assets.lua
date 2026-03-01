--- @file oh_my_robot/platform/bsp/assets.lua
--- @brief BSP 资源解析
--- @details 负责板级 OS 配置与启动/链接脚本解析。
local bsp_root = os.scriptdir()
local utils = import("utils", {rootdir = bsp_root})
local data_loader = import("data_loader", {rootdir = bsp_root})

--- 更新板级 OS 配置上下文
---@param oh_my_robot table oh_my_robot 模块
---@param board_name string 板级名称
---@param os_name string OS 名称
---@return nil
function update_board_os_context(om_robot, board_name, os_name)
    if not om_robot or type(om_robot.set_context_extra) ~= "function" then
        raise("oh_my_robot context setter not available")
    end
    utils.require_string(os_name, "os")
    local profile = data_loader.get_profile(board_name)
    local osal_map = profile.board.osal or {}
    local osal_dir = osal_map[os_name]
    if not osal_dir or osal_dir == "" then
        raise("board os config not found: " .. board_name .. "/" .. os_name)
    end
    local osal_path = path.is_absolute(osal_dir) and osal_dir or path.join(bsp_root, osal_dir)
    if not os.isdir(osal_path) then
        raise("board os config dir not found: " .. osal_path)
    end
    om_robot.set_context_extra({
        os_name = os_name,
        board_os_config_dir = osal_path,
    })
end

--- 获取板级构建资源（启动文件与链接脚本）
---@param board_name string 板级名称
---@param toolchain_name string 工具链名称
---@return table assets 资源信息
function get_board_build_assets(board_name, toolchain_name)
    utils.require_string(toolchain_name, "toolchain")
    local profile = data_loader.get_profile(board_name)

    --- 根据工具链解析资源映射
    ---@param entry table|nil 资源定义
    ---@param key string 字段名
    ---@return string|nil raw
    local function resolve_asset(entry, key)
        local mapping = entry and entry[key] or nil
        if not mapping then
            return nil
        end
        return mapping[toolchain_name] or mapping.default
    end

    --- 解析资源路径并校验存在性
    ---@param key string 字段名
    ---@param label string 错误标签
    ---@return string path_abs 绝对路径
    local function resolve_asset_path(key, label)
        local raw = resolve_asset(profile.board, key)
            or resolve_asset(profile.chip, key)
            or resolve_asset(profile.vendor, key)
        if not raw then
            raise(label .. " not found: " .. board_name .. "/" .. toolchain_name)
        end
        local abs = path.is_absolute(raw) and raw or path.join(bsp_root, raw)
        if not os.isfile(abs) then
            raise(label .. " file not found: " .. abs)
        end
        return abs
    end

    return {
        startup = resolve_asset_path("startup", "startup"),
        linkerscript = resolve_asset_path("linkerscript", "linkerscript"),
    }
end

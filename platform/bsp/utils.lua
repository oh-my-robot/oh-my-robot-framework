--- @file oh_my_robot/platform/bsp/utils.lua
--- @brief BSP 工具函数
--- @details 提供字符串校验与列表操作工具。

--- 断言字符串不为空
---@param value string|nil 参数值
---@param label string 参数名
---@return string value 校验后的字符串
function require_string(value, label)
    if not value or value == "" then
        raise(label .. " not set")
    end
    return value
end

--- 追加列表内容
---@param dst table 目标列表
---@param src table|nil 源列表
---@return nil
function append_list(dst, src)
    for _, item in ipairs(src or {}) do
        dst[#dst + 1] = item
    end
end

--- 拷贝列表
---@param src table|nil 源列表
---@return table dst 新列表
function clone_list(src)
    local dst = {}
    append_list(dst, src)
    return dst
end

--- 将相对路径转换为相对于项目根目录的路径
---@param bsp_root string BSP 根目录
---@param items table|nil 路径列表
---@return table paths 相对路径列表
function normalize_paths(bsp_root, items)
    local project_dir = os.projectdir()
    local paths = {}
    for _, item in ipairs(items or {}) do
        local p
        if path.is_absolute(item) then
            p = item
        else
            p = path.join(bsp_root, item)
        end
        -- 确保路径相对于项目根目录，避免在 Windows 上出现绝对路径拼接导致的错误
        paths[#paths + 1] = path.relative(p, project_dir)
    end
    return paths
end

--- @file oh_my_robot/build/toolchains/toolchain_flags.lua
--- @brief 工具链参数与目标注入
--- @details 负责 flags 校验、合并与目标注入。

--- 追加构建参数到目标
---@param target target 目标对象
---@param kind string 参数类型（cflags/cxflags/asflags/ldflags）
---@param flags table|nil 参数列表
---@param force boolean 是否强制设置
---@return nil
function add_target_flags(target, kind, flags, force)
    if not flags or #flags == 0 then
        return
    end
    if force then
        target:add(kind, flags, {force = true})
        return
    end
    target:add(kind, flags)
end

--- 批量追加构建参数到目标
---@param target target 目标对象
---@param flags table|nil 参数集合（cflags/cxflags/asflags/ldflags）
---@param options table|nil 选项（force/include_ldflags）
---@return nil
function add_target_flag_set(target, flags, options)
    if not flags then
        return
    end
    local force = options and options.force or false
    local include_ldflags = options and options.include_ldflags or false
    add_target_flags(target, "cflags", flags.cflags, force)
    add_target_flags(target, "cxflags", flags.cxflags, force)
    add_target_flags(target, "asflags", flags.asflags, force)
    if include_ldflags then
        add_target_flags(target, "ldflags", flags.ldflags, force)
    end
end

--- 校验工具链 flags 的结构与内容
---@param flags table|nil 工具链 flags 结构
---@return nil
function validate_flags(flags)
    if not flags then
        return
    end
    if type(flags) ~= "table" then
        raise("toolchain flags invalid: not a table")
    end
    local valid_keys = {
        common = true,
        cflags = true,
        cxflags = true,
        asflags = true,
        ldflags = true,
    }
    for key, list in pairs(flags) do
        if not valid_keys[key] then
            raise("toolchain flags invalid key: " .. tostring(key))
        end
        if type(list) ~= "table" then
            raise("toolchain flags invalid: " .. tostring(key))
        end
        for _, flag in ipairs(list) do
            if type(flag) ~= "string" then
                raise("toolchain flags invalid item: " .. tostring(key))
            end
        end
    end
end

--- 获取工具链 flags（包含 common 合并）
---@param toolchain_config ToolchainsConfig 工具链配置表
---@param toolchain_name string 工具链名称
---@return table flags 参数表（cflags/cxflags/asflags/ldflags）
function get_flags(toolchain_config, toolchain_name)
    if not toolchain_name or toolchain_name == "" then
        raise("toolchain not set")
    end
    local entry = toolchain_config.toolchains
        and toolchain_config.toolchains[toolchain_name]
        or nil
    if not entry then
        raise("toolchain not supported: " .. toolchain_name)
    end
    validate_flags(entry.flags)
    local flags = {
        cflags = {},
        cxflags = {},
        asflags = {},
        ldflags = {},
    }
    --- 追加参数列表
    ---@param dst table 目标列表
    ---@param src table|nil 源列表
    ---@return nil
    local function append(dst, src)
        for _, item in ipairs(src or {}) do
            dst[#dst + 1] = item
        end
    end
    append(flags.cflags, entry.flags and entry.flags.common)
    append(flags.cxflags, entry.flags and entry.flags.common)
    append(flags.asflags, entry.flags and entry.flags.common)
    append(flags.ldflags, entry.flags and entry.flags.common)

    append(flags.cflags, entry.flags and entry.flags.cflags)
    append(flags.cxflags, entry.flags and entry.flags.cxflags)
    append(flags.asflags, entry.flags and entry.flags.asflags)
    append(flags.ldflags, entry.flags and entry.flags.ldflags)
    return flags
end

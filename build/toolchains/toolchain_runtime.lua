--- @file oh_my_robot/build/toolchains/toolchain_runtime.lua
--- @brief 运行时行为映射（toolchain + semihosting）
--- @details 统一解析 semihosting 策略，并返回需要注入的源文件与参数。

local om_root = path.join(os.scriptdir(), "..", "..")
local runtime_root = path.join(om_root, "build", "runtime")

--- 构造空 flags 集合
---@return table flags
local function new_empty_flags()
    return {
        cflags = {},
        cxflags = {},
        asflags = {},
        ldflags = {},
    }
end

--- 构造运行时注入信息
---@param flags table|nil 参数集合
---@param sources string[]|nil 源文件集合
---@return table payload
local function make_payload(flags, sources)
    return {
        flags = flags or new_empty_flags(),
        sources = sources or {},
    }
end

--- 解析 semihosting 模式
---@param mode string|nil 模式（off/on）
---@return string normalized_mode
function resolve_semihosting_mode(mode)
    local normalized_mode = mode or "off"
    if normalized_mode ~= "off" and normalized_mode ~= "on" then
        raise("semihosting invalid: " .. tostring(normalized_mode))
    end
    return normalized_mode
end

--- armclang 运行时映射
---@param semihosting_mode string 规范化模式
---@return table payload
local function resolve_armclang_payload(semihosting_mode)
    if semihosting_mode == "on" then
        return make_payload(new_empty_flags(), {})
    end
    local stub_source = path.join(runtime_root, "armclang", "semihost_stub.c")
    if not os.isfile(stub_source) then
        raise("armclang semihost stub missing: " .. stub_source)
    end
    return make_payload(new_empty_flags(), {stub_source})
end

local runtime_resolvers = {
    armclang = resolve_armclang_payload,
}

--- 解析运行时注入信息
---@param toolchain_name string|nil 工具链名称
---@param semihosting_mode string|nil semihosting 模式
---@return table payload
function resolve_runtime_payload(toolchain_name, semihosting_mode)
    local normalized_mode = resolve_semihosting_mode(semihosting_mode)
    local resolver = toolchain_name and runtime_resolvers[toolchain_name] or nil
    if not resolver then
        return make_payload(new_empty_flags(), {})
    end
    return resolver(normalized_mode)
end

return {
    resolve_semihosting_mode = resolve_semihosting_mode,
    resolve_runtime_payload = resolve_runtime_payload,
}

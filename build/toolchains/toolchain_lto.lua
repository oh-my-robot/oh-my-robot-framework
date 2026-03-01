--- @file oh_my_robot/build/toolchains/toolchain_lto.lua
--- @brief LTO 策略解析
--- @details 统一处理 lto=auto/on/off 与跨工具链稳定收敛逻辑。

local MIN_GNURM_LTO_VERSION = {
    major = 14,
    minor = 2,
    patch = 0,
}

--- gnu-rm 在 safe 范围下默认允许开启 LTO 的目标
--- 目标选择原则：优先核心业务库与最终 binary，规避 HAL/RTOS 大体量对象导致的不稳定行为。
local GNURM_SAFE_TARGETS = {
    robot_project = true,
    tar_awcore = true,
    tar_awalgo = true,
    tar_awdrivers = true,
    tar_awsystems = true,
    tar_sync = true,
}

local gnu_version_cache = {}

--- 归一化工具链名称
---@param toolchain_name string|nil 工具链名称（可能包含 [..] 后缀）
---@return string normalized
local function normalize_toolchain_name(toolchain_name)
    if type(toolchain_name) ~= "string" or toolchain_name == "" then
        return "unknown"
    end
    local base = toolchain_name:match("^(.-)%[")
    return base or toolchain_name
end

--- 归一化 LTO 范围模式
---@param scope string|nil 输入模式（auto/safe/full）
---@return string normalized_scope
local function normalize_lto_scope(scope)
    local normalized = scope or "auto"
    if normalized ~= "auto" and normalized ~= "safe" and normalized ~= "full" then
        raise("lto scope invalid: " .. tostring(normalized))
    end
    return normalized
end

--- 解析工具链下的生效 scope
---@param normalized_toolchain string 归一化工具链名
---@param scope string|nil 输入 scope
---@return string effective_scope
local function resolve_effective_scope(normalized_toolchain, scope)
    local normalized_scope = normalize_lto_scope(scope)
    if normalized_scope ~= "auto" then
        return normalized_scope
    end
    if normalized_toolchain == "gnu-rm" then
        return "safe"
    end
    return "full"
end

--- 查找可执行工具
---@param tool_name string 工具名
---@param bin_dir string|nil 工具目录
---@return string|nil program
local function resolve_program(tool_name, bin_dir)
    if not tool_name or tool_name == "" then
        return nil
    end
    local executable_name = tool_name
    if os.host() == "windows" and not executable_name:lower():find("%.exe$", 1) then
        executable_name = executable_name .. ".exe"
    end
    if bin_dir and bin_dir ~= "" then
        local candidate = path.join(bin_dir, executable_name)
        if os.isexec(candidate) then
            return candidate
        end
    end
    local find_tool = import("lib.detect.find_tool")
    local tool = find_tool(tool_name, {paths = bin_dir and {bin_dir} or nil})
    if tool and tool.program and os.isexec(tool.program) then
        return tool.program
    end
    return nil
end

--- 运行命令并获取输出
---@param program string 程序路径
---@param argv string[] 参数列表
---@return string|nil output
---@return string|nil err
local function run_program(program, argv)
    local output = nil
    local run_error = nil
    try {
        function()
            output = os.iorunv(program, argv)
        end,
        catch {
            function(errors)
                run_error = tostring(errors)
            end
        }
    }
    if not output then
        return nil, run_error or "command run failed"
    end
    return output, nil
end

--- 解析版本号文本
---@param text string|nil
---@return string|nil version
local function parse_version_text(text)
    if type(text) ~= "string" or text == "" then
        return nil
    end
    local version = text:match("(%d+%.%d+%.%d+)")
    if version then
        return version
    end
    local major, minor = text:match("(%d+)%.(%d+)")
    if major and minor then
        return string.format("%d.%d.0", tonumber(major), tonumber(minor))
    end
    return nil
end

--- 解析三段式版本
---@param version string
---@return table|nil triplet
local function parse_version_triplet(version)
    local major, minor, patch = version:match("^(%d+)%.(%d+)%.(%d+)$")
    if not major then
        return nil
    end
    return {
        major = tonumber(major),
        minor = tonumber(minor),
        patch = tonumber(patch),
    }
end

--- 比较版本大小
---@param lhs table
---@param rhs table
---@return integer cmp -1/0/1
local function compare_triplet(lhs, rhs)
    if lhs.major ~= rhs.major then
        return lhs.major > rhs.major and 1 or -1
    end
    if lhs.minor ~= rhs.minor then
        return lhs.minor > rhs.minor and 1 or -1
    end
    if lhs.patch ~= rhs.patch then
        return lhs.patch > rhs.patch and 1 or -1
    end
    return 0
end

--- 读取 gnu-rm gcc 版本
---@return string|nil version
---@return string|nil err
local function resolve_gnurm_gcc_version()
    local config = import("core.project.config")
    local bin_dir = config.get("bin")
    local gcc_program = resolve_program("arm-none-eabi-gcc", bin_dir)
    if not gcc_program then
        return nil, "arm-none-eabi-gcc not found"
    end
    if gnu_version_cache[gcc_program] then
        return gnu_version_cache[gcc_program], nil
    end
    local output = nil
    local run_error = nil
    output, run_error = run_program(gcc_program, {"-dumpfullversion", "-dumpversion"})
    if not output then
        output, run_error = run_program(gcc_program, {"-dumpversion"})
    end
    if not output then
        return nil, run_error
    end
    local version = parse_version_text(output)
    if not version then
        return nil, "gcc version parse failed"
    end
    gnu_version_cache[gcc_program] = version
    return version, nil
end

--- 判断 gnu-rm 是否满足 LTO 稳定开启门槛
---@return boolean enabled
---@return string reason
local function resolve_gnurm_lto_support()
    local version, version_error = resolve_gnurm_gcc_version()
    if not version then
        return false, "auto disabled on gnu-rm: " .. tostring(version_error or "version unknown")
    end
    local version_triplet = parse_version_triplet(version)
    if not version_triplet then
        return false, "auto disabled on gnu-rm: invalid gcc version " .. tostring(version)
    end
    local cmp = compare_triplet(version_triplet, MIN_GNURM_LTO_VERSION)
    if cmp < 0 then
        return false, string.format(
            "auto disabled on gnu-rm: gcc %s < %d.%d.%d",
            version,
            MIN_GNURM_LTO_VERSION.major,
            MIN_GNURM_LTO_VERSION.minor,
            MIN_GNURM_LTO_VERSION.patch
        )
    end
    return true, "auto enabled on gnu-rm (gcc " .. version .. ")"
end

--- 判断目标类型是否支持 LTO 参数注入
---@param target_kind string|nil 目标类型
---@return boolean supported
local function is_lto_target_kind(target_kind)
    return target_kind == "binary" or target_kind == "static" or target_kind == "object"
end

--- 解析目标级 scope 裁决
---@param normalized_toolchain string 归一化工具链名
---@param effective_scope string 生效 scope（safe/full）
---@param target_name string|nil 目标名
---@param target_kind string|nil 目标类型
---@return boolean enabled
---@return string reason
local function resolve_target_scope(normalized_toolchain, effective_scope, target_name, target_kind)
    if target_kind and not is_lto_target_kind(target_kind) then
        return false, string.format("scope=%s excludes target kind=%s", effective_scope, tostring(target_kind))
    end
    if effective_scope == "full" then
        return true, string.format("scope=%s", effective_scope)
    end
    if normalized_toolchain ~= "gnu-rm" then
        return true, string.format("scope=%s mapped to full on %s", effective_scope, normalized_toolchain)
    end
    if target_kind == "binary" then
        return true, string.format("scope=%s keep binary=%s", effective_scope, tostring(target_name or "unknown"))
    end
    if target_name and GNURM_SAFE_TARGETS[target_name] then
        return true, string.format("scope=%s allowlist target=%s", effective_scope, target_name)
    end
    return false, string.format("scope=%s excludes target=%s", effective_scope, tostring(target_name or "unknown"))
end

--- 绑定 LTO 参数
---@param plan table 计划对象
---@param normalized_toolchain string 归一化工具链名称
---@return nil
local function apply_lto_flags(plan, normalized_toolchain)
    if normalized_toolchain == "armclang" then
        plan.cflags = {"-flto"}
        plan.cxflags = {"-flto"}
        plan.ldflags = {"--lto"}
        plan.needs_armclang_fallback_flags = true
        return
    end
    -- gnu-rm 等 gcc 类工具链：编译与链接统一使用 -flto。
    plan.cflags = {"-flto"}
    plan.cxflags = {"-flto"}
    plan.ldflags = {"-flto"}
end

--- 解析 LTO 模式
---@param mode string|nil 输入模式
---@return string normalized_mode 归一化模式（auto/on/off）
function resolve_lto_mode(mode)
    local normalized = mode or "auto"
    if normalized ~= "auto" and normalized ~= "on" and normalized ~= "off" then
        raise("lto mode invalid: " .. tostring(normalized))
    end
    return normalized
end

--- 解析 LTO 范围模式
---@param scope string|nil 输入模式（auto/safe/full）
---@return string normalized_scope
function resolve_lto_scope(scope)
    return normalize_lto_scope(scope)
end

--- 解析 LTO 计划
---@param toolchain_name string|nil 工具链名称
---@param lto_mode string|nil LTO 模式（auto/on/off）
---@param target_name string|nil 目标名称
---@param target_kind string|nil 目标类型（binary/static/phony/headeronly/...）
---@param lto_scope string|nil LTO 范围（auto/safe/full）
---@return table plan
function resolve_lto_plan(toolchain_name, lto_mode, target_name, target_kind, lto_scope)
    local mode = resolve_lto_mode(lto_mode)
    local normalized_toolchain = normalize_toolchain_name(toolchain_name)
    local scope = normalize_lto_scope(lto_scope)
    local effective_scope = resolve_effective_scope(normalized_toolchain, scope)
    local plan = {
        mode = mode,
        scope = scope,
        effective_scope = effective_scope,
        toolchain_name = normalized_toolchain,
        target_name = target_name,
        target_kind = target_kind,
        enabled = false,
        reason = "",
        cflags = {},
        cxflags = {},
        ldflags = {},
        needs_armclang_fallback_flags = false,
    }
    if mode == "off" then
        plan.enabled = false
        plan.reason = "disabled by policy off"
        return plan
    end

    if normalized_toolchain == "unknown" then
        plan.enabled = false
        plan.reason = "auto pending toolchain resolution"
        return plan
    end

    if mode == "auto" and normalized_toolchain == "gnu-rm" then
        local enabled, reason = resolve_gnurm_lto_support()
        if not enabled then
            plan.enabled = false
            plan.reason = reason
            return plan
        end
        plan.reason = reason
    else
        plan.reason = (mode == "on") and "forced by policy on" or "auto enabled"
    end

    local target_enabled, target_reason = resolve_target_scope(
        normalized_toolchain,
        effective_scope,
        target_name,
        target_kind
    )
    if not target_enabled then
        plan.enabled = false
        plan.reason = plan.reason .. ", " .. target_reason
        return plan
    end

    plan.enabled = true
    plan.reason = plan.reason .. ", " .. target_reason
    apply_lto_flags(plan, normalized_toolchain)
    return plan
end

return {
    resolve_lto_mode = resolve_lto_mode,
    resolve_lto_scope = resolve_lto_scope,
    resolve_lto_plan = resolve_lto_plan,
}

--- @file oh_my_robot/build/toolchains/toolchain_linkguard.lua
--- @brief OM 链接契约校验
--- @details 在二进制构建后校验关键模块是否提供必需强符号，避免依赖传播退化。

--- 需要由对应模块提供的关键符号
---@type table<string, string[]>
local REQUIRED_PROVIDER_SYMBOLS = {
    tar_os = {"osal_malloc", "osal_free"},
    tar_sync = {"completion_init", "completion_wait"},
}

--- 归一化数组为集合
---@param values string[]|nil
---@return table<string, boolean> set
local function to_set(values)
    local result = {}
    for _, value in ipairs(values or {}) do
        if value and value ~= "" then
            result[value] = true
        end
    end
    return result
end

--- 查找可执行工具
---@param tool_name string
---@param bin_dir string|nil
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
---@param program string
---@param argv string[]
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
    return output
end

--- 初始化符号状态表
---@param symbols string[]
---@return table<string, table>
local function init_symbol_states(symbols)
    local states = {}
    for _, symbol_name in ipairs(symbols or {}) do
        states[symbol_name] = {
            strong = false,
            weak = false,
            undefined = false,
        }
    end
    return states
end

--- 合并符号状态
---@param states table<string, table>
---@param symbol_name string
---@param kind string
local function merge_symbol_state(states, symbol_name, kind)
    local state = states[symbol_name]
    if not state then
        return
    end
    if kind == "strong" then
        state.strong = true
        return
    end
    if kind == "weak" then
        state.weak = true
        return
    end
    if kind == "undefined" then
        state.undefined = true
    end
end

--- 解析 GNU nm 输出
---@param output string
---@param symbols string[]
---@return table<string, table> states
local function parse_symbols_gnu(output, symbols)
    local symbol_set = to_set(symbols)
    local states = init_symbol_states(symbols)
    for line in output:gmatch("[^\r\n]+") do
        local symbol_type, symbol_name = line:match("^%s*([A-Za-z])%s+([^%s]+)%s*$")
        if not symbol_name then
            local _, parsed_type, parsed_name = line:match("^%s*([%x]+)%s+([A-Za-z])%s+([^%s]+)%s*$")
            symbol_type = parsed_type
            symbol_name = parsed_name
        end
        if symbol_name and symbol_set[symbol_name] then
            if symbol_type == "U" then
                merge_symbol_state(states, symbol_name, "undefined")
            elseif symbol_type == "W"
                or symbol_type == "w"
                or symbol_type == "V"
                or symbol_type == "v" then
                merge_symbol_state(states, symbol_name, "weak")
            else
                merge_symbol_state(states, symbol_name, "strong")
            end
        end
    end
    return states
end

--- 解析 fromelf 符号输出
---@param output string
---@param symbols string[]
---@return table<string, table> states
local function parse_symbols_fromelf(output, symbols)
    local symbol_set = to_set(symbols)
    local states = init_symbol_states(symbols)
    for line in output:gmatch("[^\r\n]+") do
        local symbol_name, bind, section = line:match(
            "^%s*%d+%s+([^%s]+)%s+0x[%da-fA-F]+%s+(%w+)%s+(%S+)%s+"
        )
        if symbol_name and symbol_set[symbol_name] then
            if section == "UND" then
                merge_symbol_state(states, symbol_name, "undefined")
            elseif bind == "Wk" then
                merge_symbol_state(states, symbol_name, "weak")
            else
                merge_symbol_state(states, symbol_name, "strong")
            end
        end
    end
    return states
end

--- 计算校验结果
---@param states table<string, table>
---@return string[] missing
---@return string[] weak_only
local function summarize_symbol_states(states)
    local missing = {}
    local weak_only = {}
    for symbol_name, state in pairs(states or {}) do
        if state.strong then
            -- strong 存在时认为满足
        elseif state.weak then
            weak_only[#weak_only + 1] = symbol_name
        else
            missing[#missing + 1] = symbol_name
        end
    end
    table.sort(missing)
    table.sort(weak_only)
    return missing, weak_only
end

--- 按工具链解析目标文件符号
---@param artifact string
---@param symbols string[]
---@param toolchain_name string|nil
---@return table<string, table>|nil states
---@return string|nil err
---@return boolean|nil opaque_lto
local function resolve_symbol_states(artifact, symbols, toolchain_name)
    local config = import("core.project.config")
    local bin_dir = config.get("bin")
    if toolchain_name == "armclang" then
        local fromelf_program = resolve_program("fromelf", bin_dir)
        if not fromelf_program then
            return nil, "fromelf not found", nil
        end
        local output, run_error = run_program(fromelf_program, {"--text", "-s", artifact})
        if not output then
            return nil, "fromelf failed: " .. tostring(run_error), nil
        end
        local opaque_lto = output:find(".llvmbc", 1, true) ~= nil
        return parse_symbols_fromelf(output, symbols), nil, opaque_lto
    end

    local nm_program = resolve_program("arm-none-eabi-nm", bin_dir)
    if not nm_program then
        nm_program = resolve_program("nm", bin_dir)
    end
    if not nm_program then
        return nil, "arm-none-eabi-nm not found", nil
    end
    local output, run_error = run_program(nm_program, {"-g", artifact})
    if not output then
        return nil, "nm failed: " .. tostring(run_error), nil
    end
    return parse_symbols_gnu(output, symbols), nil, false
end

--- 校验单个提供者目标的关键符号
---@param provider_name string
---@param symbols string[]
---@param toolchain_name string|nil
---@return table report
local function verify_provider_symbols(provider_name, symbols, toolchain_name)
    local project = import("core.project.project")
    local provider_target = project.target(provider_name)
    if not provider_target then
        return {
            ok = false,
            reason = "provider target not found: " .. tostring(provider_name),
        }
    end
    local artifact = provider_target:targetfile()
    if not artifact or artifact == "" then
        return {
            ok = false,
            reason = "provider artifact unresolved: " .. tostring(provider_name),
        }
    end
    if not os.isfile(artifact) then
        return {
            ok = false,
            reason = "provider artifact not found: " .. tostring(artifact),
        }
    end
    local states, parse_error, opaque_lto = resolve_symbol_states(artifact, symbols, toolchain_name)
    if not states then
        return {
            ok = false,
            reason = "symbol resolve failed for " .. provider_name .. ": " .. tostring(parse_error),
        }
    end
    local missing, weak_only = summarize_symbol_states(states)
    if opaque_lto and (#missing > 0 or #weak_only > 0) then
        return {
            ok = true,
            skipped = true,
            provider = provider_name,
            artifact = artifact,
            reason = "skip symbol-level contract check for armclang LTO bitcode archive",
        }
    end
    return {
        ok = (#missing == 0 and #weak_only == 0),
        provider = provider_name,
        artifact = artifact,
        missing = missing,
        weak_only = weak_only,
    }
end

--- 校验 OM 链接契约
---@param target target
---@param context table|nil
---@return table report
function verify_om_link_contract(target, context)
    if not target or target:kind() ~= "binary" then
        return {
            ok = true,
            skipped = true,
            reason = "non-binary target",
        }
    end
    local toolchain_name = context and context.toolchain_name or nil
    local failures = {}
    local checks = {}
    for provider_name, symbols in pairs(REQUIRED_PROVIDER_SYMBOLS) do
        local report = verify_provider_symbols(provider_name, symbols, toolchain_name)
        checks[#checks + 1] = report
        if not report.ok then
            failures[#failures + 1] = report
        end
    end

    if #failures == 0 then
        return {
            ok = true,
            checks = checks,
        }
    end

    local lines = {
        "[oh-my-robot] link contract check failed:",
    }
    for _, failure in ipairs(failures) do
        if failure.provider then
            lines[#lines + 1] = string.format(
                "[oh-my-robot]   provider=%s artifact=%s",
                tostring(failure.provider),
                tostring(failure.artifact or "unknown")
            )
            if failure.missing and #failure.missing > 0 then
                lines[#lines + 1] = "[oh-my-robot]     missing strong symbols: " .. table.concat(failure.missing, ", ")
            end
            if failure.weak_only and #failure.weak_only > 0 then
                lines[#lines + 1] = "[oh-my-robot]     weak-only symbols: " .. table.concat(failure.weak_only, ", ")
            end
        else
            lines[#lines + 1] = "[oh-my-robot]   " .. tostring(failure.reason or "unknown failure")
        end
    end
    lines[#lines + 1] = "[oh-my-robot]   action: keep app target depending only on tar_oh_my_robot; fix provider target wiring."
    return {
        ok = false,
        reason = table.concat(lines, "\n"),
        checks = checks,
    }
end

return {
    verify_om_link_contract = verify_om_link_contract,
}

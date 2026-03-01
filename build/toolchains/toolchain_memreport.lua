--- @file oh_my_robot/build/toolchains/toolchain_memreport.lua
--- @brief 构建后内存分布报告
--- @details 统一解析 ELF 用量与链接脚本容量，输出 FLASH/RAM 分布摘要。

--- 去掉首尾空白
---@param value string|nil 原始字符串
---@return string trimmed
local function trim(value)
    return (value or ""):gsub("^%s+", ""):gsub("%s+$", "")
end

--- 解析容量字面量（支持 0x/10 进制与 K/M/G 后缀）
---@param token string|nil 字面量
---@return integer|nil bytes 字节数
local function parse_size_literal(token)
    local raw = trim(token)
    if raw == "" then
        return nil
    end
    raw = trim(raw:gsub("[;].*$", ""))
    local number_part, suffix = raw:match("^(0[xX]%x+)([kKmMgG]?)$")
    if not number_part then
        number_part, suffix = raw:match("^(%d+)([kKmMgG]?)$")
    end
    if not number_part then
        return nil
    end
    local base_value = tonumber(number_part)
    if not base_value then
        return nil
    end
    local multiplier = 1
    local normalized_suffix = (suffix or ""):upper()
    if normalized_suffix == "K" then
        multiplier = 1024
    elseif normalized_suffix == "M" then
        multiplier = 1024 * 1024
    elseif normalized_suffix == "G" then
        multiplier = 1024 * 1024 * 1024
    end
    return math.floor(base_value * multiplier)
end

--- 判断区域名是否属于 Flash 或 RAM
---@param region_name string 区域名
---@return string|nil kind 区域类型（flash/ram）
local function classify_region_name(region_name)
    local upper = (region_name or ""):upper()
    if upper:find("ROM", 1, true) or upper:find("FLASH", 1, true) then
        return "flash"
    end
    if upper:find("RAM", 1, true)
        or upper:find("IRAM", 1, true)
        or upper:find("SRAM", 1, true)
        or upper:find("CCM", 1, true)
        or upper:find("DTCM", 1, true) then
        return "ram"
    end
    return nil
end

--- 解析 GNU ld 链接脚本容量
---@param content string 链接脚本文本
---@return table|nil totals 容量汇总
---@return string|nil err 错误信息
local function parse_ld_totals(content)
    local flash_total = 0
    local ram_total = 0
    local found_region = false
    for line in content:gmatch("[^\r\n]+") do
        local region_name, attrs, length_expr = line:match(
            "^%s*([%w_]+)%s*%(([^)]*)%)%s*:%s*ORIGIN%s*=%s*[^,]+,%s*LENGTH%s*=%s*([^,%s;]+)"
        )
        if not region_name then
            region_name, length_expr = line:match(
                "^%s*([%w_]+)%s*:%s*ORIGIN%s*=%s*[^,]+,%s*LENGTH%s*=%s*([^,%s;]+)"
            )
        end
        if region_name and length_expr then
            local bytes = parse_size_literal(length_expr)
            if bytes then
                found_region = true
                local kind = classify_region_name(region_name)
                if not kind and attrs then
                    local attrs_upper = attrs:upper()
                    if attrs_upper:find("W", 1, true) then
                        kind = "ram"
                    elseif attrs_upper:find("X", 1, true) then
                        kind = "flash"
                    end
                end
                if kind == "flash" then
                    flash_total = flash_total + bytes
                elseif kind == "ram" then
                    ram_total = ram_total + bytes
                end
            end
        end
    end
    if not found_region then
        return nil, "memory regions not found in ld script"
    end
    if flash_total <= 0 and ram_total <= 0 then
        return nil, "memory totals unresolved from ld script"
    end
    return {
        flash_total = flash_total > 0 and flash_total or nil,
        ram_total = ram_total > 0 and ram_total or nil,
    }
end

--- 解析 Arm scatter 链接脚本容量
---@param content string 链接脚本文本
---@return table|nil totals 容量汇总
---@return string|nil err 错误信息
local function parse_sct_totals(content)
    local entries = {}
    for line in content:gmatch("[^\r\n]+") do
        local region_name, _, size_expr = line:match("^%s*([%w_]+)%s+([^%s]+)%s+([^%s]+)%s*%{")
        if region_name and size_expr then
            local bytes = parse_size_literal(size_expr)
            if bytes then
                entries[#entries + 1] = {
                    name = region_name,
                    size = bytes,
                }
            end
        end
    end
    if #entries == 0 then
        return nil, "memory regions not found in scatter script"
    end
    local flash_total = 0
    local ram_total = 0
    local has_lr_flash = false
    for _, entry in ipairs(entries) do
        local upper = entry.name:upper()
        if upper:find("^LR_") and classify_region_name(upper) == "flash" then
            flash_total = flash_total + entry.size
            has_lr_flash = true
        end
        if upper:find("^RW_") or upper:find("^ZI_") then
            if classify_region_name(upper) == "ram" then
                ram_total = ram_total + entry.size
            end
        end
    end
    if not has_lr_flash then
        for _, entry in ipairs(entries) do
            local upper = entry.name:upper()
            if not upper:find("^ER_") and classify_region_name(upper) == "flash" then
                flash_total = flash_total + entry.size
            end
        end
    end
    if ram_total <= 0 then
        for _, entry in ipairs(entries) do
            if classify_region_name(entry.name) == "ram" then
                ram_total = ram_total + entry.size
            end
        end
    end
    if flash_total <= 0 and ram_total <= 0 then
        return nil, "memory totals unresolved from scatter script"
    end
    return {
        flash_total = flash_total > 0 and flash_total or nil,
        ram_total = ram_total > 0 and ram_total or nil,
    }
end

--- 解析链接脚本容量
---@param linkerscript string 链接脚本路径
---@return table|nil totals 容量汇总
---@return string|nil err 错误信息
local function parse_linkerscript_totals(linkerscript)
    if not linkerscript or linkerscript == "" then
        return nil, "linkerscript not set"
    end
    if not os.isfile(linkerscript) then
        return nil, "linkerscript not found: " .. tostring(linkerscript)
    end
    local content = io.readfile(linkerscript)
    if type(content) ~= "string" or content == "" then
        return nil, "linkerscript read failed: " .. tostring(linkerscript)
    end
    local lower = linkerscript:lower()
    if lower:sub(-3) == ".ld" then
        return parse_ld_totals(content)
    end
    if lower:sub(-4) == ".sct" then
        return parse_sct_totals(content)
    end
    return nil, "unsupported linkerscript extension: " .. tostring(linkerscript)
end

--- 解析 ldflags 中的链接脚本路径
---@param ldflags string[]|nil 链接参数
---@return string|nil linkerscript
local function parse_linkerscript_from_ldflags(ldflags)
    for _, flag in ipairs(ldflags or {}) do
        if type(flag) == "string" then
            local gnu_script = flag:match("^%-T(.+)$")
            if gnu_script and gnu_script ~= "" then
                return gnu_script
            end
            local arm_scatter = flag:match("^%-%-scatter=(.+)$")
            if arm_scatter and arm_scatter ~= "" then
                return arm_scatter
            end
        end
    end
    return nil
end

--- 获取链接脚本路径（优先 target.ldflags，失败后回退 BSP 资产）
---@param target target 目标对象
---@param context table 上下文
---@return string|nil linkerscript
---@return string|nil err 错误信息
local function resolve_linkerscript_path(target, context)
    local candidate = parse_linkerscript_from_ldflags(target and target:get("ldflags") or nil)
    if candidate and candidate ~= "" then
        if os.isfile(candidate) then
            return candidate
        end
        local project_candidate = path.join(os.projectdir(), candidate)
        if os.isfile(project_candidate) then
            return project_candidate
        end
    end
    local linkerscript = nil
    local resolve_error = nil
    try {
        function()
            local om_root = path.join(os.scriptdir(), "..", "..")
            local bsp_root = path.join(om_root, "platform", "bsp")
            local bsp = import("bsp", {rootdir = bsp_root})
            local assets = bsp.get_board_build_assets(context.board_name, context.toolchain_name)
            linkerscript = assets and assets.linkerscript or nil
        end,
        catch {
            function(errors)
                resolve_error = tostring(errors)
            end
        }
    }
    if linkerscript and os.isfile(linkerscript) then
        return linkerscript
    end
    if resolve_error and resolve_error ~= "" then
        return nil, "linkerscript unresolved: " .. resolve_error
    end
    return nil, "linkerscript unresolved"
end

--- 查找可执行工具
---@param tool_name string 工具名
---@param bin_dir string|nil 工具链 bin 目录
---@return string|nil program 可执行路径
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

--- 安全执行外部命令并读取输出
---@param program string 可执行路径
---@param argv string[] 参数列表
---@return string|nil output 输出
---@return string|nil err 错误信息
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

--- 解析 GNU size 输出
---@param targetfile string ELF 路径
---@param bin_dir string|nil 工具链 bin 目录
---@return table|nil usage 用量
---@return string|nil err 错误信息
local function resolve_usage_gnu(targetfile, bin_dir)
    local size_program = resolve_program("arm-none-eabi-size", bin_dir)
    if not size_program then
        return nil, "arm-none-eabi-size not found"
    end
    local output, run_error = run_program(size_program, {targetfile})
    if not output then
        return nil, "arm-none-eabi-size failed: " .. tostring(run_error)
    end
    for line in output:gmatch("[^\r\n]+") do
        local text_value, data_value, bss_value = line:match("^%s*(%d+)%s+(%d+)%s+(%d+)%s+%d+%s+[%da-fA-F]+%s+")
        if text_value then
            local text_size = tonumber(text_value) or 0
            local data_size = tonumber(data_value) or 0
            local bss_size = tonumber(bss_value) or 0
            return {
                flash_used = text_size + data_size,
                ram_used = data_size + bss_size,
            }
        end
    end
    return nil, "unable to parse arm-none-eabi-size output"
end

--- 解析 fromelf 输出
---@param targetfile string ELF 路径
---@param bin_dir string|nil 工具链 bin 目录
---@return table|nil usage 用量
---@return string|nil err 错误信息
local function resolve_usage_armclang(targetfile, bin_dir)
    local fromelf_program = resolve_program("fromelf", bin_dir)
    if not fromelf_program then
        return nil, "fromelf not found"
    end
    local output, run_error = run_program(fromelf_program, {"--text", "-z", targetfile})
    if not output then
        return nil, "fromelf failed: " .. tostring(run_error)
    end
    for line in output:gmatch("[^\r\n]+") do
        local code_value, ro_value, rw_value, zi_value = line:match(
            "^%s*(%d+)%s+(%d+)%s+(%d+)%s+(%d+)%s+%d+%s+%d+%s+ROM Totals"
        )
        if code_value then
            local code_size = tonumber(code_value) or 0
            local ro_size = tonumber(ro_value) or 0
            local rw_size = tonumber(rw_value) or 0
            local zi_size = tonumber(zi_value) or 0
            return {
                flash_used = code_size + ro_size + rw_size,
                ram_used = rw_size + zi_size,
            }
        end
    end
    for line in output:gmatch("[^\r\n]+") do
        local code_value, ro_value, rw_value, zi_value = line:match(
            "^%s*(%d+)%s+(%d+)%s+(%d+)%s+(%d+)%s+%d+%s+%d+%s+.+%.elf.+$"
        )
        if code_value then
            local code_size = tonumber(code_value) or 0
            local ro_size = tonumber(ro_value) or 0
            local rw_size = tonumber(rw_value) or 0
            local zi_size = tonumber(zi_value) or 0
            return {
                flash_used = code_size + ro_size + rw_size,
                ram_used = rw_size + zi_size,
            }
        end
    end
    return nil, "unable to parse fromelf output"
end

--- 解析 ELF 用量（按工具链）
---@param targetfile string ELF 路径
---@param toolchain_name string|nil 工具链名称
---@return table|nil usage 用量
---@return string|nil err 错误信息
local function resolve_usage(targetfile, toolchain_name)
    local config = import("core.project.config")
    local bin_dir = config.get("bin")
    if toolchain_name == "armclang" then
        return resolve_usage_armclang(targetfile, bin_dir)
    end
    if toolchain_name == "gnu-rm" then
        return resolve_usage_gnu(targetfile, bin_dir)
    end
    local usage = resolve_usage_gnu(targetfile, bin_dir)
    if usage then
        return usage
    end
    return resolve_usage_armclang(targetfile, bin_dir)
end

--- 生成构建后内存分布报告
---@param target target 目标对象
---@param context table 上下文
---@return table report 报告数据
function build_memory_distribution_report(target, context)
    local targetfile = target and target:targetfile() or nil
    if not targetfile or targetfile == "" then
        return {
            available = false,
            reason = "target file not found",
        }
    end
    local usage, usage_error = resolve_usage(targetfile, context and context.toolchain_name or nil)
    if not usage then
        return {
            available = false,
            reason = usage_error or "memory usage unresolved",
        }
    end
    local linkerscript, linkerscript_error = resolve_linkerscript_path(target, context or {})
    local totals = nil
    local totals_error = nil
    if linkerscript then
        totals, totals_error = parse_linkerscript_totals(linkerscript)
    else
        totals_error = linkerscript_error
    end
    local reason = nil
    if totals_error and totals_error ~= "" then
        reason = totals_error
    end
    return {
        available = true,
        reason = reason,
        linkerscript = linkerscript,
        flash_used = usage.flash_used,
        ram_used = usage.ram_used,
        flash_total = totals and totals.flash_total or nil,
        ram_total = totals and totals.ram_total or nil,
    }
end

return {
    build_memory_distribution_report = build_memory_distribution_report,
}

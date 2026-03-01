--- @file oh_my_robot/build/toolchains/toolchain_image.lua
--- @brief 工具链镜像转换
--- @details 负责镜像转换器解析与执行。

--- 解析镜像转换工具路径
---@param tool_name string 工具名
---@param bin_dir string|nil 工具 bin 目录
---@return string|nil program 工具可执行路径
local function resolve_image_tool(tool_name, bin_dir)
    if not tool_name or tool_name == "" then
        return nil
    end
    local executable_name = tool_name
    if os.host() == "windows" then
        local lower_name = executable_name:lower()
        if lower_name:sub(-4) ~= ".exe" then
            executable_name = executable_name .. ".exe"
        end
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

--- 获取镜像转换工具配置
---@param toolchain_config ToolchainsConfig 工具链配置表
---@param toolchain_name string 工具链名称
---@return table|nil converters 镜像转换器（hex/bin）
function resolve_image_converters(toolchain_config, toolchain_name)
    if not toolchain_name or toolchain_name == "" then
        raise("toolchain not set")
    end
    local entry = toolchain_config.toolchains
        and toolchain_config.toolchains[toolchain_name]
        or nil
    if not entry then
        raise("toolchain not supported: " .. toolchain_name)
    end
    if not entry.image then
        return nil
    end
    local bin_dir = get_config("bin") or entry.bin
    local converters = {}
    for kind, image in pairs(entry.image) do
        local program = resolve_image_tool(image.tool, bin_dir)
        if not program then
            raise("image tool not found: " .. tostring(image.tool))
        end
        converters[kind] = {
            program = program,
            kind = image.kind,
            format = image.format,
        }
    end
    return converters
end

--- 执行镜像转换
---@param converter table 镜像转换器配置
---@param input string 输入 ELF 文件路径
---@param output string 输出文件路径
---@return nil
function run_image_convert(converter, input, output)
    if not converter then
        return
    end
    if converter.kind == "objcopy" then
        os.runv(converter.program, {"-O", converter.format, input, output})
        return
    end
    if converter.kind == "fromelf" then
        os.runv(converter.program, {"--" .. converter.format, "--output", output, input})
        return
    end
    raise("image tool kind not supported: " .. tostring(converter.kind))
end

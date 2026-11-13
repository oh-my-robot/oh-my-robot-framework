--- @file oh_my_robot/xmake/modules/om_external.lua
--- @brief 外部构建工具调用
--- @details 内存报告与链接契约检查由 tools/omcheck 下的独立脚本承担；
---          本模块只负责拼参数并调用，不解析 ELF/链接脚本/符号表。
local om_root = path.join(os.scriptdir(), "..", "..")

--- 查找 Python 解释器
---@return string program Python 可执行文件
local function resolve_python()
    local find_tool = import("lib.detect.find_tool")
    local python = find_tool("python3") or find_tool("python")
    if not python then
        raise("python3/python not found (required by tools/omcheck)")
    end
    return python.program or python
end

--- 查找外部脚本
---@param name string 脚本文件名
---@return string script 脚本路径
local function resolve_script(name)
    local script = path.join(om_root, "tools", "omcheck", name)
    if not os.isfile(script) then
        raise("external tool not found: " .. script)
    end
    return script
end

--- 读取工具链 bin 目录
---@return string|nil bin_dir 工具链 bin 目录
local function resolve_bin_dir()
    local config = import("core.project.config")
    config.load()
    return config.get("bin")
end

--- 运行外部脚本
---@param name string 脚本文件名
---@param argv string[] 参数列表
---@return boolean ok 是否成功
local function run_script(name, argv)
    local python = resolve_python()
    local script = resolve_script(name)
    local args = {script}
    for _, value in ipairs(argv or {}) do
        args[#args + 1] = value
    end
    local code = os.execv(python, args, {stdout = io.stdout, stderr = io.stderr})
    return code == 0
end

--- 输出内存使用报告（信息性，不阻断构建）
---@param target target 目标对象
---@param context table 构建上下文
---@return nil
function run_mem_report(target, context)
    if target:kind() ~= "binary" then
        return
    end
    local argv = {
        "--elf", target:targetfile(),
        "--toolchain", context.toolchain_name,
    }
    local bin_dir = resolve_bin_dir()
    if bin_dir and bin_dir ~= "" then
        argv[#argv + 1] = "--bin-dir"
        argv[#argv + 1] = bin_dir
    end
    run_script("mem_report.py", argv)
end

--- 校验链接契约（失败时阻断构建）
---@param target target 目标对象
---@param context table 构建上下文
---@return boolean ok 是否通过
---@return string|nil reason 失败原因
function run_link_guard(target, context)
    if target:kind() ~= "binary" then
        return true
    end
    local argv = {
        "--elf", target:targetfile(),
        "--toolchain", context.toolchain_name,
    }
    local bin_dir = resolve_bin_dir()
    if bin_dir and bin_dir ~= "" then
        argv[#argv + 1] = "--bin-dir"
        argv[#argv + 1] = bin_dir
    end
    local project = import("core.project.project")
    for _, name in ipairs({"tar_os", "tar_sync", "tar_awdrivers", "tar_awdevices"}) do
        local provider = project.target(name)
        if provider then
            local artifact = provider:targetfile()
            if artifact and artifact ~= "" then
                argv[#argv + 1] = "--provider"
                argv[#argv + 1] = name .. "=" .. artifact
            end
        end
    end
    if not run_script("link_guard.py", argv) then
        return false, "[oh-my-robot] link contract check failed"
    end
    return true
end

return {
    run_mem_report = run_mem_report,
    run_link_guard = run_link_guard,
}

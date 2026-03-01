--- @file oh_my_robot/build/modules/om_version.lua
--- @brief Framework 版本字符串解析模块
--- @details 统一从 Git 元信息生成 FRAMEWORK_VERSION，格式：
---          <tag-or-v0.0.0>+g<sha>[-dirty]

local cached_framework_version = nil

--- 去除首尾空白
---@param value string|nil
---@return string|nil trimmed
local function trim(value)
    if type(value) ~= "string" then
        return nil
    end
    return (value:gsub("^%s+", ""):gsub("%s+$", ""))
end

--- 运行 git 命令并返回原始输出
---@param argv string[] 参数列表
---@return string|nil output
---@return string|nil err
local function run_git(argv)
    local output = nil
    local run_error = nil
    local project_root = os.projectdir()
    local command_argv = {"-C", project_root}
    for _, argument in ipairs(argv or {}) do
        command_argv[#command_argv + 1] = argument
    end
    try {
        function()
            output = os.iorunv("git", command_argv)
        end,
        catch {
            function(errors)
                run_error = tostring(errors)
            end
        }
    }
    if not output then
        return nil, run_error or "git command run failed"
    end
    return output, nil
end

--- 读取当前 HEAD 的短 SHA
---@return string|nil sha
local function resolve_short_sha()
    local output, _ = run_git({"rev-parse", "--short=8", "HEAD"})
    local sha = trim(output)
    if not sha or sha == "" then
        return nil
    end
    return sha
end

--- 读取最近的版本标签（v*）
---@return string|nil tag
local function resolve_latest_tag()
    local output, _ = run_git({"describe", "--tags", "--match", "v*", "--abbrev=0"})
    local tag = trim(output)
    if not tag or tag == "" then
        return nil
    end
    return tag
end

--- 判断当前工作区是否 dirty（仅看已跟踪文件）
---@return boolean dirty
local function is_worktree_dirty()
    local output, run_error = run_git({"status", "--porcelain", "--untracked-files=no"})
    if not output then
        -- 无法读取状态时，不阻断构建；按 clean 处理。
        return false, run_error
    end
    local lines = trim(output)
    return (lines ~= nil and lines ~= "")
end

--- 解析 FRAMEWORK_VERSION
---@return string version
function resolve_framework_version()
    if cached_framework_version then
        return cached_framework_version
    end

    local short_sha = resolve_short_sha()
    if not short_sha then
        cached_framework_version = "v0.0.0+gunknown-nogit"
        return cached_framework_version
    end

    local latest_tag = resolve_latest_tag() or "v0.0.0"
    local dirty = is_worktree_dirty()
    local version = latest_tag .. "+g" .. short_sha
    if dirty then
        version = version .. "-dirty"
    end

    cached_framework_version = version
    return cached_framework_version
end

return {
    resolve_framework_version = resolve_framework_version,
}


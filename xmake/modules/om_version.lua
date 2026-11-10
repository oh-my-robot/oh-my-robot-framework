--- @file oh_my_robot/xmake/modules/om_version.lua
--- @brief Framework 版本字符串解析模块
--- @details 从 Git 元信息生成 FRAMEWORK_VERSION：
---          <tag-or-v0.0.0>+g<sha>[-dirty]

local cached_version = nil

--- 运行 git 命令并返回去除首尾空白的输出
---@param args string[] git 参数
---@return string|nil output 输出
local function run_git(args)
    local framework_root = path.directory(path.directory(os.scriptdir()))
    local output = nil
    try {
        function()
            output = os.iorunv("git", table.join({"-C", framework_root}, args))
        end,
        catch {
            function()
                output = nil
            end
        }
    }
    if not output then
        return nil
    end
    return (output:gsub("^%s+", ""):gsub("%s+$", ""))
end

--- 解析 FRAMEWORK_VERSION
---@return string version
function resolve_framework_version()
    if cached_version then
        return cached_version
    end
    local sha = run_git({"rev-parse", "--short=8", "HEAD"})
    if not sha or sha == "" then
        cached_version = "v0.0.0+gunknown-nogit"
        return cached_version
    end
    local tag = run_git({"describe", "--tags", "--match", "v*", "--abbrev=0"}) or "v0.0.0"
    local status = run_git({"status", "--porcelain", "--untracked-files=no"})
    cached_version = tag .. "+g" .. sha
    if status and status ~= "" then
        cached_version = cached_version .. "-dirty"
    end
    return cached_version
end

return {
    resolve_framework_version = resolve_framework_version,
}

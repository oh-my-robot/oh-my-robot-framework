--
-- ws_root.lua —— 工作区根目录发现脚本
-- 用法：xmake lua ws_root.lua
local function find_root(start)
    local dir = start
    while dir do
        local marker = path.join(dir, ".oh-my-robot-workspace")
        if os.isfile(marker) then
            return dir
        end
        local parent = dir:match("^(.*)[/\\][^/\\]+$")
        if not parent or parent == dir then break end
        dir = parent
    end
    return nil
end

local root = find_root(os.curdir())
if root then
    print(root)
else
    print("")
end

--- @file oh_my_robot/platform/bsp/boards/rm-a-board/xmake.lua
--- @brief rm-a-board BSP 构建脚本
--- @details 当前未提供 rm-a-board 的完整板级实现。
local board_root = os.scriptdir()
local bsp_root = path.join(board_root, "..", "..")
local om_root = path.join(bsp_root, "..", "..")
local modules_root = path.join(om_root, "build", "modules")
local oh_my_robot = import("oh-my-robot", {rootdir = modules_root})
local context = oh_my_robot.get_context()

if context.board_name == "rm-a-board" then
    raise("rm-a-board not implemented: missing board data and sources.")
end

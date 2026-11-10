--- @file oh_my_robot/xmake/rules/selfreg.lua
--- @brief OM 自注册模块直接注入规则
--- @details 把框架自注册模块源（lib/systems/src、lib/services/src 下的全部 .c）直接编译进
---          binary，保证 OM_INIT 自注册 entry 不被静态库按需抽取丢弃。
---
---          原理：直接编译产生的 .o 先于静态归档被链接，已定义模块符号；链接器随后处理
---          归档时，该成员因符号已解析而不再被抽取，故无需从静态库剔除源文件，也不会产生
---          重复符号。这是 Linux/Zephyr "全对象链接（obj-y）" 目标在 XMake 静态归档模型下的
---          等价实现（XMake 无干净的 per-dep --whole-archive 入口，见 ADR-0010）。
---
---          用法：binary 目标除 context/board_assets/image_convert 外，再加本规则：
---            add_rules("oh_my_robot.context", "oh_my_robot.board_assets",
---                      "oh_my_robot.image_convert", "oh_my_robot.selfreg")
---
---          约定：lib/systems/src 与 lib/services/src 视为"自注册模块目录"，其下所有 .c
---          自动注入；lib/source/core/om_main.c 为框架默认 main（弱符号），同样经本规则
---          注入 binary——用户不写 main 时启动文件直接进入启动编排，用户定义强 main 则
---          自动覆盖（逃生通道，见 ADR-0013）。om_framework_main=off 时跳过注入（宿主工程
---          自带 main / 双弱符号规避）。

local om_root = path.join(os.scriptdir(), "..", "..")

--- 自注册模块源目录（相对框架根），其下所有 .c 自动注入 binary。
local SELFREG_DIRS = {
    "lib/systems/src",
    "lib/services/src",
}

--- 框架自带自注册源文件（相对框架根），逐一注入 binary。
local SELFREG_FILES = {
    "lib/source/core/om_main.c",
    -- 第三方库（BSD 许可——保留 LICENSE 于同目录）；services 后端引用
    "third_party/segger-rtt/SEGGER_RTT.c",
}

rule("oh_my_robot.selfreg")
    --- 配置阶段把自注册模块源直接挂到 binary
    ---@param target target 二进制目标
    on_config(function(target)
        if target:kind() ~= "binary" then
            return
        end
        local config = import("core.project.config")
        config.load()
        local includedirs = {
            path.join(om_root, "lib/include"),
            path.join(om_root, "lib/systems/include"),
            path.join(om_root, "lib/services/include"),
            path.join(om_root, "third_party/segger-rtt"), -- SEGGER_RTT 头（服务后端引用；第三方库目录）
        }
        for _, dir in ipairs(SELFREG_DIRS) do
            local files = os.files(path.join(om_root, dir, "**.c"))
            if files then
                for _, f in ipairs(files) do
                    target:add("files", f, { includedirs = includedirs })
                end
            end
        end
        -- 框架默认 main 注入（开关见 xmake/config/options.lua）
        if config.get("om_framework_main") ~= "off" then
            for _, file in ipairs(SELFREG_FILES) do
                target:add("files", path.join(om_root, file), { includedirs = includedirs })
            end
        end
    end)
rule_end()

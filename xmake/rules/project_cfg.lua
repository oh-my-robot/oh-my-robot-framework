--- @file oh_my_robot/xmake/rules/project_cfg.lua
--- @brief 工程配置片段自动发现规则 + 配置变更感知
--- @details ①检测工程 cfg/ 目录中的配置片段并注入编译上下文：
---   cfg/om_appcfg.h                        → 框架层覆写（注入 -DOM_USE_APPCFG）
---   cfg/boards/<board>/om_boardcfg.h       → 板层覆写（注入 -DOM_USE_BOARDCFG）
---           片段不存在 = 零成本（不注入任何内容）；<board> 取当前构建板
---           （context.board_name——与 preset board= 一致）。
--- ②配置变更感知：每个配置再生（xmake f）向 <project>/.xmake/om_cfg_state
---   写入当前配置状态摘要，并作为所有目标的 depfile——配置/旗标变更后
---   构建期自动重编（根治"旗标变更不重编/陈旧归档成员"——实测备忘）。
---   命名/职责/优先级契约见 ADR-0017 (project_config_layering)。
--- 用法：各目标在规则组中显式添加 "oh_my_robot.project_cfg"（binary 与框架库目标
---       lib/xmake.lua tar_* 均挂接——注入全编译单元一致，见 2026-09-05 审计：
---       binary-only 注入致驱动库源（如 log_serial_backend.c 锋利边守卫）未达
---       appcfg 宏——配置分层一致性缺口）；库目标另挂同款 depfile（同文件状态机）。
local om_root = path.join(os.scriptdir(), "..", "..")
local modules_root = path.join(om_root, "xmake", "modules")

--- 配置状态摘要文件（所有目标共用的配置标记 depfile；lib/xmake.lua 同名路径）
local function cfg_state_file()
    return path.join(os.projectdir(), ".xmake", "om_cfg_state")
end

rule("oh_my_robot.project_cfg")
    --- 配置阶段检测并注入工程配置片段 + 写入配置状态标记
    ---@param target target 目标对象
    on_config(function(target)
        local oh_my_robot = import("oh-my-robot", {rootdir = modules_root})
        local context = oh_my_robot.get_context()
        local project_dir = os.projectdir()
        local appcfg = path.join(project_dir, "cfg", "om_appcfg.h")
        local has_appcfg = os.isfile(appcfg)
        local board_name = context.board_name
        local boardcfg = board_name and path.join(project_dir, "cfg", "boards", board_name, "om_boardcfg.h") or nil
        local has_boardcfg = boardcfg and os.isfile(boardcfg) or false

        -- 配置状态摘要：内容 = 注入决定性状态（含片段文件原文——值变更即感知）；
        -- 仅当内容变化时重写（mtime 变化→depfile 触发重编）
        local function cfg_content(p)
            return os.isfile(p) and io.readfile(p) or ""
        end
        local state = string.format("board=%s\n== appcfg ==\n%s\n== boardcfg ==\n%s",
                                    tostring(board_name), cfg_content(appcfg), cfg_content(boardcfg))
        local marker = cfg_state_file()
        local cur = os.isfile(marker) and io.readfile(marker) or nil
        if cur ~= state then
            io.writefile(marker, state)
            -- 内容缓存清理：xmake 编译缓存键=源码内容（不含旗标/包含路径）——配置/换板后
            -- 旧对象仍会命中 → 状态变化时同点清缓存（与 depfile 标记同一触发源）
            -- 存在性守卫：os.rmdir 对不存在目录报错（xmake 3.1 视作致命——CI 实证
            -- exit 255 卡 build job）
            local _cache_dir = path.join(project_dir, ".cache")
            if os.isdir(_cache_dir) then
                os.rmdir(_cache_dir)
            end
            local _build_cache_dir = path.join(project_dir, "build", ".build_cache")
            if os.isdir(_build_cache_dir) then
                os.rmdir(_build_cache_dir)
            end
        end

        -- 注入面向全部目标（含框架库 tar_*）：配置分层（ADR-0017）= 所有编译单元
        -- 一致可见工程配置——binary-only 会让库源（drivers 等）配置不一致
        if has_appcfg then
            target:add("defines", "OM_USE_APPCFG")
            target:add("includedirs", path.join(project_dir, "cfg"))
        end
        if has_boardcfg then
            target:add("defines", "OM_USE_BOARDCFG")
            target:add("includedirs", path.join(project_dir, "cfg", "boards", board_name))
        end
    end)

--- @file oh_my_robot/build/toolchains/xmake.lua
--- @brief OM 工具链注册入口
--- @details 读取工具链数据并在描述域注册可用工具链。
includes("data.lua")

--- @rule oh_my_robot.toolchain_bootstrap
--- @brief 工具链预热规则
--- @details 在加载阶段确保工具链配置已落地。
rule("oh_my_robot.toolchain_bootstrap")
    --- 规则加载阶段准备工具链配置
    on_load(function()
        -- 统一补全工具链配置，保证首轮构建可用
        local bootstrap = import("toolchain_bootstrap", {rootdir = os.scriptdir()})
        bootstrap.ensure_toolchain_ready()
    end)
rule_end()

--- 注册工具链（描述域）
---@return nil
local function register_toolchains()
    local toolchain_config = om_toolchains_data
    for name, entry in pairs(toolchain_config.toolchains or {}) do
        if entry.kind == "custom" then
            local toolchain_name = name
            local toolchain_entry = entry
            toolchain(toolchain_name)
                set_kind("standalone")
                --- 工具链加载回调：设置 sdk/bin 与 toolset
                ---@param toolchain_instance toolchain 工具链对象
                on_load(function(toolchain_instance)
                    -- 根据配置设置 sdk/bin 与工具集
                    if toolchain_entry.sdk then
                        toolchain_instance:set("sdkdir", toolchain_entry.sdk)
                    end
                    if toolchain_entry.bin then
                        toolchain_instance:set("bindir", toolchain_entry.bin)
                    end
                    if toolchain_entry.toolset then
                        for tool_key, tool_value in pairs(toolchain_entry.toolset) do
                            toolchain_instance:set("toolset", tool_key, tool_value)
                        end
                    end
                end)
            toolchain_end()
        end
    end
end

register_toolchains()

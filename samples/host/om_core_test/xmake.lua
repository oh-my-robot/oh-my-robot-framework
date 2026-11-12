--[[
    @file om_core_test/xmake.lua
    @brief kernel-core 主机侧测试（无宿主端单元测试框架的最小脚手架）

    运行方式（在仓库根或任意位置）：
      xmake f -c -P oh_my_robot/samples/host/om_core_test -m debug
      xmake build -P oh_my_robot/samples/host/om_core_test
      xmake run -P oh_my_robot/samples/host/om_core_test om_core_init_test
      xmake run -P oh_my_robot/samples/host/om_core_test om_core_init_test_abort
      xmake run -P oh_my_robot/samples/host/om_core_test om_core_fatal_test

    退出码 0=通过；非 0=失败（EXPECT 断言不通过）。
    测试对象为 OS 无关 kernel-core 源码，host 直接编译运行（秒级验证，
    替代"构建+烧录+断点"慢循环；启动链逻辑最难烧录调试，恰是 host 可测）。
]]

set_project("om_core_test")
set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release")

local fw = path.join(os.scriptdir(), "..", "..", "..")

-- om_do_initcalls：级别顺序/区间/prio/空槽/失败记录并继续
target("om_core_init_test")
    set_kind("binary")
    set_languages("c11")
    add_includedirs(path.join(fw, "lib/include"))
    add_includedirs(path.join(fw, "lib/source/core")) -- include "om_init.c" 注入
    add_files("om_test_common.c", "om_init_test.c")
target_end()

-- OM_INIT_ABORT_ON_FAIL 语义（编译期宏，独立目标）
target("om_core_init_test_abort")
    set_kind("binary")
    set_languages("c11")
    add_defines("OM_INIT_ABORT_ON_FAIL")
    add_includedirs(path.join(fw, "lib/include"))
    add_includedirs(path.join(fw, "lib/source/core"))
    add_files("om_test_common.c", "om_init_abort_test.c")
target_end()

-- om_fatal_error：handler 调用链 / ctx 传递（include om_fatal.c 注入 + port 桩
-- + 跨 TU 强 handler 覆盖，见 om_fatal_test.c 文件头说明）
target("om_core_fatal_test")
    set_kind("binary")
    set_languages("c11")
    add_includedirs(path.join(fw, "lib/include"))
    add_includedirs(path.join(fw, "lib/source/core"))
    add_files("om_test_common.c", "om_fatal_test.c", "om_fatal_handler_override.c")
target_end()

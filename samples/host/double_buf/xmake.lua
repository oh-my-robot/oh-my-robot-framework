set_project("om_host_samples")
set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release")

--[[
    @target host_double_buf_test_c
    @brief 测试双缓冲区的乒乓读写
    @details 编译并运行测试程序，验证 DoubleBuf 的 Ping-Pong 机制。

    在项目根目录中顺次执行以下命令即可运行例程：
    xmake f -c -P oh_my_robot/samples/host/double_buf -p windows -a x64 -m debug
    xmake build -P oh_my_robot/samples/host/double_buf host_double_buf_test_c
    xmake run -P oh_my_robot/samples/host/double_buf host_double_buf_test_c
]]

target("host_double_buf_test_c")
    set_kind("binary")
    set_languages("c11")
    set_warnings("all")
    add_includedirs("../../../lib/include")
    add_includedirs("../../../lib/data_struct/include")
    add_files("double_buf_test.c")
    add_files("../../../lib/data_struct/src/double_buf.c")
    if is_plat("windows") then
        set_toolchains("msvc")
        add_cxflags("/utf-8", {force = true})
    elseif is_plat("linux") then
        add_syslinks("pthread")
    end
target_end()

set_project("om_host_samples")
set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release")

--[[
    @target host_ringbuffer_test_c
    @brief 测试环形缓冲区并发访问
    @details 编译并运行测试程序，验证环形缓冲区在多线程环境下的并发访问是否安全。

    在项目根目录中顺次执行以下命令即可运行例程：
    xmake f -c -P oh_my_robot/samples/host/ringbuf -p windows -a x64 -m debug
    xmake build -P oh_my_robot/samples/host/ringbuf host_ringbuffer_test_c
    xmake run -P oh_my_robot/samples/host/ringbuf host_ringbuffer_test_c
]]

target("host_ringbuffer_test_c")
    set_kind("binary")
    set_languages("c11")
    set_warnings("all")
    add_includedirs("../../../lib/include")
    add_files("ringbuffer_concurrency_main.c")
    add_files("../../../lib/source/data_struct/ringbuffer.c")
    if is_plat("windows") then
        set_toolchains("msvc")
        add_cxflags("/utf-8", {force = true})
        add_syslinks("kernel32")
    elseif is_plat("linux") then
        add_syslinks("pthread")
    end
target_end()

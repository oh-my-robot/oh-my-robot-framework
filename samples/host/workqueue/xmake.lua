set_project("om_host_workqueue_test")
set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release")

target("host_wq_test")
    set_kind("binary")
    set_languages("gnu11")
    set_warnings("all")

    -- host stub 优先（osal/, sync/, data_struct/ 本地覆盖）
    add_includedirs(".")
    -- 平台无关头文件：core/om_def.h, port/, atomic/
    add_includedirs("../../../lib/include")
    -- 数据结构（被本地 data_struct/corelist.h 覆盖）
    add_includedirs("../../../lib/data_struct/include")
    -- async/workqueue.h
    add_includedirs("../../../lib/async/include")

    add_files("workqueue_host_test.c")
    add_files("host_osal.c")
    add_files("../../../lib/async/src/workqueue.c")

    if is_plat("windows") then
        set_toolchains("clang")
        add_cxflags("-fms-extensions", {force = true})
        add_syslinks("kernel32")
    elseif is_plat("linux") then
        add_syslinks("pthread")
    end
target_end()

--[[
    @file flash_dev_test/xmake.lua
    @brief FlashDev 框架 host 仿真测试（几何双模/边界/对齐/program 语义/
           整扇区擦/DevInterface/并发互斥）

    运行方式（参照 samples/host/om_core_test 的 -P 惯例）：
      xmake f -c -P oh-my-robot-framework/samples/host/flash_dev_test -m debug
      xmake build -P oh-my-robot-framework/samples/host/flash_dev_test
      xmake run -P oh-my-robot-framework/samples/host/flash_dev_test host_flash_test

    退出码 0=全绿；非 0=有 FAIL。OSAL 由本地 host_osal.c 桩提供；
    本地 osal/ 为裁剪覆盖头（规避 lib osal_config 对 om_osal_portdef.h 的端口依赖）。
]]

set_project("om_host_flash_dev_test")
set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release")

local fw = path.join(os.scriptdir(), "..", "..", "..")

target("host_flash_test")
    set_kind("binary")
    set_languages("gnu11") -- corelist.h 使用 typeof（GNU 扩展），需 gnu 方言
    set_warnings("all")

    -- 本地覆盖优先（osal/ sync/ 裁剪头）
    add_includedirs(os.scriptdir())
    -- 平台无关头文件：core/om_def.h、port/、atomic/
    add_includedirs(path.join(fw, "lib/include"))
    -- 数据结构（device.h 依赖 corelist.h）
    add_includedirs(path.join(fw, "lib/data_struct/include"))
    -- async（pal_flash_dev.h 依赖 workqueue.h）
    add_includedirs(path.join(fw, "lib/async/include"))
    -- drivers 公共头：model/device.h、peripheral/flash/pal_flash_dev.h
    add_includedirs(path.join(fw, "lib/drivers/include"))

    add_files("flash_dev_test.c", "host_osal.c", "flash_sim.c")
    -- 框架实现直编（仿 om_core_test/workqueue 模式）
    add_files(path.join(fw, "lib/drivers/src/peripheral/flash/hal_flash.c"))
    add_files(path.join(fw, "lib/drivers/src/peripheral/flash/flash_domain.c"))
    add_files(path.join(fw, "lib/async/src/workqueue.c"))
    add_files(path.join(fw, "lib/drivers/src/model/device.c"))

    if is_plat("linux") then
        add_syslinks("pthread")
    end
target_end()

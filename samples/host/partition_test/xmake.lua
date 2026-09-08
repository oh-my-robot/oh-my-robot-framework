--[[
    @file partition_test/xmake.lua
    @brief 分区表抽象 host 测试（按名查询值拷贝/便捷层边界/配置错误显式报错）

    运行方式（同 flash_dev_test -P 惯例）：
      xmake f -c -P oh-my-robot-framework/samples/host/partition_test -m debug --mingw="D:/Program Files/ProgramTools/WinGW/w64devkit"
      xmake build -P oh-my-robot-framework/samples/host/partition_test
      xmake run -P oh-my-robot-framework/samples/host/partition_test host_partition_test

    退出码 0=全绿。器件仿真与 osal 桩复用同级 flash_dev_test 基础设施
    （flash_sim.c/h、host_osal.c、osal/ sync/ 覆盖头——相对引用，不复制）。
]]

set_project("om_host_partition_test")
set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release")

local fw = path.join(os.scriptdir(), "..", "..", "..")
local fdt = path.join(os.scriptdir(), "..", "flash_dev_test")

target("host_partition_test")
    set_kind("binary")
    set_languages("gnu11")
    set_warnings("all")

    -- 复用同级基础设施：flash_dev_test 目录提供 flash_sim.h、osal/ sync/ 覆盖头
    add_includedirs(os.scriptdir())
    add_includedirs(fdt)
    add_includedirs(path.join(fw, "lib/include"))
    add_includedirs(path.join(fw, "lib/data_struct/include"))
    add_includedirs(path.join(fw, "lib/async/include"))
    add_includedirs(path.join(fw, "lib/drivers/include"))

    add_files("partition_test.c")
    add_files(path.join(fdt, "host_osal.c"))
    add_files(path.join(fdt, "flash_sim.c"))
    -- 框架实现直编
    add_files(path.join(fw, "lib/drivers/src/storage/partition.c"))
    add_files(path.join(fw, "lib/drivers/src/peripheral/flash/hal_flash.c"))
    add_files(path.join(fw, "lib/drivers/src/peripheral/flash/flash_domain.c"))
    add_files(path.join(fw, "lib/async/src/workqueue.c"))
    add_files(path.join(fw, "lib/drivers/src/model/device.c"))

    if is_plat("linux") then
        add_syslinks("pthread")
    end
target_end()

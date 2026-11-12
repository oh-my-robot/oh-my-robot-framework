--[[
    @file spi_nor_test/xmake.lua
    @brief W25Q256JV SPI NOR 适配器 host 测试（跑在真实 SPI 框架之上）

    运行方式（参照 samples/host/om_core_test 的 -P 惯例）：
      xmake f -P devices/tests/host/spi_nor_test -p mingw -m debug \
              --mingw="D:/ProgramFiles/ProgramTools/WinGW-64/mingw64" --cflags="-utf-8" -y
      xmake build -P devices/tests/host/spi_nor_test
      xmake run -P devices/tests/host/spi_nor_test host_spi_nor_test

    退出码 0=全绿。OSAL 由本地 host_osal.c 桩提供；本地 osal/ 与 sync/ 为
    裁剪覆盖头（规避 lib osal_config 对端口定义的依赖）。本目标是 SPI 框架
    （hal_spi.c）的第一个 host 消费者，故把 async 一并编入。
]]

set_project("om_host_spi_nor_test")
set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release")

local repo = path.join(os.scriptdir(), "..", "..", "..", "..")
local devices = path.join(repo, "devices")

target("host_spi_nor_test")
    set_kind("binary")
    set_languages("gnu11") -- corelist.h 使用 typeof（GNU 扩展），需 gnu 方言
    set_warnings("all")

    -- 本地覆盖优先（osal/ sync/ 裁剪头 + 器件仿真头）
    add_includedirs(os.scriptdir())
    -- 平台无关头文件：core/om_def.h、port/、atomic/
    add_includedirs(path.join(repo, "lib/include"))
    -- 数据结构（device.h 依赖 corelist.h）
    add_includedirs(path.join(repo, "lib/data_struct/include"))
    -- async/workqueue.h（SpiBus 内嵌 workqueue）
    add_includedirs(path.join(repo, "lib/async/include"))
    -- drivers 公共头
    add_includedirs(path.join(repo, "lib/drivers/include"))
    -- 器件库公共头
    add_includedirs(path.join(devices, "include"))

    add_files("spi_nor_test.c", "host_osal.c", "host_mem.c", "host_gpio_fake.c", "spi_nor_sim.c")
    -- 器件实现直编（仿 om_core_test/workqueue 模式）
    add_files(path.join(devices, "src/flash/spi_nor_w25q256jv.c"))
    add_files(path.join(repo, "lib/drivers/src/peripheral/flash/hal_flash.c"))
    add_files(path.join(repo, "lib/drivers/src/peripheral/spi/hal_spi.c"))
    -- GPIO：片选两路径都要覆盖——控制器路径（挂载期由框架配置引脚）用真实
    -- hal_gpio.c + 本地假控制器；控制器直控路径（csSpec.controller == NULL）不经 GPIO。
    add_files(path.join(repo, "lib/drivers/src/peripheral/gpio/hal_gpio.c"))
    add_files(path.join(repo, "lib/drivers/src/model/device.c"))
    add_files(path.join(repo, "lib/async/src/workqueue.c"))

    if is_plat("linux") then
        add_syslinks("pthread")
    end
target_end()

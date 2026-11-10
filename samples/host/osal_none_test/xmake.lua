--[[
    @file osal_none_test/xmake.lua
    @brief osal-none 端口 host 单流语料——真实模拟裸机行为：
           R1 同一实现只差 cfg（端口源直编，无桩）；
           R2 模拟 ISR 上下文（isr_enter/exit 钩子覆盖 *_from_isr 正路径与误用拒绝）；
           R3 host 临界区 = 临界区对象（模拟关中断互斥，ISR 模拟辅助线程真并发）；
           R4 忙等让步 Sleep(0)（语义不变，不空转整核）。

    运行方式（-P 惯例）：
      xmake f -c -P oh-my-robot-framework/samples/host/osal_none_test -m debug --mingw="D:/Program Files/ProgramTools/WinGW/w64devkit"
      xmake build -P oh-my-robot-framework/samples/host/osal_none_test
      xmake run -P oh-my-robot-framework/samples/host/osal_none_test host_osal_none_test

    退出码 0=全绿。OM_OSAL_PORT=3（OSAL_PORT_NONE）；
    堆 = 端口同一静态池实现，cfg 调大（65536）供语料分配。
]]

set_project("om_host_osal_none_test")
set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release")

local fw = path.join(os.scriptdir(), "..", "..", "..")

target("host_osal_none_test")
    set_kind("binary")
    set_languages("gnu11")
    set_warnings("all")

    add_defines("OM_OSAL_PORT=3", "OM_OSAL_NONE_HEAP_SIZE=65536")

    add_includedirs(path.join(fw, "lib/include"))
    add_includedirs(path.join(fw, "lib/osal/include"))
    add_includedirs(path.join(fw, "lib/data_struct/include"))
    add_includedirs(path.join(fw, "lib/async/include"))
    add_includedirs(path.join(fw, "lib/sync/include"))
    add_includedirs(path.join(fw, "lib/drivers/include"))
    add_includedirs(path.join(fw, "platform/osal/none"))

    add_files("osal_none_test.c")
    -- workqueue：os=none 下编译期走坍缩分支（#if OM_OSAL_PORT == OSAL_PORT_NONE）
    add_files(path.join(fw, "lib/async/src/workqueue.c"))
    -- FlashDev 坍缩路径编译验证（os 轴分支；运行需器件后端，归实机）
    add_files(path.join(fw, "lib/drivers/src/peripheral/flash/hal_flash.c"))
    add_files(path.join(fw, "lib/drivers/src/model/device.c"))
    -- 端口实现直编（host 与 target 同一代码，差异收敛 arch 文件）
    add_files(path.join(fw, "platform/osal/none/osal_core_none.c"))
    add_files(path.join(fw, "platform/osal/none/osal_time_none.c"))
    add_files(path.join(fw, "platform/osal/none/osal_thread_none.c"))
    add_files(path.join(fw, "platform/osal/none/osal_sem_none.c"))
    add_files(path.join(fw, "platform/osal/none/osal_mutex_none.c"))
    add_files(path.join(fw, "platform/osal/none/osal_timer_none.c"))
    -- host arch 实现（测试基础设施；目标实现见端口 portable/<arch>/）
    add_files("osal_none_arch_x64.c")
target_end()

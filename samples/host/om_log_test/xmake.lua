--[[
    @file om_log_test/xmake.lua
    @brief log 服务主机侧测试（无宿主端单元测试框架的最小脚手架，om_core_test 同款手法）

    运行方式（在仓库根）：
      xmake f -c -P samples/host/om_log_test -m debug
      xmake build -P samples/host/om_log_test
      xmake run -P samples/host/om_log_test om_log_formatter_test
      xmake run -P samples/host/om_log_test om_log_filter_test
      xmake run -P samples/host/om_log_test om_log_module_filter_test
      xmake run -P samples/host/om_log_test om_log_ring_test

    退出码 0=通过；非 0=失败（EXPECT 断言不通过）。
    formatter 目标：纯 C 流式格式化器（无 OS 依赖）；
    filter 目标：om_log_log 全链（过滤/扇出/per-backend 级别/管理 API——OM_LOG_ASYNC=0
    同步模式：现场触发 + 滞留回放，桩 no-op 化临界区）；
    module_filter 目标：per-backend 默认级 + 按模块覆盖用例（set/clear/get_module_level
    语义——OM_LOG_ASYNC=0 同步模式，源清单与 filter 同构）；
    ring 目标：消息环语义（OM_LOG_ASYNC=0 + OM_LOG_RING_LEN=4——滞留/回放顺序/过滤/
    满丢计数/告警节流）。
    异步消费（门铃/日志线程）为 OSAL 依赖——无 host 桩，目标板验证（samples/pal/log_rtt）。
    filter/ring 目标另含 osal 桩（本地 osal/osal_time.h shadow + om_log_osal_stub.c——
    core.c/msg.c 时间戳取 osal_time_now_monotonic，host 桩恒 0）。
]]

set_project("om_log_test")
set_xmakever("3.0.7")
add_rules("mode.debug", "mode.release")

local fw = path.join(os.scriptdir(), "..", "..", "..")
local log_src = path.join(fw, "lib/services/src/log")

target("om_log_formatter_test")
    set_kind("binary")
    set_languages("c11")
    add_includedirs(path.join(fw, "lib/include"))
    add_includedirs(path.join(fw, "lib/services/include"))
    add_includedirs(path.join(fw, "lib/data_struct/include"))
    add_includedirs(log_src)
    add_files("om_log_test_common.c", "om_log_formatter_test.c",
              path.join(log_src, "formatter.c"), path.join(log_src, "msg.c"))
target_end()

target("om_log_filter_test")
    set_kind("binary")
    set_languages("c11")
    add_includedirs(os.scriptdir()) -- 本地 osal/osal_time.h 桩 shadow（先于框架头解析）
    add_includedirs(path.join(fw, "lib/include"))
    add_includedirs(path.join(fw, "lib/services/include"))
    add_includedirs(path.join(fw, "lib/data_struct/include"))
    add_includedirs(log_src)
    add_defines("OM_LOG_ASYNC=0") -- 同步模式：现场触发 + 滞留回放（零 OSAL——Ringbuf 纯原子）
    add_files("om_log_test_common.c", "om_log_filter_test.c", "om_log_port_stub.c",
              "om_log_osal_stub.c",
              path.join(fw, "lib/data_struct/src/ringbuffer.c"),
              path.join(log_src, "formatter.c"), path.join(log_src, "core.c"),
              path.join(log_src, "backend.c"), path.join(log_src, "msg.c"),
              path.join(log_src, "module.c"), path.join(log_src, "stats.c"),
              path.join(log_src, "ring.c")) -- 消息环（生产/消费/滞留回放）
target_end()

target("om_log_module_filter_test")
    set_kind("binary")
    set_languages("c11")
    add_includedirs(os.scriptdir()) -- 本地 osal/osal_time.h 桩 shadow（先于框架头解析）
    add_includedirs(path.join(fw, "lib/include"))
    add_includedirs(path.join(fw, "lib/services/include"))
    add_includedirs(path.join(fw, "lib/data_struct/include"))
    add_includedirs(log_src)
    add_defines("OM_LOG_ASYNC=0") -- 同步模式：现场触发 + 滞留回放（零 OSAL——Ringbuf 纯原子）
    add_files("om_log_test_common.c", "om_log_module_filter_test.c", "om_log_port_stub.c",
              "om_log_osal_stub.c",
              path.join(fw, "lib/data_struct/src/ringbuffer.c"),
              path.join(log_src, "formatter.c"), path.join(log_src, "core.c"),
              path.join(log_src, "backend.c"), path.join(log_src, "msg.c"),
              path.join(log_src, "module.c"), path.join(log_src, "stats.c"),
              path.join(log_src, "ring.c")) -- 后端按模块覆盖（默认级 + 覆盖表语义）
target_end()

target("om_log_ring_test")
    set_kind("binary")
    set_languages("c11")
    add_includedirs(os.scriptdir()) -- 本地 osal/osal_time.h 桩 shadow（先于框架头解析）
    add_includedirs(path.join(fw, "lib/include"))
    add_includedirs(path.join(fw, "lib/services/include"))
    add_includedirs(path.join(fw, "lib/data_struct/include"))
    add_includedirs(log_src)
    add_defines("OM_LOG_ASYNC=0", "OM_LOG_RING_LEN=4") -- 同步模式 + 小环（4 槽精确构造滞留/满丢）
    add_files("om_log_test_common.c", "om_log_ring_test.c", "om_log_port_stub.c",
              "om_log_osal_stub.c",
              path.join(fw, "lib/data_struct/src/ringbuffer.c"),
              path.join(log_src, "formatter.c"), path.join(log_src, "core.c"),
              path.join(log_src, "backend.c"), path.join(log_src, "msg.c"),
              path.join(log_src, "module.c"), path.join(log_src, "stats.c"),
              path.join(log_src, "ring.c")) -- 环语义 + 丢弃后验告警
target_end()

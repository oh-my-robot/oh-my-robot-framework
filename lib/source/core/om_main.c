/**
 * @file  om_main.c
 * @brief 框架默认 main —— 启动编排的唯一 C 入口（init 子系统，kernel 层）
 * @details
 * - 用户默认不写 main：框架经 oh_my_robot.selfreg 规则把本文件直编进 binary，
 *   提供弱符号 main（Zephyr kernel/main.c 同款）。启动文件调 main → 进入启动编排。
 * - 逃生通道：用户定义强 main（宿主/测试/bootloader/自定义启动序列）自动覆盖本弱符号；
 *   若完全不需要框架 main 符号，构建期关闭 oh_my_robot.selfreg 的注入
 *   （om_framework_main 开关，见 build/rules/selfreg.lua）。
 * - 职责边界：本文件仅做"默认接线"——调用 om_system_startup()；不含任何
 *   平台/板级/业务逻辑，这些一律经 OM_INIT_LEVEL_* 分散加载（见 om_init.h、ADR-0013）。
 * - 签名说明：声明 `(int argc, char *argv[])` 而非 `(void)`——armclang 会给定义无参
 *   main 的 TU 自动生成强符号 __ARM_use_no_argv（C 库 argv 检测），框架弱 main 与用户
 *   强 main 并存时会重复定义（L6123E）；带参签名抑制该生成，参数被忽略。
 */
#include "core/om_init.h"

OM_WEAK int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* 启动编排：调度器前 EARLIEST+BOARD+DRIVER → init 线程跑 SERVICE..LATE
     * （含 OM_INIT_APPLICATION 注册的 app 设置）→ osal_kernel_start()，正常不返回。 */
    om_system_startup();
    while (1) {
    }
}

# 原始调研报告（未提炼）

四份报告由并行调研任务产出（2026-09-15）。**每条事实带出处 URL，且各含「存疑与未证实」一节。**
本目录是 `../log_persist_research.md` 的证据基础——提炼稿只摘了结论，全部来源链接在这里。

| 文件 | 范围 | 规模 |
|---|---|---|
| `research_1_zephyr.md` | Zephyr：log core / 后端模型 / FCB 与 FS 持久化后端 / panic 路径 | 430 行 |
| `research_2_rtos.md` | RT-Thread（ulog/EasyFlash/FlashDB）、ESP-IDF（NVS/coredump/esp_log）、NuttX（syslog/ramlog/CRASHDUMP）、MCUboot、SEGGER | 583 行 / 39 来源 |
| `research_3_systems.md` | pstore+ramoops、Android pmsg/logd、systemd-journald、AUTOSAR DLT、车规 EDR、ArduPilot DataFlash、Betaflight Blackbox | 932 行 / 341 链接 |
| `research_4_flash_tech.md` | 器件时序与寿命、记录布局、恢复扫描、掉电窗口、磨损均衡、要不要文件系统、CRC 选型 | 508 行 |

## 使用注意

1. 这些是**调研产出，不是决策**——引用前请先看各自末尾的「存疑与未证实」节，其中条目**明确不可作为设计依据**（例：FlashDB 官方文档站抓不到正文、机制描述全部改由源码支撑；NVS 页状态数值两个 ref 均 404；SPIFFS 劣化的具体数字无原始测试条件）。
2. **所有容量 / 寿命 / 扫描耗时数字均为推算**（读带宽是按 10 MB/s 外置、20 MB/s 片内粗估的），须在目标板实测后再用于设计定稿。
3. `research_3_systems.md` 修正了派单任务书里的若干错误预设（journald 无 rename 两阶段、无 `qcom/pmsg.c`、Android 无 `persistent` buffer id、ArduPilot 记录头是 3 字节等），详见其「存疑与未证实」节。

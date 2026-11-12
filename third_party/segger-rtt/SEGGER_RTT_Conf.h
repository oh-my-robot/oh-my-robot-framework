/*
 * SEGGER_RTT_Conf.h
 *
 * Minimal config header (empty body — all defaults come from
 * SEGGER_RTT_ConfDefaults.h). Override any macro here with #ifndef-safe
 * definitions, e.g.:
 *   #ifndef SEGGER_RTT_MAX_NUM_UP_BUFFERS
 *   #define SEGGER_RTT_MAX_NUM_UP_BUFFERS 3
 *   #endif
 *
 * Original license: SEGGER Microcontroller GmbH — see LICENSE.md.
 */

#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

/* 上向（目标→宿主）环形缓冲 4096B：压测实证（2026-09-04 21 格矩阵）——1024B 时宿主读一轮
 * 停顿即触发 3-5% 稳态丢包（环形余量仅 ~25ms），4096B 下 225KB/s 满速率零丢；
 * 与 Zephyr 默认一致。代价 +3KB RAM（本板 192KB 余量充足）。 */
#define BUFFER_SIZE_UP 4096

#endif /* SEGGER_RTT_CONF_H */

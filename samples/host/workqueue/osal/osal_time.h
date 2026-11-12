#ifndef OM_HOST_OSAL_TIME_H
#define OM_HOST_OSAL_TIME_H

#include <stdint.h>

typedef uint32_t OsalTimeMs;

/** host 桩：Sleep(ms)，无返回值；与 host_osal.c 中的实现匹配 */
void osal_sleep_ms(OsalTimeMs sleep_ms);

#endif

#include "osal/osal.h"
#include "osal/osal_config.h"
#include "osal/osal_core.h"
#include "osal/osal_event.h"
#include "osal/osal_mutex.h"
#include "osal/osal_port.h"
#include "osal/osal_queue.h"
#include "osal/osal_sem.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h"
#include "osal/osal_timer.h"

/* Keep a real translation unit so static analysis always traverses these headers. */
int osal_headers_probe_translation_unit_anchor(void)
{
    return 0;
}

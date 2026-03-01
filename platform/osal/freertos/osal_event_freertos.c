#include "osal/osal_event.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "osal_time_freertos.h"

static inline EventGroupHandle_t osal_event_flags_to_native(OsalEventFlags_t event_flags)
{
    return (EventGroupHandle_t)event_flags;
}

static inline OsalEventFlags_t osal_event_flags_from_native(EventGroupHandle_t event_flags)
{
    return (OsalEventFlags_t)event_flags;
}

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define OSAL_STATIC_ASSERT(cond, msg) _Static_assert((cond), #msg)
#else
#define OSAL_STATIC_ASSERT(cond, msg) typedef char static_assert_##msg[(cond) ? 1 : -1]
#endif

#if (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_16_BITS)
#error "OM event_flags requires non-16-bit tick width on FreeRTOS."
#endif

OSAL_STATIC_ASSERT((OSAL_EVENT_FLAGS_USABLE_MASK != 0u), osal_event_flags_usable_mask_must_not_be_zero);
OSAL_STATIC_ASSERT((((uint32_t)OSAL_EVENT_FLAGS_USABLE_MASK & (uint32_t)eventEVENT_BITS_CONTROL_BYTES) == 0u),
                   osal_event_flags_usable_mask_must_not_overlap_control_bits);

static uint32_t osal_event_get_usable_mask(void)
{
    return ((uint32_t)(~((uint32_t)eventEVENT_BITS_CONTROL_BYTES))) & OSAL_EVENT_FLAGS_USABLE_MASK;
}

static int osal_event_flags_mask_valid(uint32_t flags)
{
    uint32_t usable_mask = osal_event_get_usable_mask();
    return ((flags & (~usable_mask)) == 0u);
}

static int osal_event_check_task_context(void)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr == 0);
    return (in_isr == 0);
}

static int osal_event_check_isr_context(void)
{
    int in_isr = osal_is_in_isr();
    OSAL_ASSERT(in_isr != 0);
    return (in_isr != 0);
}

OsalStatus_t osal_event_flags_create(OsalEventFlags_t* event_flags)
{
    if (!event_flags)
        return OSAL_INVALID;
    if (!osal_event_check_task_context())
        return OSAL_INVALID;

    EventGroupHandle_t handle = xEventGroupCreate();
    if (!handle)
        return OSAL_NO_RESOURCE;

    *event_flags = osal_event_flags_from_native(handle);
    return OSAL_OK;
}

OsalStatus_t osal_event_flags_delete(OsalEventFlags_t event_flags)
{
    if (!event_flags)
        return OSAL_INVALID;
    if (!osal_event_check_task_context())
        return OSAL_INVALID;

    vEventGroupDelete(osal_event_flags_to_native(event_flags));
    return OSAL_OK;
}

OsalStatus_t osal_event_flags_set(OsalEventFlags_t event_flags, uint32_t flags)
{
    if (!event_flags)
        return OSAL_INVALID;
    if (!osal_event_flags_mask_valid(flags))
        return OSAL_INVALID;
    if (!osal_event_check_task_context())
        return OSAL_INVALID;

    (void)xEventGroupSetBits(osal_event_flags_to_native(event_flags), (EventBits_t)flags);
    return OSAL_OK;
}

OsalStatus_t osal_event_flags_set_from_isr(OsalEventFlags_t event_flags, uint32_t flags)
{
    if (!event_flags)
        return OSAL_INVALID;
    if (!osal_event_flags_mask_valid(flags))
        return OSAL_INVALID;
    if (!osal_event_check_isr_context())
        return OSAL_INVALID;

    BaseType_t need_switch = pdFALSE;
    BaseType_t ok = xEventGroupSetBitsFromISR(osal_event_flags_to_native(event_flags), (EventBits_t)flags, &need_switch);
    if (need_switch == pdTRUE)
        portYIELD_FROM_ISR(need_switch);

    if (ok == pdPASS)
        return OSAL_OK;

    /* FreeRTOS 涓璺緞閫氬父鐢?timer service queue 璧勬簮涓嶈冻瑙﹀彂銆?*/
    return OSAL_NO_RESOURCE;
}

OsalStatus_t osal_event_flags_clear(OsalEventFlags_t event_flags, uint32_t flags)
{
    if (!event_flags)
        return OSAL_INVALID;
    if (!osal_event_flags_mask_valid(flags))
        return OSAL_INVALID;
    if (!osal_event_check_task_context())
        return OSAL_INVALID;

    (void)xEventGroupClearBits(osal_event_flags_to_native(event_flags), (EventBits_t)flags);
    return OSAL_OK;
}

OsalStatus_t osal_event_flags_wait(OsalEventFlags_t event_flags, uint32_t wait_mask, uint32_t* out_value,
                                    uint32_t timeout_ms, uint32_t options)
{
    if (!event_flags || wait_mask == 0u)
        return OSAL_INVALID;
    if (!osal_event_flags_mask_valid(wait_mask))
        return OSAL_INVALID;
    if (!osal_event_check_task_context())
        return OSAL_INVALID;

    const BaseType_t clear_on_exit = ((options & OSAL_EVENT_FLAGS_NO_CLEAR) != 0u) ? pdFALSE : pdTRUE;
    const BaseType_t wait_for_all = ((options & OSAL_EVENT_FLAGS_WAIT_ALL) != 0u) ? pdTRUE : pdFALSE;

    TickType_t ticks = osal_ms_to_ticks(timeout_ms);
    EventBits_t bits =
        xEventGroupWaitBits(
            osal_event_flags_to_native(event_flags),
            (EventBits_t)wait_mask,
            clear_on_exit,
            wait_for_all,
            ticks);

    uint32_t observed_bits = ((uint32_t)bits) & osal_event_get_usable_mask();

    if (out_value)
        *out_value = observed_bits;

    if (wait_for_all == pdTRUE)
    {
        if ((observed_bits & wait_mask) == wait_mask)
            return OSAL_OK;
    }
    else if ((observed_bits & wait_mask) != 0u)
    {
        return OSAL_OK;
    }

    return (timeout_ms == 0u) ? OSAL_WOULD_BLOCK : OSAL_TIMEOUT;
}


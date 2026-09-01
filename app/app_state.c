/*
 * app_state.c — 小智设备状态机
 *
 * P4：set() 不再直调 ui_main_on_state（协议线程摸 LVGL 的线程违规），
 * 改发 APP_EVT_STATE 经 ui_msg 泵到主线程（多经一拍 20ms，肉眼不可见）。
 */
#include "app_state.h"
#include "app_events.h"
#include "app_event_bus.h"
#include "osal/osal_atomic.h"
#include <string.h>

static osal_atomic_t g_state = OSAL_ATOMIC_INIT_ZERO;

xz_state_t app_state_get(void)
{
    return (xz_state_t)osal_atomic_get(&g_state);
}

void app_state_init(void)
{
    osal_atomic_set(&g_state, XZ_STATE_IDLE);
}

void app_state_deinit(void)
{
}

void app_state_set(xz_state_t s, const char *reason)
{
    osal_atomic_set(&g_state, s);
    /* 状态同步走事件总线（主线程应用）；reason 作为 data 携带 */
    app_event_post(APP_EVT_STATE, reason, (int)s, NULL, 0);
}

/*
 * app_state.c — 小智设备状态机（M0 骨架：仅记录 + 通知 UI）
 */
#include "app_state.h"
#include "ui_main.h"
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
    ui_main_on_state(s, reason);
}

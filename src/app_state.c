/*
 * app_state.c — 小智设备状态机（M0 骨架：仅记录 + 通知 UI）
 */
#include "app_state.h"
#include "ui/ui_main.h"
#include <stdatomic.h>
#include <string.h>

static atomic_int g_state = XZ_STATE_IDLE;

xz_state_t app_state_get(void)
{
    return (xz_state_t)atomic_load(&g_state);
}

void app_state_init(void)
{
    atomic_store(&g_state, XZ_STATE_IDLE);
}

void app_state_deinit(void)
{
}

void app_state_set(xz_state_t s, const char *reason)
{
    atomic_store(&g_state, s);
    ui_main_on_state(s, reason);
}

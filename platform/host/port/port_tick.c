/*
 * port/sdl/port_tick.c — LVGL 时基（LV_TICK_CUSTOM=1）
 *
 * 原实现用 gettimeofday 记起点，现直接用 osal CLOCK_MONOTONIC
 * 单调毫秒（无回绕起点问题，语义等价）。
 */
#include "osal/osal_time.h"
#include <stdint.h>

uint32_t custom_tick_get(void)
{
    return (uint32_t)osal_uptime_ms();
}

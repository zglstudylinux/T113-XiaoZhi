/*
 * osal_time.c — POSIX 实现：CLOCK_MONOTONIC 毫秒时基 + nanosleep
 */
#include "osal/osal_time.h"
#include <time.h>

uint32_t osal_uptime_ms(void)
{
    return (uint32_t)osal_uptime_ms64();
}

uint64_t osal_uptime_ms64(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

void osal_sleep_ms(uint32_t ms)
{
    if (ms == 0)
        return;
    struct timespec ts = {
        .tv_sec  = (time_t)(ms / 1000U),
        .tv_nsec = (long)(ms % 1000U) * 1000000L,
    };
    while (nanosleep(&ts, &ts) != 0)
        ;   /* EINTR 后剩余时间继续睡 */
}

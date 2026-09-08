/*
 * osal_log.c — POSIX 实现：时间戳 + stdout
 */
#include "osal/osal_log.h"
#include "osal/osal_time.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

static osal_log_level_t g_level = OSAL_LOG_D;

void osal_log_set_level(osal_log_level_t lvl) { g_level = lvl; }

static const char *level_char(osal_log_level_t l)
{
    switch (l) {
    case OSAL_LOG_D: return "D";
    case OSAL_LOG_I: return "I";
    case OSAL_LOG_W: return "W";
    default:         return "E";
    }
}

void osal_log(osal_log_level_t lvl, const char *tag, const char *fmt, ...)
{
    if (lvl < g_level)
        return;

    uint64_t ms = osal_uptime_ms64();
    printf("[%s][%02u:%02u:%02u.%03u][%s] ",
           level_char(lvl),
           (unsigned)((ms / 3600000ULL) % 24ULL),
           (unsigned)((ms / 60000ULL) % 60ULL),
           (unsigned)((ms / 1000ULL) % 60ULL),
           (unsigned)(ms % 1000ULL),
           tag ? tag : "app");

    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

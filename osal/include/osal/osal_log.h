/*
 * osal_log.h — 统一日志接口
 *
 * 统一现有 "[xz] xxx" printf 风格为 "[I][hh:mm:ss.mmm][tag] xxx"；
 * posix 实现输出到 stdout（main 已 setvbuf 无缓冲）。MCU 侧换 UART 后端
 * 只重写 osal/posix/osal_log.c。
 */
#ifndef OSAL_LOG_H
#define OSAL_LOG_H

#include "osal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OSAL_LOG_D = 0,
    OSAL_LOG_I = 1,
    OSAL_LOG_W = 2,
    OSAL_LOG_E = 3,
} osal_log_level_t;

void osal_log_set_level(osal_log_level_t lvl);
void osal_log(osal_log_level_t lvl, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define XZ_LOGD(tag, ...) osal_log(OSAL_LOG_D, tag, __VA_ARGS__)
#define XZ_LOGI(tag, ...) osal_log(OSAL_LOG_I, tag, __VA_ARGS__)
#define XZ_LOGW(tag, ...) osal_log(OSAL_LOG_W, tag, __VA_ARGS__)
#define XZ_LOGE(tag, ...) osal_log(OSAL_LOG_E, tag, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
#endif /* OSAL_LOG_H */

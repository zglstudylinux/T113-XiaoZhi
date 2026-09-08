/*
 * osal_time.h — 毫秒时基（LVGL tick、重连退避、帧计时唯一来源）
 *
 * osal_uptime_ms 用 uint32（LVGL 语义，回绕 49 天）；
 * 长跨度计时（wifi/协议重连）用 osal_uptime_ms64。
 */
#ifndef OSAL_TIME_H
#define OSAL_TIME_H

#include "osal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t osal_uptime_ms(void);
uint64_t osal_uptime_ms64(void);
void     osal_sleep_ms(uint32_t ms);   /* ms==0 立即返回 */

#ifdef __cplusplus
}
#endif
#endif /* OSAL_TIME_H */

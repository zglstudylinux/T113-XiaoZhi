/*
 * app_event_bus.h — 业务→UI 事件总线（主线程投递）
 *
 * 实现 = malloc 载荷 + ui_msg_post（ui_msg 泵 20ms 在主循环执行）。
 * 队满丢最旧（UI 只需最新状态，语义与 M3 相同）。
 */
#ifndef APP_EVENT_BUS_H
#define APP_EVENT_BUS_H

#include "app_events.h"

/* 任意线程可调；载荷 data 拷贝（最多 511 字节）。返回 0 已入队 */
int app_event_post(app_evt_t evt, const char *data,
                   int aux1, const void *aux, unsigned aux_sz);

/* ui_facade 初始化时注册（主线程，启动早期）；返回句柄供反订阅 */
int  app_event_subscribe(void (*handler)(app_evt_t evt, const char *data,
                                         int aux1, const void *aux,
                                         unsigned aux_sz, void *user),
                         void *user);
void app_event_unsubscribe(int handle);

#endif

/*
 * ui_facade.h — 业务事件订阅入口（main 启动早期初始化一次）
 */
#ifndef UI_FACADE_H
#define UI_FACADE_H

void ui_facade_init(void);      /* 内部先 ui_msg_init 再订阅事件总线 */
void ui_facade_deinit(void);

#endif

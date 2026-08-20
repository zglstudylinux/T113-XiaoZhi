/*
 * ui_wifi_setup.h — WiFi 配网页（480×640）
 */
#ifndef UI_WIFI_SETUP_H
#define UI_WIFI_SETUP_H

#include "lvgl/lvgl.h"

/* 进入配网页。on_done(connected) 在连接流程结束后回调（主线程）。 */
void ui_wifi_setup_create(void (*on_done)(int connected));

#endif

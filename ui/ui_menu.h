/*
 * ui_menu.h — 启动菜单页（M5）
 *
 * 图标按钮列表：AI 小智 / WiFi 设置，后续 app（蓝牙音箱等）往
 * g_pages[] 加一行即可。
 */
#ifndef UI_MENU_H
#define UI_MENU_H

#include "lvgl/lvgl.h"

void ui_menu_create(lv_obj_t *scr);

#endif

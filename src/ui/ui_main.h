/*
 * ui_main.h — 小智主界面（480×640）
 */
#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "lvgl/lvgl.h"
#include "app_state.h"

/* 供 main.c 引用的字体 */
extern lv_font_t *ui_font_cn_32;
extern lv_font_t *ui_font_cn_48;

void ui_main_create(void);

/* app_state 回调（主线程内被调） */
void ui_main_on_state(xz_state_t s, const char *reason);

/* 聊天气泡（M3/M4：stt/tts 文本；内部 lv_async_call 转主线程） */
void ui_main_add_chat(uint8_t from_user, const char *text);

#endif

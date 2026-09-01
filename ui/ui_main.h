/*
 * ui_main.h — 小智主界面（480×640）
 */
#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "lvgl/lvgl.h"
#include "app_state.h"

/* 供 main.c 引用的字体 */
extern lv_font_t *ui_font_cn_24;
extern lv_font_t *ui_font_cn_32;
extern lv_font_t *ui_font_cn_48;

void ui_main_create(lv_obj_t *scr);

/* 配网页 create 适配器（main.c 启动失败路径用） */
void ui_wifi_setup_open(lv_obj_t *scr);

/* 未绑定设备：状态行大字显示 6 位验证码（主线程内调用） */
void ui_main_show_activation(const char *code);

/* app_state 回调（主线程内被调） */
void ui_main_on_state(xz_state_t s, const char *reason);

/* 服务器连接信息行（M2：已连接 session_id / 验证码提示）。
 * 内部 lv_async_call 转主线程，任意线程可调。 */
void ui_main_on_server_info(const char *line1, const char *line2);

/* 聊天气泡（M3/M4：stt/tts 文本；内部 lv_async_call 转主线程） */
void ui_main_add_chat(uint8_t from_user, const char *text);

#endif

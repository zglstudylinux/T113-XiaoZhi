/*
 * ui_navigate.h — 界面跳转管理（M5）
 *
 * 统一切屏入口：销毁回调 → 建新 screen → lv_scr_load → 删旧 screen。
 * 各页面注册销毁回调，切走前停自己的 lv_timer / 复位静态指针，
 * 防止迟到的 lv_async_call（扫描/连接/协议线程）摸到已释放的对象。
 */
#ifndef UI_NAVIGATE_H
#define UI_NAVIGATE_H

#include "lvgl/lvgl.h"

typedef void (*ui_page_create_t)(lv_obj_t *scr);

/* 注册当前页销毁回调（页面 create 时调用；只对当前页生效，
 * 切屏时先调用再销毁，回调内置空指针/停定时器） */
void ui_nav_set_leave_cb(void (*cb)(void));

/* 跳转到新页面：先回调 leave_cb（停定时器/复位指针），
 * 建新 screen + lv_scr_load + 删旧 screen。任意页面都用这个切 */
void ui_nav_go(ui_page_create_t create);

/* 跳转但不删旧 screen（罕见用） */
void ui_nav_go_keep(ui_page_create_t create);

#endif

/*
 * ui_navigate.c — 界面跳转管理
 *
 * 修复的 bug：
 * 1. 配网页 → 主界面只删 screen，但配网页的扫描/连接线程还会经
 *    lv_async_call 操作 g_list/g_status（已释放）→ LVGL 对象状态写坏
 *    → 出现"小智界面跟 WiFi 配置界面重叠"。
 *    现在：页面 create 时注册 leave 回调（停定时器、静态指针置空、
 *    置"页已销毁"标志），切屏时先调用，线程回调看到标志直接丢消息。
 * 2. 删除旧屏的时机：lv_scr_load() 只标记待激活，真实切屏发生在
 *    下一帧 lv_timer_handler。自己管理删除（立即删/async 延迟删）在
 *    快速反复切换下都有 use-after-free 窗口（实测反复进出后崩溃、
 *    触摸无响应）。改用 LVGL8 官方 lv_scr_load_anim(..., auto_del=true)
 *    ——框架内部在切换完成后安全删除旧屏。
 */
#include "ui_navigate.h"
#include <stdio.h>

static void (*g_leave_cb)(void);

void ui_nav_set_leave_cb(void (*cb)(void))
{
    g_leave_cb = cb;
}

static void go_internal(ui_page_create_t create, int keep_old)
{
    /* 1) 旧页离开回调：停定时器 + 静态指针置空（线程回调不再摸对象） */
    if (g_leave_cb) {
        void (*cb)(void) = g_leave_cb;
        g_leave_cb = NULL;          /* 只触发一次 */
        cb();
    }

    /* 2) 建新页；切换由 LVGL 完成，auto_del=true 切完自动删旧屏 */
    lv_obj_t *scr = lv_obj_create(NULL);
    create(scr);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, !keep_old);
}

void ui_nav_go(ui_page_create_t create)
{
    go_internal(create, 0);
}

void ui_nav_go_keep(ui_page_create_t create)
{
    go_internal(create, 1);
}

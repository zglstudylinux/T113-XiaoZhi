/*
 * ui_wifi_setup.c — WiFi 配网页（480×640，M1）
 *
 * 流程：进入 → 显示"扫描中" → 扫描线程出结果 → 列表（SSID + 信号图标）
 *       → 点选 → 密码输入框 + lv_keyboard → 点"连接" → 连接线程
 *       → 成功存 wifi.cfg 回主页 / 失败提示（密码错误/超时）
 *
 * wifi_manager 的 scan/connect 是阻塞调用，全部放工作线程，
 * 结果经 lv_async_call 回 UI（bt-speaker 验证过的模式）。
 *
 * 注意：g_scan_cache 是静态缓存 —— btn 的 user_data 指向其中的 aps[]，
 * 下次扫描覆盖前列表已重建，悬空指针不存在。
 */
#include "ui_wifi_setup.h"
#include "ui_main.h"
#include "../wifi/wifi_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

static void (*g_on_done)(int connected);

static lv_obj_t *g_scr;             /* 配网 screen */
static lv_obj_t *g_list;            /* AP 列表 */
static lv_obj_t *g_status;          /* 状态行 */
static lv_obj_t *g_psw_win;         /* 密码弹窗 */
static lv_obj_t *g_psw_ta;
static lv_obj_t *g_kb;
static char g_sel_ssid[33];
static uint8_t g_sel_open;
typedef struct { int count; wifi_ap_t aps[WIFI_MAX_AP]; } scan_msg_t_;
typedef struct { int result; char ip[16]; } conn_msg_t;

static char g_psk_pending[64];      /* 点连接时从 textarea 拷出（主线程） */
static scan_msg_t_ g_scan_cache;

/* 前置声明（事件回调相互引用） */
static void retry_ev_cb(lv_event_t *e);
static void ap_click_ev_cb(lv_event_t *e);
static void start_scan(void);

/* ---------- 扫描结果回 UI ---------- */

static void do_scan_async(void *p)
{
    scan_msg_t_ *m = (scan_msg_t_ *)p;
    lv_obj_clean(g_list);                       /* 清掉"扫描中"占位 */

    if (m->count <= 0) {
        lv_obj_t *l = lv_label_create(g_list);
        lv_obj_set_style_text_font(l, ui_font_cn_32, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xE74C3C), 0);
        lv_label_set_text(l, "未扫到网络\n点此重试");
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(l, retry_ev_cb, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(g_status, "扫描失败");
        return;
    }

    char line[64];
    for (int i = 0; i < m->count; i++) {
        wifi_ap_t *ap = &m->aps[i];
        lv_obj_t *btn = lv_btn_create(g_list);
        lv_obj_set_size(btn, 396, 64);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, ui_font_cn_32, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
        lv_label_set_text(lbl, ap->ssid);
        lv_obj_t *sig = lv_label_create(btn);
        lv_obj_set_style_text_font(sig, ui_font_cn_32, 0);
        lv_obj_align(sig, LV_ALIGN_RIGHT_MID, -8, 0);
        snprintf(line, sizeof(line), "%s%s",
                 LV_SYMBOL_WIFI,
                 ap->auth_open ? " ∅" : "");
        lv_label_set_text(sig, line);
        lv_obj_add_event_cb(btn, ap_click_ev_cb, LV_EVENT_CLICKED,
                            (void *)ap);
    }
    lv_label_set_text(g_status, "请选择网络");
}

static void *scan_thread(void *arg)
{
    (void)arg;
    scan_msg_t_ *m = &g_scan_cache;
    m->count = wifi_manager_scan(m->aps, WIFI_MAX_AP);
    lv_async_call(do_scan_async, m);
    return NULL;
}

static void start_scan(void)
{
    lv_label_set_text(g_status, "扫描中…");
    lv_obj_clean(g_list);
    lv_obj_t *l = lv_label_create(g_list);
    lv_obj_set_style_text_font(l, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x8A98A6), 0);
    lv_label_set_text(l, "正在扫描附近的 WiFi…");
    pthread_t tid;
    pthread_create(&tid, NULL, scan_thread, NULL);
    pthread_detach(tid);
}

/* ---------- 连接流程 ---------- */

static void conn_done_async(void *p)
{
    conn_msg_t *m = (conn_msg_t *)p;
    if (m->result == 0) {
        char txt[96];
        snprintf(txt, sizeof(txt), "已连接 %s\nIP: %s", g_sel_ssid, m->ip);
        lv_label_set_text(g_status, txt);
        /* 稍候回主页 —— 用 lv_timer 一次性延迟，避免阻塞 UI 线程 */
        struct { char unused; } *noop = NULL; (void)noop;
        if (g_on_done) g_on_done(1);
    } else if (m->result == -2) {
        lv_label_set_text(g_status, "密码错误，请重试");
        lv_obj_clear_flag(g_psw_win, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(g_status, "连接失败/超时，请重试");
    }
    free(m);
}

static void *connect_thread(void *arg)
{
    (void)arg;
    char psk[64];
    strncpy(psk, g_psk_pending, sizeof(psk) - 1);
    psk[sizeof(psk) - 1] = '\0';

    conn_msg_t *m = (conn_msg_t *)malloc(sizeof(conn_msg_t));
    m->result = wifi_manager_connect(g_sel_ssid, g_sel_open ? NULL : psk, 25000);
    const char *ip = wifi_manager_get_ip();
    snprintf(m->ip, sizeof(m->ip), "%s", ip ? ip : "?");
    if (m->result == 0)
        wifi_manager_save_cfg(g_sel_ssid, psk);
    lv_async_call(conn_done_async, m);
    return NULL;
}

static void conn_click_ev_cb(lv_event_t *e)
{
    (void)e;
    const char *txt = lv_textarea_get_text(g_psw_ta);
    snprintf(g_psk_pending, sizeof(g_psk_pending), "%s", txt);
    if (!g_sel_open && strlen(g_psk_pending) < 8) {
        lv_label_set_text(g_status, "密码至少 8 位");
        return;
    }
    lv_obj_add_flag(g_psw_win, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_status, "连接中…");
    pthread_t tid;
    pthread_create(&tid, NULL, connect_thread, NULL);
    pthread_detach(tid);
}

static void cancel_click_ev_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(g_psw_win, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- AP 选择 → 弹密码窗 ---------- */

static void ap_click_ev_cb(lv_event_t *e)
{
    wifi_ap_t *ap = (wifi_ap_t *)lv_event_get_user_data(e);
    snprintf(g_sel_ssid, sizeof(g_sel_ssid), "%s", ap->ssid);
    g_sel_open = ap->auth_open;

    if (g_sel_open) {
        g_psk_pending[0] = '\0';
        lv_label_set_text(g_status, "连接中…");
        pthread_t tid;
        pthread_create(&tid, NULL, connect_thread, NULL);
        pthread_detach(tid);
        return;
    }

    /* 弹窗标题（第一个子对象） */
    lv_obj_t *title = lv_obj_get_child(g_psw_win, 0);
    char t[64];
    snprintf(t, sizeof(t), "连接 %s", g_sel_ssid);
    lv_label_set_text(title, t);
    lv_textarea_set_text(g_psw_ta, "");
    lv_obj_clear_flag(g_psw_win, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(g_kb, g_psw_ta);
}

static void retry_ev_cb(lv_event_t *e)
{
    (void)e;
    start_scan();
}

/* ---------- 密码弹窗 ---------- */

static void create_psw_win(lv_obj_t *parent)
{
    g_psw_win = lv_obj_create(parent);
    lv_obj_set_size(g_psw_win, 440, 240);
    lv_obj_align(g_psw_win, LV_ALIGN_CENTER, 0, -100);
    lv_obj_set_style_bg_color(g_psw_win, lv_color_hex(0x24303C), 0);
    lv_obj_set_style_radius(g_psw_win, 12, 0);
    lv_obj_clear_flag(g_psw_win, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_psw_win, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(g_psw_win);
    lv_obj_set_style_text_font(title, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title, "输入密码");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    g_psw_ta = lv_textarea_create(g_psw_win);
    lv_obj_set_size(g_psw_ta, 400, 56);
    lv_obj_align(g_psw_ta, LV_ALIGN_TOP_MID, 0, 48);
    lv_textarea_set_password_mode(g_psw_ta, 1);
    lv_textarea_set_one_line(g_psw_ta, 1);
    lv_textarea_set_placeholder_text(g_psw_ta, "WiFi 密码");

    lv_obj_t *conn = lv_btn_create(g_psw_win);
    lv_obj_set_size(conn, 190, 56);
    lv_obj_align(conn, LV_ALIGN_TOP_LEFT, 10, 120);
    lv_obj_set_style_bg_color(conn, lv_color_hex(0x1A6FB5), 0);
    lv_obj_t *cl = lv_label_create(conn);
    lv_obj_set_style_text_font(cl, ui_font_cn_32, 0);
    lv_label_set_text(cl, "连接");
    lv_obj_center(cl);
    lv_obj_add_event_cb(conn, conn_click_ev_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel = lv_btn_create(g_psw_win);
    lv_obj_set_size(cancel, 190, 56);
    lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -10, 120);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x546E7A), 0);
    lv_obj_t *xl = lv_label_create(cancel);
    lv_obj_set_style_text_font(xl, ui_font_cn_32, 0);
    lv_label_set_text(xl, "取消");
    lv_obj_center(xl);
    lv_obj_add_event_cb(cancel, cancel_click_ev_cb, LV_EVENT_CLICKED, NULL);
}

/* ---------- 页面 ---------- */

void ui_wifi_setup_create(void (*on_done)(int connected))
{
    g_on_done = on_done;

    g_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_scr, lv_color_hex(0x101418), 0);
    lv_scr_load(g_scr);

    lv_obj_t *title = lv_label_create(g_scr);
    lv_obj_set_style_text_font(title, ui_font_cn_48, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title, "WiFi 设置");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    lv_obj_t *refresh = lv_btn_create(g_scr);
    lv_obj_set_size(refresh, 100, 56);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -16, 10);
    lv_obj_t *rl = lv_label_create(refresh);
    lv_obj_set_style_text_font(rl, ui_font_cn_32, 0);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH);
    lv_obj_center(rl);
    lv_obj_add_event_cb(refresh, retry_ev_cb, LV_EVENT_CLICKED, NULL);

    g_status = lv_label_create(g_scr);
    lv_obj_set_style_text_font(g_status, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(g_status, lv_color_hex(0x4FC3F7), 0);
    lv_label_set_text(g_status, "扫描中…");
    lv_obj_align(g_status, LV_ALIGN_TOP_LEFT, 20, 76);

    g_list = lv_obj_create(g_scr);
    lv_obj_set_size(g_list, 440, 480);
    lv_obj_align(g_list, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_set_style_bg_color(g_list, lv_color_hex(0x1A2129), 0);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_list, 0, 0);
    lv_obj_set_style_radius(g_list, 12, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);

    create_psw_win(g_scr);

    /* 键盘（屏幕下半，配网期间显示） */
    g_kb = lv_keyboard_create(g_scr);
    lv_obj_set_size(g_kb, 480, 240);
    lv_obj_align(g_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(g_kb, ui_font_cn_32, 0);

    start_scan();
}

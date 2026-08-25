/*
 * ui_main.c — 小智主界面（480×640）
 *
 * M3：说话按钮接 talk_start/stop（点按一次开始，服务器 VAD 判停或
 * 再点按结束）；stt/tts 文本气泡显示。
 */
#include "ui_main.h"
#include "net/xz_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *g_state_label;
static lv_obj_t *g_net_label;
static lv_obj_t *g_chat_cont;
static lv_obj_t *g_chat_box;      /* 气泡纵向排列容器 */
static lv_obj_t *g_talk_btn;
static lv_obj_t *g_talk_lbl;

static const char *state_text(xz_state_t s)
{
    switch (s) {
    case XZ_STATE_IDLE:        return "待命 · 点按开始说话";
    case XZ_STATE_WIFI_SETUP:  return "WiFi 配网中…";
    case XZ_STATE_CONNECTING:  return "连接服务器中…";
    case XZ_STATE_LISTENING:   return "聆听中…";
    case XZ_STATE_SPEAKING:    return "回答中…";
    case XZ_STATE_ERROR:       return "出错了，点按重试";
    default:                   return "";
    }
}

static void talk_btn_event(lv_event_t *e);

void ui_main_create(void)
{
    lv_obj_t *scr = lv_scr_act();

    /* 标题 */
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, ui_font_cn_48, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title, "AI 小智");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    /* 状态行 */
    g_state_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_state_label, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(g_state_label, lv_color_hex(0x4FC3F7), 0);
    lv_label_set_text(g_state_label, state_text(XZ_STATE_IDLE));
    lv_obj_align(g_state_label, LV_ALIGN_TOP_MID, 0, 96);

    /* 服务器信息行（M2：session / 验证码） */
    g_net_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_net_label, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(g_net_label, lv_color_hex(0x9E9E9E), 0);
    lv_label_set_text(g_net_label, "未连接服务器");
    lv_obj_align(g_net_label, LV_ALIGN_TOP_MID, 0, 150);

    /* 聊天区（滚动容器 + 气泡纵向排列） */
    g_chat_cont = lv_obj_create(scr);
    lv_obj_set_size(g_chat_cont, 440, 260);
    lv_obj_align(g_chat_cont, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_set_style_bg_color(g_chat_cont, lv_color_hex(0x1A2129), 0);
    lv_obj_set_style_bg_opa(g_chat_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_chat_cont, 0, 0);
    lv_obj_set_style_radius(g_chat_cont, 12, 0);
    lv_obj_set_style_pad_all(g_chat_cont, 8, 0);
    lv_obj_set_scroll_dir(g_chat_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_chat_cont, LV_SCROLLBAR_MODE_OFF);

    g_chat_box = lv_obj_create(g_chat_cont);
    lv_obj_set_size(g_chat_box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(g_chat_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_chat_box, 0, 0);
    lv_obj_set_style_pad_all(g_chat_box, 0, 0);
    lv_obj_set_flex_flow(g_chat_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(g_chat_box, LV_OBJ_FLAG_SCROLLABLE);

    /* 说话按钮：点按一次开始（VAD 自动断句），说话中再点提前结束 */
    g_talk_btn = lv_btn_create(scr);
    lv_obj_set_size(g_talk_btn, 120, 120);
    lv_obj_align(g_talk_btn, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_radius(g_talk_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_talk_btn, lv_color_hex(0x1A6FB5), 0);
    g_talk_lbl = lv_label_create(g_talk_btn);
    lv_obj_set_style_text_font(g_talk_lbl, ui_font_cn_32, 0);
    lv_label_set_text(g_talk_lbl, LV_SYMBOL_AUDIO);
    lv_obj_center(g_talk_lbl);
    lv_obj_add_event_cb(g_talk_btn, talk_btn_event, LV_EVENT_CLICKED, NULL);
}

/* 说话按钮：toggle talk_start/stop */
static void talk_btn_event(lv_event_t *e)
{
    (void)e;
    if (xz_protocol_talking()) {
        xz_protocol_talk_stop();
        ui_main_on_state(XZ_STATE_IDLE, "talk stop by button");
    } else {
        if (xz_protocol_talk_start() == 0) {
            ui_main_on_state(XZ_STATE_LISTENING, "talk start");
        } else {
            ui_main_on_state(XZ_STATE_ERROR, "talk start fail（未连接？）");
        }
    }
    /* 按钮态跟随 */
    int on = xz_protocol_talking();
    lv_obj_set_style_bg_color(g_talk_btn,
        lv_color_hex(on ? 0xD32F2F : 0x1A6FB5), 0);
    lv_label_set_text(g_talk_lbl, on ? LV_SYMBOL_STOP : LV_SYMBOL_AUDIO);
}

void ui_main_on_state(xz_state_t s, const char *reason)
{
    if (g_state_label == NULL)
        return;
    lv_label_set_text(g_state_label, state_text(s));
    if (reason && strlen(reason) > 0)
        printf("[ui] state=%d reason=%s\n", (int)s, reason);
}

/* ---- 服务器信息行（M2；任意线程可调，lv_async_call 转主线程） ---- */
typedef struct {
    char l1[128];
    char l2[192];
} net_info_msg_t;

static void net_info_async(void *p)
{
    net_info_msg_t *m = (net_info_msg_t *)p;
    if (g_net_label == NULL) { free(m); return; }
    if (m->l2[0])
        lv_label_set_text_fmt(g_net_label, "%s\n%s", m->l1, m->l2);
    else
        lv_label_set_text(g_net_label, m->l1);
    lv_obj_align(g_net_label, LV_ALIGN_TOP_MID, 0, 150);
    free(m);
}

void ui_main_on_server_info(const char *line1, const char *line2)
{
    net_info_msg_t *m = (net_info_msg_t *)malloc(sizeof(*m));
    if (!m) return;
    snprintf(m->l1, sizeof(m->l1), "%s", line1 ? line1 : "");
    snprintf(m->l2, sizeof(m->l2), "%s", line2 ? line2 : "");
    lv_async_call(net_info_async, m);
}

/* ---- 聊天气泡（stt/tts 文本；内部 lv_async_call 转主线程） ---- */
typedef struct {
    uint8_t from_user;
    char text[256];
} chat_msg_t;

static void chat_add_async(void *p)
{
    chat_msg_t *m = (chat_msg_t *)p;
    lv_obj_t *bubble = lv_label_create(g_chat_box);
    lv_obj_set_style_text_font(bubble, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(bubble,
        lv_color_hex(m->from_user ? 0x81C784 : 0xFFFFFF), 0);
    lv_label_set_text(bubble, m->text);
    /* 用户右对齐（绿），AI 左对齐（白）；长文本限宽换行 */
    lv_obj_set_width(bubble, 360);
    lv_label_set_long_mode(bubble, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(bubble,
        m->from_user ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_scroll_to_view(bubble, LV_ANIM_ON);
    /* 上限 8 条，超出删最旧（防 LVGL 内存涨） */
    uint32_t cnt = lv_obj_get_child_cnt(g_chat_box);
    if (cnt > 8)
        lv_obj_del(lv_obj_get_child(g_chat_box, 0));
    free(m);
}

void ui_main_add_chat(uint8_t from_user, const char *text)
{
    if (text == NULL || text[0] == '\0')
        return;
    chat_msg_t *m = (chat_msg_t *)malloc(sizeof(chat_msg_t));
    if (m == NULL)
        return;
    m->from_user = from_user;
    snprintf(m->text, sizeof(m->text), "%s", text);
    lv_async_call(chat_add_async, m);
}

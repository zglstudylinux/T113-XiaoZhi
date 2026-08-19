/*
 * ui_main.c — 小智主界面（480×640，M0 骨架版）
 *
 * M0：标题 + 状态行 + 聊天区（占位）+ 说话按钮（占位）
 * M3/M4 接入实际数据后逐步完善。
 */
#include "ui_main.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *g_state_label;
static lv_obj_t *g_chat_cont;

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

    /* 聊天区（滚动容器，M3 接入气泡） */
    g_chat_cont = lv_obj_create(scr);
    lv_obj_set_size(g_chat_cont, 440, 340);
    lv_obj_align(g_chat_cont, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_bg_color(g_chat_cont, lv_color_hex(0x1A2129), 0);
    lv_obj_set_style_bg_opa(g_chat_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_chat_cont, 0, 0);
    lv_obj_set_style_radius(g_chat_cont, 12, 0);
    lv_obj_clear_flag(g_chat_cont, LV_OBJ_FLAG_SCROLLABLE);  /* M3 开滚动 */

    /* 说话按钮（占位，M3 接 listen start/stop） */
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 120, 120);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A6FB5), 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, ui_font_cn_32, 0);
    lv_label_set_text(lbl, LV_SYMBOL_AUDIO);
    lv_obj_center(lbl);
}

void ui_main_on_state(xz_state_t s, const char *reason)
{
    if (g_state_label == NULL)
        return;
    lv_label_set_text(g_state_label, state_text(s));
    if (reason && strlen(reason) > 0)
        printf("[ui] state=%d reason=%s\n", (int)s, reason);
}

/* ---- 跨线程入口（M3 起用；现仅主线程路径） ---- */
typedef struct {
    uint8_t from_user;
    char text[256];
} chat_msg_t;

static void chat_add_async(void *p)
{
    chat_msg_t *m = (chat_msg_t *)p;
    lv_obj_t *bubble = lv_label_create(g_chat_cont);
    lv_obj_set_style_text_font(bubble, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(bubble,
        lv_color_hex(m->from_user ? 0x81C784 : 0xFFFFFF), 0);
    lv_label_set_text(bubble, m->text);
    lv_obj_align(bubble, m->from_user ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT,
                 0, 0);
    free(m);   /* lv_async_call 投递的堆消息，用完即释放（bt-speaker 模式） */
}

void ui_main_add_chat(uint8_t from_user, const char *text)
{
    chat_msg_t *m = (chat_msg_t *)malloc(sizeof(chat_msg_t));
    if (m == NULL)
        return;
    m->from_user = from_user;
    snprintf(m->text, sizeof(m->text), "%s", text ? text : "");
    lv_async_call(chat_add_async, m);
}

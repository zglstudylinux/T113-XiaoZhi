/*
 * ui_main.c — 小智主界面（480×640，M5 产品化）
 *
 * 版面：全屏夜空背景图 → 顶部标题 → 中央表情脸（ui_face）→
 * 状态行 → 服务器信息行 → 半透明聊天气泡 → 底部胶囊说话按钮。
 * stt/tts 气泡、talk 按钮 toggle 逻辑沿用 M3。
 */
#include "ui_main.h"
#include "ui_face.h"
#include "ui_navigate.h"
#include "ui_msg.h"
#include "ui_menu.h"
#include "ui_wifi_setup.h"
#include "net/xz_protocol.h"
#include "wifi/wifi_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BG_PATH  "S:/mnt/UDISK/xiaozhi/images/bg_main.png"

static lv_obj_t *g_bg;
static lv_obj_t *g_state_label;
static lv_obj_t *g_net_label;
static lv_obj_t *g_chat_cont;
static lv_obj_t *g_chat_box;      /* 气泡纵向排列容器 */
static lv_obj_t *g_talk_btn;
static lv_obj_t *g_talk_lbl;
static lv_obj_t *g_wifi_label;    /* 无 WiFi 提示行（含验证码场景复用） */
static int         g_page_gone = 0;

static const char *state_text(xz_state_t s)
{
    switch (s) {
    case XZ_STATE_IDLE:        return "点按下方说话";
    case XZ_STATE_WIFI_SETUP:  return "WiFi 配网中…";
    case XZ_STATE_CONNECTING:  return "连接服务器中…";
    case XZ_STATE_LISTENING:   return "聆听中…";
    case XZ_STATE_SPEAKING:    return "回答中…";
    case XZ_STATE_ERROR:       return "出错了，点按重试";
    default:                   return "";
    }
}

static void talk_btn_event(lv_event_t *e);
void ui_wifi_setup_open(lv_obj_t *scr);       /* 文件尾：配网页适配器 */
void xz_app_wifi_setup_done(int connected);   /* main.c 提供 */

static void back_btn_event(lv_event_t *e)
{
    (void)e;
    /* 回菜单前收尾：停采集会话/停播，协议线程继续跑（重进即复用） */
    if (xz_protocol_talking())
        xz_protocol_talk_stop();
    ui_nav_go(ui_menu_create);        /* ui_menu.h */
}

/* 离开主界面（ui_nav_go 切走前调用）：停 face 轮播 + 异步消息丢弃 */
static void ui_main_leave(void)
{
    g_page_gone = 1;
    ui_face_leave();
    g_state_label = g_net_label = g_chat_cont = g_chat_box = NULL;
    g_talk_btn = g_talk_lbl = g_wifi_label = NULL;
}

void ui_main_create(lv_obj_t *scr)
{
    g_page_gone = 0;
    ui_nav_set_leave_cb(ui_main_leave);

    /* screen 由 ui_nav_go 创建/加载/删旧页（统一跳转防重叠） */

    /* 全屏背景图（星空），置于最底层 */
    g_bg = lv_img_create(scr);
    lv_img_set_src(g_bg, BG_PATH);
    lv_obj_set_pos(g_bg, 0, 0);
    lv_obj_set_size(g_bg, 480, 640);
    lv_obj_move_to_index(g_bg, 0);

    /* 左上返回按钮（回菜单页） */
    lv_obj_t *back = lv_btn_create(scr);
    lv_obj_set_size(back, 108, 52);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 16, 14);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x223042), 0);
    lv_obj_set_style_radius(back, 26, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_btn_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_obj_set_style_text_font(back_lbl, ui_font_cn_24, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x8FA3B8), 0);
    lv_label_set_text(back_lbl, "返回");
    lv_obj_center(back_lbl);

    /* 标题（返回键右侧） */
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title, "AI 小智");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 140, 22);

    /* 表情脸（中央 300×300） */
    ui_face_create(scr);

    /* WiFi 态提前检测：决定状态行/信息行内容（未连网时隐藏服务器行） */
    int no_wifi = (wifi_manager_state() != WIFI_STATE_CONNECTED);

    /* 状态行（脸下方）：未连 WiFi 不显示"点按下方说话"（会误导） */
    g_state_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_state_label, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(g_state_label, lv_color_hex(0x4FC3F7), 0);
    lv_label_set_text(g_state_label,
        no_wifi ? "未连接 WiFi" : state_text(XZ_STATE_IDLE));
    /* 状态行要压在聊天区上方（聊天区顶 432），太低会与对话框叠字 */
    lv_obj_align(g_state_label, LV_ALIGN_TOP_MID, 0, 350);

    if (no_wifi) {
        /* 黄字指引占信息行位置（同一行不叠字） */
        g_wifi_label = lv_label_create(scr);
        lv_obj_set_style_text_font(g_wifi_label, ui_font_cn_24, 0);
        lv_obj_set_style_text_color(g_wifi_label, lv_color_hex(0xF7B500), 0);
        lv_label_set_text(g_wifi_label, "点下方按钮先配网");
        lv_obj_align(g_wifi_label, LV_ALIGN_TOP_MID, 0, 392);
    } else {
        /* 服务器信息行（session / 验证码） */
        g_net_label = lv_label_create(scr);
        lv_obj_set_style_text_font(g_net_label, ui_font_cn_24, 0);
        lv_obj_set_style_text_color(g_net_label, lv_color_hex(0x8FA3B8), 0);
        lv_label_set_text(g_net_label, "未连接服务器");
        lv_obj_align(g_net_label, LV_ALIGN_TOP_MID, 0, 392);
    }

    /* 聊天区（滚动容器 + 气泡纵向排列） */
    g_chat_cont = lv_obj_create(scr);
    lv_obj_set_size(g_chat_cont, 440, 106);
    lv_obj_align(g_chat_cont, LV_ALIGN_BOTTOM_MID, 0, -104);
    lv_obj_set_style_bg_color(g_chat_cont, lv_color_hex(0x0A0E14), 0);
    lv_obj_set_style_bg_opa(g_chat_cont, LV_OPA_40, 0);
    lv_obj_set_style_border_width(g_chat_cont, 0, 0);
    lv_obj_set_style_radius(g_chat_cont, 12, 0);
    lv_obj_set_style_pad_all(g_chat_cont, 6, 0);
    lv_obj_set_scroll_dir(g_chat_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_chat_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_chat_cont, LV_OBJ_FLAG_HIDDEN);   /* 空聊天区不显示 */

    g_chat_box = lv_obj_create(g_chat_cont);
    lv_obj_set_size(g_chat_box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(g_chat_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_chat_box, 0, 0);
    lv_obj_set_style_pad_all(g_chat_box, 0, 0);
    lv_obj_set_flex_flow(g_chat_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(g_chat_box, LV_OBJ_FLAG_SCROLLABLE);

    /* 胶囊说话按钮：点按一次开始（VAD 自动断句），说话中再点提前结束 */
    g_talk_btn = lv_btn_create(scr);
    lv_obj_set_size(g_talk_btn, 200, 64);
    lv_obj_align(g_talk_btn, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_style_radius(g_talk_btn, 32, 0);
    lv_obj_set_style_bg_color(g_talk_btn, lv_color_hex(0x1A6FB5), 0);
    lv_obj_set_style_shadow_width(g_talk_btn, 30, 0);
    lv_obj_set_style_shadow_color(g_talk_btn, lv_color_hex(0x4FC3F7), 0);
    g_talk_lbl = lv_label_create(g_talk_btn);
    lv_obj_set_style_text_font(g_talk_lbl, ui_font_cn_24, 0);
    lv_label_set_text(g_talk_lbl, no_wifi ? "去连 WiFi" : "点按说话");
    lv_obj_center(g_talk_lbl);
    if (no_wifi) {
        lv_obj_set_style_bg_color(g_talk_btn, lv_color_hex(0xB26A00), 0);
        lv_obj_set_style_shadow_color(g_talk_btn, lv_color_hex(0xF7B500), 0);
    }
    lv_obj_add_event_cb(g_talk_btn, talk_btn_event, LV_EVENT_CLICKED, NULL);

    /* 进入小智页：WiFi 就绪则确保协议在跑（幂等；覆盖菜单/配网等入口） */
    xz_app_ensure_protocol();
}

/* 说话按钮：toggle talk_start/stop */
static void talk_btn_event(lv_event_t *e)
{
    (void)e;
    /* 无 WiFi：按钮变配网入口，跳配网页（成功后由 wifi 流程回小智页） */
    if (wifi_manager_state() != WIFI_STATE_CONNECTED) {
        ui_nav_go(ui_wifi_setup_open);      /* 见文件尾适配器 */
        return;
    }
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
    lv_label_set_text(g_talk_lbl, on ? "结束" : "点按说话");
}

void ui_main_on_state(xz_state_t s, const char *reason)
{
    if (g_page_gone || g_state_label == NULL)
        return;
    /* 无 WiFi 时状态行固定（协议连不上刷屏没意义） */
    if (wifi_manager_state() != WIFI_STATE_CONNECTED &&
        (s == XZ_STATE_IDLE || s == XZ_STATE_CONNECTING)) {
        lv_label_set_text(g_state_label, "未连接 WiFi");
        return;
    }
    lv_label_set_text(g_state_label, state_text(s));
    if (ui_font_cn_24) {             /* 状态行用 24 号更协调 */
        lv_obj_set_style_text_font(g_state_label, ui_font_cn_32, 0);
    }
    ui_face_set_state(s);            /* 表情脸联动 */
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
    if (g_page_gone) { free(m); return; }
    /* no_wifi 进的页面没建 net 行：复用黄字 label 当信息行 */
    if (g_net_label == NULL && g_wifi_label != NULL) {
        g_net_label = g_wifi_label;
        g_wifi_label = NULL;
        lv_obj_set_style_text_color(g_net_label, lv_color_hex(0x8FA3B8), 0);
    }
    if (g_net_label == NULL) { free(m); return; }
    if (m->l2[0])
        lv_label_set_text_fmt(g_net_label, "%s\n%s", m->l1, m->l2);
    else
        lv_label_set_text(g_net_label, m->l1);
    lv_obj_align(g_net_label, LV_ALIGN_TOP_MID, 0, 392);
    free(m);
}

void ui_main_on_server_info(const char *line1, const char *line2)
{
    net_info_msg_t *m = (net_info_msg_t *)malloc(sizeof(*m));
    if (!m) return;
    snprintf(m->l1, sizeof(m->l1), "%s", line1 ? line1 : "");
    snprintf(m->l2, sizeof(m->l2), "%s", line2 ? line2 : "");
    ui_msg_post(net_info_async, m);
}

/* ---- 聊天气泡（stt/tts 文本；内部 lv_async_call 转主线程） ---- */
typedef struct {
    uint8_t from_user;
    char text[256];
} chat_msg_t;

static void scroll_chat_to_bottom(void *unused);

static void chat_add_async(void *p)
{
    chat_msg_t *m = (chat_msg_t *)p;
    if (g_page_gone || g_chat_box == NULL) { free(m); return; }

    if (g_chat_cont && lv_obj_has_flag(g_chat_cont, LV_OBJ_FLAG_HIDDEN))
        lv_obj_clear_flag(g_chat_cont, LV_OBJ_FLAG_HIDDEN);

    /* 行容器：占满一行，气泡靠左/靠右放（flex 列布局忽略子项 align） */
    lv_obj_t *row = lv_obj_create(g_chat_box);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* 气泡容器（圆角半透明，让星空透出） */
    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_set_style_bg_color(bubble,
        lv_color_hex(m->from_user ? 0x2E7D32 : 0x2A3644), 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_70, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_radius(bubble, 10, 0);
    lv_obj_set_style_pad_hor(bubble, 10, 0);
    lv_obj_set_style_pad_ver(bubble, 4, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(bubble);
    lv_obj_set_style_text_font(lbl, ui_font_cn_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl, m->text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, 340);

    /* 用户绿泡靠右，AI 灰泡靠左；限宽换行 */
    lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(bubble, 360, 0);
    lv_obj_align(bubble, m->from_user ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID,
                 0, 0);

    /* 上限 8 条，超出删最旧（防 LVGL 内存涨） */
    uint32_t cnt = lv_obj_get_child_cnt(g_chat_box);
    if (cnt > 8)
        lv_obj_del(lv_obj_get_child(g_chat_box, 0));

    /* 布局重算后同步贴底一次（滚动不变量：内容高-视口高，不依赖随滚动
     * 平移的子坐标）。首条消息时聊天容器刚从 HIDDEN 转可见，同拍内几何
     * 可能仍是隐藏期的旧值 → 同步滚可能漏；故再 post 一拍 lv_async 兜底
     * （运行在下一刷新周期，几何必为最终值；重滚幂等，平时无感）。 */
    lv_obj_update_layout(g_chat_box);
    scroll_chat_to_bottom(NULL);
    lv_async_call(scroll_chat_to_bottom, NULL);
    free(m);
}

/* 贴底：同步版 + lv_async 延迟版共用（签名匹配 lv_async_cb_t） */
static void scroll_chat_to_bottom(void *unused)
{
    (void)unused;
    if (g_page_gone || g_chat_box == NULL || g_chat_cont == NULL)
        return;
    lv_obj_update_layout(g_chat_box);
    lv_coord_t content_h = lv_obj_get_height(g_chat_box);
    lv_coord_t view_h = lv_obj_get_height(g_chat_cont)
                      - lv_obj_get_style_pad_top(g_chat_cont, LV_PART_MAIN)
                      - lv_obj_get_style_pad_bottom(g_chat_cont, LV_PART_MAIN);
    lv_coord_t overscroll = content_h - view_h;
    if (overscroll > 0)
        lv_obj_scroll_to_y(g_chat_cont, overscroll, LV_ANIM_OFF);
}

void ui_main_add_chat(uint8_t from_user, const char *text)
{
    if (text == NULL || text[0] == '\0')
        return;
    chat_msg_t *m = (chat_msg_t *)malloc(sizeof(*m));
    if (m == NULL)
        return;
    m->from_user = from_user;
    snprintf(m->text, sizeof(m->text), "%s", text);
    ui_msg_post(chat_add_async, m);
}

/* 配网页适配器（ui_nav_go 需要.lv_obj_t* 参数形式） */
void ui_wifi_setup_open(lv_obj_t *scr)
{
    ui_wifi_setup_create_ex(scr, xz_app_wifi_setup_done);
}

/* 未绑定设备：状态行大字显示 6 位验证码（用户到 xiaozhi.me 控制台绑定） */
void ui_main_show_activation(const char *code)
{
    if (code == NULL || !code[0])
        return;
    if (g_page_gone || g_state_label == NULL)
        return;
    char txt[64];
    snprintf(txt, sizeof(txt), "验证码 %s", code);
    lv_label_set_text(g_state_label, txt);
    lv_obj_set_style_text_color(g_state_label, lv_color_hex(0xF7B500), 0);
}

/*
 * ui_menu.c — 启动菜单页
 *
 * 按钮项：图标（素材 PNG，缺失时退化为文字符号）+ 标题 + 副标题。
 * 新增入口：g_pages[] 加一项 + 对应 create 函数。
 */
#include "ui_menu.h"
#include "ui_navigate.h"
#include "ui_main.h"
#include "ui_wifi_setup.h"
#include "ui_res.h"
#include "wifi/wifi_manager.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;               /* images/ 下文件名（经 ui_res_img 解析） */
    const char *symbol;             /* 图标退化/兜底 */
    const char *title;
    const char *sub;
    void (*open)(void);             /* 点按动作 */
} menu_item_t;

static void open_xiaozhi(void);
static void open_wifi(void);

static const menu_item_t g_pages[] = {
    { "menu_xiaozhi.png", LV_SYMBOL_AUDIO, "AI 小智",
      "语音助手", open_xiaozhi },
    { "menu_wifi.png", LV_SYMBOL_WIFI, "WiFi 设置",
      "扫描 / 连接网络", open_wifi },
};

/* ---------------- 入口动作 ---------------- */

static void open_xiaozhi(void)
{
    ui_nav_go(ui_main_create);
}

/* 配网完成（主线程，配网页内回调）：成功 → 直接进小智页开聊；失败留在配网页 */
static void wifi_setup_done(int connected)
{
    printf("[menu] wifi_setup_done connected=%d\n", connected);
    if (connected)
        ui_nav_go(ui_main_create);
}

/* ui_wifi_setup_create_ex 带回调参数，包一层适配 ui_page_create_t */
static void wifi_setup_create_adapter(lv_obj_t *scr)
{
    ui_wifi_setup_create_ex(scr, wifi_setup_done);
}

static void open_wifi(void)
{
    ui_nav_go(wifi_setup_create_adapter);
}

/* ---------------- UI ---------------- */

static void item_click_cb(lv_event_t *e)
{
    menu_item_t *it = (menu_item_t *)lv_event_get_user_data(e);
    it->open();
}

static void add_item(lv_obj_t *scr, const menu_item_t *it, int idx)
{

    /* 卡片按钮：左图标 + 中间标题/副标题 + 右侧箭头 */
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 420, 150);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 150 + idx * 180);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x223042), 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 24, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(btn, item_click_cb, LV_EVENT_CLICKED, (void *)it);

    /* 图标：PNG 素材（可解码才用），否则 symbol 字体兜底。
     * 注意 lv_img_get_src(src) 按 src 查询解码器，不是传对象 */
    lv_obj_t *icon = NULL;
    if (it->name && lv_img_get_src(ui_res_img(it->name)) != NULL) {
        icon = lv_img_create(btn);
        lv_img_set_src(icon, ui_res_img(it->name));
    }
    if (icon == NULL) {
        icon = lv_label_create(btn);
        lv_obj_set_style_text_font(icon, ui_font_cn_48, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(0x4FC3F7), 0);
        lv_label_set_text(icon, it->symbol);
    }
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 28, 0);
    lv_obj_set_size(icon, 96, 96);

    /* 标题 + 副标题 */
    lv_obj_t *title = lv_label_create(btn);
    lv_obj_set_style_text_font(title, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title, it->title);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 150, -22);

    lv_obj_t *sub = lv_label_create(btn);
    lv_obj_set_style_text_font(sub, ui_font_cn_24, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x8FA3B8), 0);
    lv_label_set_text(sub, it->sub);
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, 150, 24);

    /* 右侧 "进入" 提示（FreeType 无 LVGL symbol 字形，用文字） */
    lv_obj_t *arrow = lv_label_create(btn);
    lv_obj_set_style_text_font(arrow, ui_font_cn_24, 0);
    lv_obj_set_style_text_color(arrow, lv_color_hex(0x5A6B7D), 0);
    lv_label_set_text(arrow, "进入");
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -24, 0);
}

void ui_menu_create(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10141C), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, ui_font_cn_48, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title, "AI 小智");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 24, 22);

    lv_obj_t *sub = lv_label_create(scr);
    lv_obj_set_style_text_font(sub, ui_font_cn_24, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x8FA3B8), 0);
    lv_label_set_text_fmt(sub, "T113 语音助手  ·  WiFi %s",
        wifi_manager_state() == WIFI_STATE_CONNECTED ? "已连接" : "未连接");
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 26, 86);

    for (unsigned i = 0; i < sizeof(g_pages) / sizeof(g_pages[0]); i++)
        add_item(scr, &g_pages[i], (int)i);
}

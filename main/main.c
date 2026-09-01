/*
 * T113 AI 小智 — 主入口（P4 解耦后瘦身为纯装配层）
 *
 * 职责：初始化 port（显示/输入/字体）→ ui_facade（订阅业务事件）→
 * 画启动页 → 后台 WiFi boot → 主循环（LVGL pump）。
 * 协议事件分发在 app/controller.c，UI 更新在 ui/ui_facade.c，
 * UI→业务调用走 app/app_api.h —— 本文件不含业务/UI 细节逻辑。
 *
 * 显示：sunxifb /dev/fb0，480x640 竖屏（host 为 SDL 窗口）
 * 触摸：evdev /dev/input/event4（dx_touch；host 为鼠标）
 * 音频：ALSA hw:audiocodec capture(16k mono) + playback
 * 网络：WiFi(wpa_supplicant) + WebSocket(wss, OpenSSL) → 小智服务器
 */
#include "lvgl/lvgl.h"
#include "port_display.h"
#include "ui_navigate.h"
#include "ui_facade.h"
#include "ui_main.h"
#include "ui_menu.h"
#include "app/app_api.h"
#include "app/app_events.h"
#include "app/app_event_bus.h"
#include "wifi/wifi_manager.h"
#include "osal/osal_thread.h"
#include "osal/osal_time.h"
#include "ui_msg.h"
#include <stdlib.h>
#include <stdio.h>

/* ===== WiFi 启动流程（M1）=====
 * 后台线程：wifi_manager_open（加载驱动+起 wpa_supplicant，可能 5~10s）
 *   → 有保存配置则 auto_connect → 成功进主页（并启动协议）/
 *     失败或无配置进菜单页。
 * 结果经 app_event_post(APP_EVT_NAVIGATE) 投递，ui_facade 主线程切页。
 * （切页不能在工作线程直接做：ui_nav_go 触碰 LVGL，非线程安全） */

typedef struct { int ok; } wifi_boot_msg_t;

static void wifi_boot_done(void *p)
{
    wifi_boot_msg_t *m = (wifi_boot_msg_t *)p;
    if (m->ok) {
        /* WiFi 已就绪：直达小智页（app_api 内部发 APP_EVT_NAVIGATE +
         * 启动协议），连接成功即可对话 */
        app_api_wifi_setup_done(1);
    } else {
        /* 自动重连失败：进菜单页（可去 WiFi 设置手动配网） */
        app_event_post(APP_EVT_NAVIGATE, NULL, APP_PAGE_MENU, NULL, 0);
    }
    free(m);
}

static void *wifi_boot_thread(void *arg)
{
    (void)arg;
    wifi_boot_msg_t *m = (wifi_boot_msg_t *)malloc(sizeof(*m));
    m->ok = 0;

    if (wifi_manager_open() == 0) {
        if (wifi_manager_auto_connect() == 0) {
            printf("[main] WiFi 自动重连成功: %s ip=%s\n",
                   wifi_manager_get_ssid(), wifi_manager_get_ip());
            m->ok = 1;
        } else {
            printf("[main] 自动重连失败，进菜单页\n");
        }
    } else {
        printf("[main] wifi_manager_open 失败，进菜单页\n");
    }
    /* 成败都先回主线程再切页/启动（ui_msg 泵线程安全） */
    ui_msg_post(wifi_boot_done, m);
    return NULL;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);   /* 后台进程日志实时落文件 */

    lv_init();
    if (port_display_init() != 0) {   /* fb/evdev/PNG/字体（port/*） */
        return 1;
    }

    /* ===== UI + 事件泵 ===== */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    ui_facade_init();   /* ui_msg 泵 + 订阅业务事件总线（P4） */

    /* ===== 启动过渡页 + WiFi 后台初始化（M1） ===== */
    lv_obj_t *boot_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(boot_scr, lv_color_hex(0x101418), 0);
    lv_scr_load(boot_scr);
    lv_obj_t *boot_label = lv_label_create(boot_scr);
    lv_obj_set_style_text_font(boot_label, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(boot_label, lv_color_hex(0x4FC3F7), 0);
    lv_label_set_text(boot_label, "AI 小智 启动中…\n正在准备 WiFi");
    lv_obj_align(boot_label, LV_ALIGN_CENTER, 0, 0);

    osal_thread_t tid;
    if (osal_thread_create(&tid, wifi_boot_thread, NULL) == OSAL_OK)
        osal_thread_detach(tid);

    /* ===== 主循环 ===== */
    while (1) {
        uint32_t time_till_next = lv_timer_handler();
        osal_sleep_ms(time_till_next > 0 ? time_till_next : 1);
    }

    /* unreachable（主循环常驻）；收尾见 app 层与 port_display */
    return 0;
}

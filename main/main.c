/*
 * T113 AI 小智 — 主入口
 *
 * 显示：sunxifb /dev/fb0，480x640 竖屏（复用 bt-speaker 验证配置）
 * 触摸：evdev /dev/input/event4（dx_touch）
 * 音频：ALSA hw:audiocodec capture(16k mono) + playback（M3/M4 接入）
 * 网络：WiFi(wpa_supplicant) + WebSocket(wss, OpenSSL) → 小智服务器（M1/M2 接入）
 */
#include "lvgl/lvgl.h"
#include "port_display.h"
#include "app_state.h"
#include "ui_navigate.h"
#include "ui_msg.h"
#include "ui_main.h"
#include "ui_menu.h"
#include "wifi/wifi_manager.h"
#include "net/xz_protocol.h"
#include "ui_face.h"
#include <stdlib.h>
#include <stdio.h>
#include "osal/osal_thread.h"
#include "osal/osal_time.h"

/* custom_tick_get（LVGL 时基）见 port/linux/port_tick.c */

/* ===== WiFi 启动流程（M1）=====

/* ===== WiFi 启动流程（M1）=====
 * 后台线程：wifi_manager_open（加载驱动+起 wpa_supplicant，可能 5~10s）
 *   → 有保存配置则 auto_connect → 成功进主页 / 失败进配网页
 *   → 无配置直接进配网页
 * 结果经 ui_msg_post 投递到主循环执行（lv_async 非线程安全）。 */

typedef struct { int ok; } wifi_boot_msg_t;

/* ===== 小智协议事件（协议线程调用 → ui_msg_post 转主循环） ===== */

typedef struct { int evt; char data[512]; } xz_evt_msg_t;

static void xz_evt_async(void *p)
{
    xz_evt_msg_t *m = (xz_evt_msg_t *)p;
    switch (m->evt) {
    case XZ_EVT_ACTIVATION:
        /* 未绑定设备：验证码大字屏显 + 信息行提示去控制台绑定 */
        ui_main_on_server_info("请到 xiaozhi.me 控制台输入验证码绑定设备", "");
        ui_main_show_activation(m->data);
        break;
    case XZ_EVT_HELLO_OK:
        /* 单行显示（带 session 两行会压到下方聊天区顶） */
        ui_main_on_server_info("服务器已连接", "");
        app_state_set(XZ_STATE_IDLE, "hello ok");
        break;
    case XZ_EVT_GOODBYE:
        ui_main_on_server_info("设备未绑定", m->data);
        app_state_set(XZ_STATE_IDLE, "goodbye");
        break;
    case XZ_EVT_DISCONNECTED:
        ui_main_on_server_info("服务器连接断开，重试中…", "");
        app_state_set(XZ_STATE_CONNECTING, "disconnected");
        break;
    case XZ_EVT_CONNECTED:
        ui_main_on_server_info("已连上，握手中…", "");
        break;
    case XZ_EVT_STT:
        ui_main_add_chat(1, m->data);
        app_state_set(XZ_STATE_IDLE, "stt");
        break;
    case XZ_EVT_TTS_SENTENCE:
        ui_main_add_chat(0, m->data);
        break;
    case XZ_EVT_TTS_START:
        app_state_set(XZ_STATE_SPEAKING, "tts start");
        break;
    case XZ_EVT_TTS_STOP:
        app_state_set(XZ_STATE_IDLE, "tts stop");
        break;
    case XZ_EVT_EMOTION:
        ui_face_set_emotion(m->data);
        break;
    default:
        break;
    }
    free(m);
}

static void xz_evt_cb(int event, const char *data, void *user)
{
    (void)user;
    xz_evt_msg_t *m = (xz_evt_msg_t *)malloc(sizeof(*m));
    if (!m) return;
    m->evt = event;
    snprintf(m->data, sizeof(m->data), "%s", data ? data : "");
    ui_msg_post(xz_evt_async, m);
}

/* WiFi 就绪后启动协议（连 xiaozhi.me，未绑定设备由服务器下发验证码）。
 * 幂等：已在跑则不重复启动（反复进出小智页都安全） */
static void xiaozhi_start(void)
{
    if (xz_protocol_started())
        return;
    if (xz_protocol_start(xz_evt_cb, NULL) == 0) {
        app_state_set(XZ_STATE_CONNECTING, "ws connecting");
        ui_main_on_server_info("连接服务器中…", "");
    }
}

/* 小智页 → 配网页 → 连接成功（主线程）：回小智页并确保协议在跑 */
void xz_app_wifi_setup_done(int connected)
{
    printf("[main] wifi_setup_done connected=%d\n", connected);
    if (connected) {
        ui_nav_go(ui_main_create);
        app_state_init();
        xiaozhi_start();
    }
}

/* UI 层入口：进入小智页时确保协议已启动（幂等）。
 * 覆盖所有进入路径：开机直进 / 菜单点入 / 配网成功跳入 */
void xz_app_ensure_protocol(void)
{
    if (wifi_manager_state() != WIFI_STATE_CONNECTED)
        return;
    xiaozhi_start();
}

static lv_obj_t *g_boot_scr;        /* 启动过渡页 */
static lv_obj_t *g_boot_label;

static void wifi_boot_done(void *p)
{
    wifi_boot_msg_t *m = (wifi_boot_msg_t *)p;
    if (m->ok) {
        /* WiFi 已就绪：直达小智页，连接成功即可对话 */
        ui_nav_go(ui_main_create);
        app_state_init();
        xiaozhi_start();
    } else {
        /* 自动重连失败：进菜单页（可去 WiFi 设置手动配网） */
        ui_nav_go(ui_menu_create);
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
            printf("[main] 自动重连失败，进配网页\n");
        }
    } else {
        printf("[main] wifi_manager_open 失败，进配网页\n");
    }
    ui_msg_post(wifi_boot_done, m);
    return NULL;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);   /* 后台进程日志实时落文件 */

    lv_init();
    if (port_display_init() != 0) {   /* fb/evdev/PNG/字体（port/linux） */
        return 1;
    }

    /* ===== UI ===== */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    ui_msg_init();   /* 线程→UI 消息泵（lv_async 非线程安全，见 ui_msg.h） */

    /* ===== 启动过渡页 + WiFi 后台初始化（M1） ===== */
    g_boot_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_boot_scr, lv_color_hex(0x101418), 0);
    lv_scr_load(g_boot_scr);
    g_boot_label = lv_label_create(g_boot_scr);
    lv_obj_set_style_text_font(g_boot_label, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(g_boot_label, lv_color_hex(0x4FC3F7), 0);
    lv_label_set_text(g_boot_label, "AI 小智 启动中…\n正在准备 WiFi");
    lv_obj_align(g_boot_label, LV_ALIGN_CENTER, 0, 0);

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

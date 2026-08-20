/*
 * T113 AI 小智 — 主入口
 *
 * 显示：sunxifb /dev/fb0，480x640 竖屏（复用 bt-speaker 验证配置）
 * 触摸：evdev /dev/input/event4（dx_touch）
 * 音频：ALSA hw:audiocodec capture(16k mono) + playback（M3/M4 接入）
 * 网络：WiFi(wpa_supplicant) + WebSocket(wss, OpenSSL) → 小智服务器（M1/M2 接入）
 */
#include "lvgl/lvgl.h"
#include "lv_drivers/display/sunxifb.h"
#include "lv_drivers/indev/evdev.h"
#include "lv_freetype.h"
#include "app_state.h"
#include "ui/ui_main.h"
#include "ui/ui_wifi_setup.h"
#include "wifi/wifi_manager.h"
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/* 板上资源路径（deploy.sh 推送目标）。
 * rootfs overlay 只有 ~8MB，资源放 /mnt/UDISK。 */
#define BOARD_RES_PATH   "/mnt/UDISK/xiaozhi"
#define FONT_CN_REGULAR  BOARD_RES_PATH "/fonts/SOURCEHANSANSCN_REGULAR.OTF"

/* UI 用字体（ui_main.c extern 引用） */
lv_font_t *ui_font_cn_32;
lv_font_t *ui_font_cn_48;

/* LVGL tick：LV_TICK_CUSTOM=1 时 custom_tick_get 直接供时基 */
uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if (start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = ((uint64_t)tv_start.tv_sec * 1000000
                    + (uint64_t)tv_start.tv_usec) / 1000;
    }
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms = ((uint64_t)tv_now.tv_sec * 1000000
                       + (uint64_t)tv_now.tv_usec) / 1000;
    return (uint32_t)(now_ms - start_ms);
}

/* ===== WiFi 启动流程（M1）=====
 * 后台线程：wifi_manager_open（加载驱动+起 wpa_supplicant，可能 5~10s）
 *   → 有保存配置则 auto_connect → 成功进主页 / 失败进配网页
 *   → 无配置直接进配网页
 * 结果经 lv_async_call 切 UI（线程安全）。 */

typedef struct { int ok; } wifi_boot_msg_t;

void wifi_setup_done_cb(int connected);

static lv_obj_t *g_boot_scr;        /* 启动过渡页 */
static lv_obj_t *g_boot_label;

static void wifi_boot_done(void *p)
{
    wifi_boot_msg_t *m = (wifi_boot_msg_t *)p;
    if (m->ok) {
        ui_main_create();
        app_state_init();
    } else {
        ui_wifi_setup_create(wifi_setup_done_cb);
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
    lv_async_call(wifi_boot_done, m);
    return NULL;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);   /* 后台进程日志实时落文件 */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    uint32_t rotated = LV_DISP_ROT_NONE;

    lv_init();
    sunxifb_init(rotated);

    static uint32_t width, height;
    sunxifb_get_sizes(&width, &height);
    printf("fb: %ux%u\n", width, height);

    static lv_color_t *buf;
    buf = (lv_color_t *)sunxifb_alloc(width * height * sizeof(lv_color_t),
                                      "xiaozhi");
    if (buf == NULL) {
        sunxifb_exit();
        printf("malloc draw buffer fail\n");
        return 1;
    }

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, width * height);

    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = sunxifb_flush;
    disp_drv.hor_res = width;
    disp_drv.ver_res = height;
    lv_disp_drv_register(&disp_drv);

    evdev_init();
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);

    /* ===== UI ===== */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    /* FreeType 中文字体（运行时加载 .otf） */
    lv_freetype_init(4, 4, 128 * 1024);
    static lv_ft_info_t ft48 = {
        .name = FONT_CN_REGULAR,
        .weight = 48,
        .style = FT_FONT_STYLE_NORMAL,
    };
    static lv_ft_info_t ft32 = {
        .name = FONT_CN_REGULAR,
        .weight = 32,
        .style = FT_FONT_STYLE_NORMAL,
    };
    if (lv_ft_font_init(&ft48) && lv_ft_font_init(&ft32)) {
        ui_font_cn_48 = ft48.font;
        ui_font_cn_32 = ft32.font;
    } else {
        printf("freetype font load FAIL: %s\n", FONT_CN_REGULAR);
    }

    /* ===== 启动过渡页 + WiFi 后台初始化（M1） ===== */
    g_boot_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_boot_scr, lv_color_hex(0x101418), 0);
    lv_scr_load(g_boot_scr);
    g_boot_label = lv_label_create(g_boot_scr);
    lv_obj_set_style_text_font(g_boot_label, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(g_boot_label, lv_color_hex(0x4FC3F7), 0);
    lv_label_set_text(g_boot_label, "AI 小智 启动中…\n正在准备 WiFi");
    lv_obj_align(g_boot_label, LV_ALIGN_CENTER, 0, 0);

    pthread_t tid;
    pthread_create(&tid, NULL, wifi_boot_thread, NULL);
    pthread_detach(tid);

    /* ===== 主循环 ===== */
    while (1) {
        uint32_t time_till_next = lv_timer_handler();
        usleep((time_till_next > 0 ? time_till_next : 1) * 1000);
    }

    app_state_deinit();
    wifi_manager_close();
    lv_ft_font_destroy(ui_font_cn_48);
    lv_ft_font_destroy(ui_font_cn_32);
    sunxifb_exit();
    return 0;
}

/* 配网页完成回调（主线程）：连接成功 → 进主界面 */
void wifi_setup_done_cb(int connected)
{
    printf("[main] wifi_setup_done connected=%d\n", connected);
    if (connected) {
        ui_main_create();
        app_state_init();
        /* 主界面顶部显示 WiFi 状态（M2 起在此显示服务器连接状态） */
    }
}

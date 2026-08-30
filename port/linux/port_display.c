/*
 * port/linux/port_display.c — T113 显示/输入平台初始化
 *
 * 从原 main.c 拆出（P2）：sunxifb + evdev + PNG + FreeType 三字号。
 * 字体指针 ui_font_cn_* 定义在此（ui_main.c extern 引用）。
 */
#include "port_display.h"
#include "lv_drivers/display/sunxifb.h"
#include "lv_drivers/indev/evdev.h"
#include "lvgl/src/extra/libs/png/lv_png.h"
#include "lv_freetype.h"
#include "osal/osal_log.h"

#include <stdio.h>

/* 板上资源路径（deploy.sh 推送目标）。
 * rootfs overlay 只有 ~8MB，资源放 /mnt/UDISK。 */
#define BOARD_RES_PATH   "/mnt/UDISK/xiaozhi"
#define FONT_CN_REGULAR  BOARD_RES_PATH "/fonts/SOURCEHANSANSCN_REGULAR.OTF"

/* UI 用字体（ui_main.c extern 引用） */
lv_font_t *ui_font_cn_24;
lv_font_t *ui_font_cn_32;
lv_font_t *ui_font_cn_48;

int port_display_init(void)
{
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    uint32_t rotated = LV_DISP_ROT_NONE;

    sunxifb_init(rotated);

    static uint32_t width, height;
    sunxifb_get_sizes(&width, &height);
    printf("fb: %ux%u\n", width, height);

    static lv_color_t *buf;
    buf = (lv_color_t *)sunxifb_alloc(width * height * sizeof(lv_color_t),
                                      "xiaozhi");
    if (buf == NULL) {
        sunxifb_exit();
        XZ_LOGE("port", "malloc draw buffer fail");
        return -1;
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

    /* PNG 解码器（表情脸/背景素材）+ FreeType 中文字体 */
    lv_png_init();
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
    static lv_ft_info_t ft24 = {
        .name = FONT_CN_REGULAR,
        .weight = 24,
        .style = FT_FONT_STYLE_NORMAL,
    };
    if (lv_ft_font_init(&ft48) && lv_ft_font_init(&ft32)
        && lv_ft_font_init(&ft24)) {
        ui_font_cn_48 = ft48.font;
        ui_font_cn_32 = ft32.font;
        ui_font_cn_24 = ft24.font;
    } else {
        XZ_LOGE("port", "freetype font load FAIL: %s", FONT_CN_REGULAR);
    }
    return 0;
}

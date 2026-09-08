/*
 * port/sdl/port_display.c — PC 模拟器显示/输入平台（host 构建）
 *
 * SDL2 窗口 480×640（竖屏，与板同尺寸）+ 鼠标当触摸 + 键盘。
 * SDL_HOR_RES/SDL_VER_RES 由 CMake -D 注入（lv_drv_conf.h 的 480×320
 * 横屏默认值不适配，该文件此二宏无 #ifndef 守卫，见风险 11）。
 * 字体/PNG 资源经 ui_res 运行时解析（仓库 assets/ 或 CI zip 布局）。
 */
#include "port_display.h"
#include "lv_drivers/sdl/sdl.h"
#include "lvgl/src/extra/libs/png/lv_png.h"
#include "lv_freetype.h"
#include "osal/osal_log.h"
#include "ui_res.h"

/* 字体文件名（目录经 ui_res_font 运行时解析，见 ui_res.h） */
#define FONT_CN_REGULAR  "SOURCEHANSANSCN_REGULAR.OTF"

/* UI 用字体（ui_main.c extern 引用） */
lv_font_t *ui_font_cn_24;
lv_font_t *ui_font_cn_32;
lv_font_t *ui_font_cn_48;

int port_display_init(void)
{
    sdl_init();

    static lv_disp_draw_buf_t disp_buf;
    /* LV_COLOR_DEPTH 32 与 SDL ARGB8888 纹理直拷（sdl_display_flush
     * 的 DEPTH==32 分支 memcpy），单缓冲即可 */
    static lv_color_t buf[SDL_HOR_RES * SDL_VER_RES];
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, SDL_HOR_RES * SDL_VER_RES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = sdl_display_flush;
    disp_drv.hor_res = SDL_HOR_RES;
    disp_drv.ver_res = SDL_VER_RES;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t mouse_drv;
    lv_indev_drv_init(&mouse_drv);
    mouse_drv.type = LV_INDEV_TYPE_POINTER;
    mouse_drv.read_cb = sdl_mouse_read;
    lv_indev_drv_register(&mouse_drv);

    static lv_indev_drv_t kb_drv;
    lv_indev_drv_init(&kb_drv);
    kb_drv.type = LV_INDEV_TYPE_KEYPAD;
    kb_drv.read_cb = sdl_keyboard_read;
    lv_indev_drv_register(&kb_drv);

    /* PNG 解码器（表情脸/背景素材）+ FreeType 中文字体 */
    lv_png_init();
    lv_freetype_init(4, 4, 128 * 1024);
    /* .name 不能走静态初始化（ui_res_font 是运行时解析），函数体里赋值 */
    static lv_ft_info_t ft48, ft32, ft24;
    ft48.name = ui_res_font(FONT_CN_REGULAR);
    ft32.name = ui_res_font(FONT_CN_REGULAR);
    ft24.name = ui_res_font(FONT_CN_REGULAR);
    ft48.weight = 48;  ft48.style = FT_FONT_STYLE_NORMAL;
    ft32.weight = 32;  ft32.style = FT_FONT_STYLE_NORMAL;
    ft24.weight = 24;  ft24.style = FT_FONT_STYLE_NORMAL;
    if (lv_ft_font_init(&ft48) && lv_ft_font_init(&ft32)
        && lv_ft_font_init(&ft24)) {
        ui_font_cn_48 = ft48.font;
        ui_font_cn_32 = ft32.font;
        ui_font_cn_24 = ft24.font;
    } else {
        XZ_LOGE("port", "freetype font load FAIL: %s", ui_res_font(FONT_CN_REGULAR));
        XZ_LOGE("port", "提示: XZ_RES_DIR=<assets 目录> 可显式指定资源目录");
        return -1;
    }
    return 0;
}

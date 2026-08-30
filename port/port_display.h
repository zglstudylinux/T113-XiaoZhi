/*
 * port_display.h — 显示/输入平台抽象（ui 层与 main 只依赖本头）
 *
 * port/linux: sunxifb(/dev/fb0) + evdev(/dev/input/event4) + FreeType 字体
 * port/sdl:   SDL2 窗口 480×640 + 鼠标/键盘（PC 模拟器，host 构建）
 */
#ifndef PORT_DISPLAY_H
#define PORT_DISPLAY_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化显示后端 + 输入设备 + PNG 解码器 + FreeType 中文字体。
 * 成功后 ui_font_cn_24/32/48 可用（见 ui_main.h）。
 * 返回 0 成功。 */
int port_display_init(void);

#ifdef __cplusplus
}
#endif
#endif /* PORT_DISPLAY_H */

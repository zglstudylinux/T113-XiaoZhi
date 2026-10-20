/*
 * app_events.h — 业务→UI 事件契约（纯 C99，不依赖 UI/LVGL）
 *
 * 业务层（app/）只 post 事件；ui/ui_facade.c 订阅总线并在主线程分发到
 * 具体页面。换一套 UI = 换一个 facade 实现，业务层零改动。
 *
 * 载荷语义见 controller.c 的 post 点注释（data 最多 512B，与协议文本
 * 上限一致；aux 供扩展）。
 */
#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdint.h>
#include "app_state.h"          /* xz_state_t（状态事件携带） */

typedef enum {
    APP_EVT_STATE = 1,          /* data=reason 串；aux1=目标状态 */
    APP_EVT_CHAT_USER,          /* data=用户话术（stt） */
    APP_EVT_CHAT_ASSISTANT,     /* data=助手话术（tts 句子） */
    APP_EVT_SERVER_INFO,        /* data=l1；aux=l2（连接状态/提示行） */
    APP_EVT_ACTIVATION,         /* data=6 位验证码（未绑定） */
    APP_EVT_BOUND,              /* 绑定成功：UI 退出验证码显示恢复状态行 */
    APP_EVT_EMOTION,            /* data=情绪串（happy/sad/...） */
    APP_EVT_NAVIGATE,           /* aux1=目标页（app_page_t） */
} app_evt_t;

/* APP_EVT_NAVIGATE 的目标页（ui_facade 映射到 ui_nav_go 的 create 回调） */
typedef enum {
    APP_PAGE_MAIN = 1,          /* 小智主页 */
    APP_PAGE_MENU,              /* 菜单页（wifi boot 失败等） */
} app_page_t;

#endif

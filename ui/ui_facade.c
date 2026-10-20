/*
 * ui_facade.c — 业务事件 → UI 分发（ui 层唯一订阅 app 事件的地方）
 *
 * 订阅 app_event_bus，主线程收到后映射到具体页面接口
 * （ui_main_* / ui_face / ui_nav_go）。业务层换 UI 时只需保持
 * app_events.h 契约，改这里即可。
 */
#include "ui_facade.h"
#include "app/app_events.h"
#include "app/app_event_bus.h"
#include "app/app_state.h"
#include "ui_main.h"
#include "ui_face.h"
#include "ui_menu.h"
#include "ui_navigate.h"
#include "ui_msg.h"
#include <stdio.h>

static int g_sub_handle = -1;

/* 主线程内执行：可以安全操作 LVGL */
static void facade_dispatch(app_evt_t evt, const char *data,
                            int aux1, const void *aux,
                            unsigned aux_sz, void *user)
{
    (void)aux; (void)aux_sz; (void)user;

    switch (evt) {
    case APP_EVT_STATE:
        ui_main_on_state((xz_state_t)aux1, data);
        break;
    case APP_EVT_CHAT_USER:
        ui_main_add_chat(1, data);
        break;
    case APP_EVT_CHAT_ASSISTANT:
        ui_main_add_chat(0, data);
        break;
    case APP_EVT_SERVER_INFO:
        ui_main_on_server_info(data, aux ? (const char *)aux : "");
        break;
    case APP_EVT_ACTIVATION:
        ui_main_show_activation(data);
        break;
    case APP_EVT_BOUND:
        ui_main_clear_activation();
        break;
    case APP_EVT_EMOTION:
        ui_face_set_emotion(data);
        break;
    case APP_EVT_NAVIGATE:
        if (aux1 == APP_PAGE_MAIN) {
            ui_nav_go(ui_main_create);
        } else if (aux1 == APP_PAGE_MENU) {
            ui_nav_go(ui_menu_create);
        }
        break;
    default:
        break;
    }
}

void ui_facade_init(void)
{
    ui_msg_init();
    g_sub_handle = app_event_subscribe(facade_dispatch, NULL);
}

void ui_facade_deinit(void)
{
    if (g_sub_handle >= 0) {
        app_event_unsubscribe(g_sub_handle);
        g_sub_handle = -1;
    }
}

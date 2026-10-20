/*
 * controller.c — 协议事件分发（原 main.c 的 xz_evt_cb/xz_evt_async 搬入）
 *
 * 协议线程回调 → app_event_post → ui_msg 泵 → ui_facade（主线程）。
 * 业务层全程不碰 LVGL / ui 头。
 */
#include "app_events.h"
#include "app_event_bus.h"
#include "app_state.h"
#include "app_api.h"             /* app_api_activation_code */
#include "net/xz_protocol.h"    /* XZ_EVT_* 枚举 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int controller_event_cb(int event, const char *data, void *user)
{
    (void)user;
    const char *d = data ? data : "";

    switch (event) {
    case XZ_EVT_ACTIVATION:
        /* 未绑定设备：验证码大字屏显（信息行由 ui_main_show_activation 统一写） */
        app_event_post(APP_EVT_ACTIVATION, d, 0, NULL, 0);
        break;
    case XZ_EVT_BOUND:
        /* 绑定成功（重连后 OTA 不再下发验证码）：恢复正常状态显示 */
        app_event_post(APP_EVT_BOUND, NULL, 0, NULL, 0);
        app_state_set(XZ_STATE_IDLE, "bound");
        break;
    case XZ_EVT_HELLO_OK:
        /* 握手完成：session_id 在握，信息行按真实状态刷新 */
        app_state_set(XZ_STATE_IDLE, "hello ok");
        app_api_refresh_server_info();
        break;
    case XZ_EVT_GOODBYE:
        app_event_post(APP_EVT_SERVER_INFO, "设备未绑定", 0, d, strlen(d) + 1);
        app_state_set(XZ_STATE_IDLE, "goodbye");
        break;
    case XZ_EVT_DISCONNECTED:
        /* 状态行回连接中；信息行按真实状态刷新（session 已清 → 未连接） */
        app_state_set(XZ_STATE_CONNECTING, "disconnected");
        app_api_refresh_server_info();
        break;
    case XZ_EVT_CONNECTED:
        break;
    case XZ_EVT_STT:
        app_event_post(APP_EVT_CHAT_USER, d, 0, NULL, 0);
        app_state_set(XZ_STATE_IDLE, "stt");
        break;
    case XZ_EVT_TTS_SENTENCE:
        app_event_post(APP_EVT_CHAT_ASSISTANT, d, 0, NULL, 0);
        break;
    case XZ_EVT_TTS_START:
        app_state_set(XZ_STATE_SPEAKING, "tts start");
        break;
    case XZ_EVT_TTS_STOP:
        app_state_set(XZ_STATE_IDLE, "tts stop");
        break;
    case XZ_EVT_EMOTION:
        app_event_post(APP_EVT_EMOTION, d, 0, NULL, 0);
        break;
    default:
        break;
    }
    return 0;
}

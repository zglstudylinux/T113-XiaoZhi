/*
 * controller.c — 协议事件分发（原 main.c 的 xz_evt_cb/xz_evt_async 搬入）
 *
 * 协议线程回调 → app_event_post → ui_msg 泵 → ui_facade（主线程）。
 * 业务层全程不碰 LVGL / ui 头。
 */
#include "app_events.h"
#include "app_event_bus.h"
#include "app_state.h"
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
        /* 未绑定设备：验证码大字屏显 + 信息行提示去控制台绑定 */
        app_event_post(APP_EVT_SERVER_INFO,
                       "请到 xiaozhi.me 控制台输入验证码绑定设备", 0, NULL, 0);
        app_event_post(APP_EVT_ACTIVATION, d, 0, NULL, 0);
        break;
    case XZ_EVT_HELLO_OK:
        app_event_post(APP_EVT_SERVER_INFO, "服务器已连接", 0, NULL, 0);
        app_state_set(XZ_STATE_IDLE, "hello ok");
        break;
    case XZ_EVT_GOODBYE:
        app_event_post(APP_EVT_SERVER_INFO, "设备未绑定", 0, d, strlen(d) + 1);
        app_state_set(XZ_STATE_IDLE, "goodbye");
        break;
    case XZ_EVT_DISCONNECTED:
        app_event_post(APP_EVT_SERVER_INFO, "服务器连接断开，重试中…", 0, NULL, 0);
        app_state_set(XZ_STATE_CONNECTING, "disconnected");
        break;
    case XZ_EVT_CONNECTED:
        app_event_post(APP_EVT_SERVER_INFO, "已连上，握手中…", 0, NULL, 0);
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

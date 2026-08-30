/*
 * app_state.h — 小智设备状态机
 *
 * 状态：idle → connecting → listening → speaking → (auto)listening → … idle
 *（对应 xiaozhi-esp32 docs/websocket.md 的 Device States）
 */
#ifndef APP_STATE_H
#define APP_STATE_H

typedef enum {
    XZ_STATE_IDLE = 0,      /* 待命：等用户点按说话 */
    XZ_STATE_WIFI_SETUP,    /* 配网中（M1） */
    XZ_STATE_CONNECTING,    /* WebSocket 连接/hello 握手中 */
    XZ_STATE_LISTENING,     /* 采集上传中（服务器 VAD 断句） */
    XZ_STATE_SPEAKING,      /* TTS 播放中 */
    XZ_STATE_ERROR,         /* 网络/服务器错误 */
} xz_state_t;

/* 状态查询（任何线程可调；只读原子） */
xz_state_t app_state_get(void);

/* 事件驱动入口（各模块回调里调，内部转 UI 更新） */
void app_state_init(void);
void app_state_deinit(void);
void app_state_set(xz_state_t s, const char *reason);

#endif

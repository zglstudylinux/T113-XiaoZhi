/*
 * xz_protocol.h — 小智协议层（hello/listen/tts/stt/llm + opus 帧收发）
 *
 * M2：连接 + hello 握手 + goodbye（6 位验证码）显示
 * M3/M4：opus 帧上下行（接口已留）
 */
#ifndef XZ_PROTOCOL_H
#define XZ_PROTOCOL_H

#include <stdint.h>

/* 协议事件回调（网络线程调用！UI 更新需自行 lv_async_call 转线程） */
typedef void (*xz_evt_cb_t)(int event, const char *data, void *user);

enum {
    XZ_EVT_HELLO_OK = 1,    /* 服务器 hello：data=session_id */
    XZ_EVT_GOODBYE,         /* 未绑定：data=验证码相关文本 */
    XZ_EVT_STT,             /* data=识别文本 */
    XZ_EVT_TTS_START,
    XZ_EVT_TTS_SENTENCE,    /* data=句子文本 */
    XZ_EVT_TTS_STOP,
    XZ_EVT_DISCONNECTED,    /* 连接断开 */
    XZ_EVT_CONNECTED,       /* ws 已连上（hello 前） */
    XZ_EVT_ACTIVATION,      /* OTA 验证码出现（解绑/首次）：data=6 位码 */
    XZ_EVT_BOUND,           /* OTA 验证码消失（绑定成功）：data=NULL */
    XZ_EVT_EMOTION,         /* llm 情绪：data=情绪串（happy/sad/...） */
};

/* 后台线程启动连接（读 /mnt/UDISK/xiaozhi/config.ini；无则用默认）。
 * 断线自动重连（指数退避，上限 30s）。0 成功启动 */
int  xz_protocol_start(xz_evt_cb_t cb, void *user);
void xz_protocol_stop(void);

/* 当前 session_id（未握手为 NULL） */
const char *xz_protocol_session_id(void);

/* 未绑定设备的 6 位验证码（OTA 注册下发；已绑定/未拿到为 NULL）。
 * 事件可能在 UI 离开主页时到达，进主页时查询补显 */
const char *xz_protocol_activation_code(void);

/* 协议线程是否已启动（幂等启动判断用） */
int  xz_protocol_started(void);

/* ---- M3/M4 用 ---- */
int xz_protocol_send_listen_start(void);   /* listen start(auto) */
int xz_protocol_send_listen_stop(void);
int xz_protocol_send_opus(const uint8_t *data, int len);

/* ---- 采集会话（M3：按钮触发；内部管 capture + listen + opus 上行） ---- */
/* 开始一次说话：连接就绪则 listen start + 起 capture；0 成功 */
int  xz_protocol_talk_start(void);
/* 结束本次说话（stop listen + 停 capture）；服务器 VAD 判停后也会收到
 * stt/tts 事件自动回到待命 */
int  xz_protocol_talk_stop(void);
int  xz_protocol_talking(void);

#endif

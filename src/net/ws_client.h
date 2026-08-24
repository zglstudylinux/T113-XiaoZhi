/*
 * ws_client.h — WebSocket 客户端（RFC6455 over OpenSSL BIO）
 *
 * M2：wss:// 小智服务器。阻塞式 API，由调用方线程（xz_protocol 的
 * 网络线程）驱动；收到的文本/二进制帧经回调抛给协议层。
 */
#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stdint.h>
#include <stddef.h>

/* 帧回调：binary=1 是 opus 帧，binary=0 是 JSON 文本（\0 结尾） */
typedef void (*ws_on_message_t)(void *user, int binary, const uint8_t *data, size_t len);
typedef void (*ws_on_event_t)(void *user, int event);   /* WS_EV_* */

enum {
    WS_EV_CONNECTED = 1,    /* TCP+TLS+WS 握手全部完成 */
    WS_EV_DISCONNECTED,     /* 对端关闭/出错（含主动 close 后） */
};

/*
 * 连接 wss://host:port/path（或 ws:// 明文）。
 * headers: 附加请求头（每行 "Key: Value\r\n"，可为 NULL）。
 * 返回 0 成功；失败返回 -1（错误信息打 log）。
 * 成功后进入收包循环，直到对端关闭或 ws_client_close() 被调。
 */
int ws_client_connect(const char *url, const char *headers,
                      ws_on_message_t on_msg, ws_on_event_t on_evt, void *user);

/* 主动断开（网络线程外的线程调用；只置标志，收包循环自行退出） */
void ws_client_close(void);

/* 发送文本帧（JSON）。线程安全（内部互斥）。0 成功 */
int ws_client_send_text(const char *text);

/* 发送二进制帧（opus）。线程安全。0 成功 */
int ws_client_send_binary(const uint8_t *data, size_t len);

/* 当前是否处于已连接状态 */
int ws_client_is_connected(void);

#endif

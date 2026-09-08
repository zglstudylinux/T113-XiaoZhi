/*
 * osal_net.h — TCP 网络（ws_client / OTA https 的底层传输）
 *
 * osal_net_poll 保留 poll(2) 三态语义：>0 就绪 / 0 超时 / <0 错误，
 * 调用方（ws 收包循环的让步逻辑）依赖该区分。
 */
#ifndef OSAL_NET_H
#define OSAL_NET_H

#include "osal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OSAL_SOCK_INVALID (-1)
typedef int osal_sock_t;

/* getaddrinfo + 非阻塞 connect + 轮询等待（等价 ws_client 原实现）。
 * 连上后自动设 5s 收发超时（SO_RCVTIMEO/SO_SNDTIMEO，原 OTA/ws 行为） */
int osal_net_tcp_connect(const char *host, const char *port,
                         uint32_t timeout_ms, osal_sock_t *out);
/* 全量写（EINTR 重试）；>0 成功 */
int osal_net_send(osal_sock_t fd, const void *buf, size_t len);
/* >0 数据 / 0 对端关闭 / <0 错误 */
int osal_net_recv(osal_sock_t fd, void *buf, size_t len);
int osal_net_poll(osal_sock_t fd, int want_read, int want_write, uint32_t timeout_ms);
void osal_net_close(osal_sock_t fd);

/* 取上行网卡 MAC（ wlan0 优先；失败返回 -1，buf 不变）。
 * Device-Id 必须小写格式化，与服务器 OTA 记账一致（见 xz_protocol.c）。 */
int osal_net_get_mac(uint8_t mac[6]);

#ifdef __cplusplus
}
#endif
#endif /* OSAL_NET_H */

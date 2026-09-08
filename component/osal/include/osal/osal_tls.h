/*
 * osal_tls.h — TLS 传输（ws_client / OTA https 共用；MCU 换 mbedtls 只改 posix 实现）
 */
#ifndef OSAL_TLS_H
#define OSAL_TLS_H

#include "osal_types.h"
#include "osal_net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_tls osal_tls_t;

/* osal_tls_read 的返回码：TLS 记录未收全需重试（等价原 BIO 层 WANT_READ 分支） */
#define OSAL_TLS_AGAIN   (-2)

/* 在已连接的 TCP socket 上做 TLS 握手（SNI=host；板上无 CA bundle 不校验证书） */
int  osal_tls_connect(osal_tls_t **out, osal_sock_t fd, const char *host, uint32_t timeout_ms);
/* 全量写；0 成功 */
int  osal_tls_write(osal_tls_t *t, const void *buf, size_t len);
/* >0 数据 / 0 对端关闭 / OSAL_TLS_AGAIN 需重试 / OSAL_ERR 错误 */
int  osal_tls_read(osal_tls_t *t, void *buf, size_t len);
void osal_tls_close(osal_tls_t *t);

#ifdef __cplusplus
}
#endif
#endif /* OSAL_TLS_H */

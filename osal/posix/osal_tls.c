/*
 * osal_tls.c — POSIX 实现：OpenSSL 封装
 *
 * 关键行为（ws_client M2/M3 踩坑结论）：
 * - 板上无 CA bundle：SSL_VERIFY_NONE（xiaozhi-esp32 官方同样不校验）
 * - SNI 必须按主机名设置（SSL_set_tlsext_host_name），否则 CDN 端握手失败
 * - SSL_connect 遇 socket 超时返回 WANT_READ/WANT_WRITE，需循环重试
 * - MCU 侧移植：换 mbedtls 重写本文件，接口不变
 */
#include "osal/osal_tls.h"
#include "osal/osal_time.h"
#include <string.h>
#include <stdio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

struct osal_tls {
    SSL     *ssl;
    SSL_CTX *ctx;
};

int osal_tls_connect(osal_tls_t **out, osal_sock_t fd, const char *host, uint32_t timeout_ms)
{
    SSL_library_init();
    SSL_load_error_strings();

    osal_tls_t *t = (osal_tls_t *)calloc(1, sizeof(*t));
    if (!t)
        return OSAL_ERR;

    t->ctx = SSL_CTX_new(TLS_client_method());
    if (!t->ctx)
        goto fail;
    SSL_CTX_set_verify(t->ctx, SSL_VERIFY_NONE, NULL);

    t->ssl = SSL_new(t->ctx);
    if (!t->ssl)
        goto fail;
    SSL_set_fd(t->ssl, fd);
    SSL_set_tlsext_host_name(t->ssl, host);   /* SNI */

    uint32_t waited = 0;
    while (1) {
        int r = SSL_connect(t->ssl);
        if (r == 1)
            break;
        int err = SSL_get_error(t->ssl, r);
        if ((err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) &&
            waited < timeout_ms) {
            osal_sleep_ms(100);
            waited += 100;
            continue;
        }
        printf("[tls] handshake fail err=%d(%lu)\n", err,
               (unsigned long)ERR_get_error());
        goto fail;
    }
    *out = t;
    return OSAL_OK;

fail:
    if (t->ssl)
        SSL_free(t->ssl);
    if (t->ctx)
        SSL_CTX_free(t->ctx);
    free(t);
    return OSAL_ERR;
}

int osal_tls_write(osal_tls_t *t, const void *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        int n = SSL_write(t->ssl, (const char *)buf + off, (int)(len - off));
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        int err = SSL_get_error(t->ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            continue;
        return OSAL_ERR;
    }
    return OSAL_OK;
}

int osal_tls_read(osal_tls_t *t, void *buf, size_t len)
{
    int n = SSL_read(t->ssl, buf, (int)len);
    if (n > 0)
        return n;
    if (n == 0)
        return 0;
    int err = SSL_get_error(t->ssl, n);
    if (err == SSL_ERROR_ZERO_RETURN)
        return 0;    /* 对端干净关闭 */
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
        return OSAL_ERR;   /* 阻塞 fd 上不应出现；按错误处理 */
    return OSAL_ERR;
}

void osal_tls_close(osal_tls_t *t)
{
    if (!t)
        return;
    /* 不发 close_notify（板上断网/断电场景发不出去也无所谓） */
    if (t->ssl)
        SSL_free(t->ssl);
    if (t->ctx)
        SSL_CTX_free(t->ctx);
    free(t);
}

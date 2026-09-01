/*
 * http_client.c — 极简 HTTP POST（原 xz_protocol.c https_post 平移，
 * 行为逐行等价：10s tcp / 15s tls 超时、8KB 响应上限、Content-Length
 * 收全判断、chunked 首块简化处理）
 */
#include "http_client.h"
#include "osal/osal_net.h"
#include "osal/osal_tls.h"
#include "osal/osal_log.h"
#include "app_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

char *http_post(const char *url, const char *headers[], int nhdr,
                const char *body)
{
    /* 解析 url */
    const char *p = url;
    int use_tls = 1;
    if (strncmp(url, "https://", 8) == 0) p = url + 8;
    else if (strncmp(url, "http://", 7) == 0) { use_tls = 0; p = url + 7; }
    else return NULL;

    static char host[128], path[192];
    const char *slash = strchr(p, '/');
    size_t hostlen = slash ? (size_t)(slash - p) : strlen(p);
    if (hostlen >= sizeof(host)) hostlen = sizeof(host) - 1;
    memcpy(host, p, hostlen);
    host[hostlen] = '\0';
    snprintf(path, sizeof(path), "%s", slash ? slash : "/");
    char port[8] = {0};
    char *colon = strchr(host, ':');
    if (colon) { snprintf(port, sizeof(port), "%s", colon + 1); *colon = '\0'; }
    if (!port[0]) snprintf(port, sizeof(port), "%s", use_tls ? "443" : "80");

    /* TCP + TLS（osal：非阻塞 connect/poll 超时 + 5s 收发超时都在内） */
    osal_sock_t fd = OSAL_SOCK_INVALID;
    if (osal_net_tcp_connect(host, port, 10000, &fd) != OSAL_OK) {
        XZ_LOGE("http", "tcp connect %s fail", host);
        return NULL;
    }
    osal_tls_t *tls = NULL;
    if (use_tls) {
        if (osal_tls_connect(&tls, fd, host, 15000) != OSAL_OK) {
            XZ_LOGE("http", "tls fail");
            osal_net_close(fd);
            return NULL;
        }
    }

    /* 组请求 */
    char req[3072];
    int off = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\n"
        "User-Agent: " XZ_HTTP_UA "\r\n"
        "Content-Length: %zu\r\n", path, host, strlen(body));
    for (int i = 0; i < nhdr; i++)
        off += snprintf(req + off, sizeof(req) - off, "%s\r\n", headers[i]);
    off += snprintf(req + off, sizeof(req) - off, "\r\n%s", body);
    XZ_LOGI("http", "POST %s%s body(%zu): %s", host, path, strlen(body), body);

    /* 发送 */
    int wr = tls ? osal_tls_write(tls, req, (size_t)off)
                 : osal_net_send(fd, req, (size_t)off);
    if (wr != OSAL_OK) {
        XZ_LOGE("http", "send fail");
        if (tls) osal_tls_close(tls);
        osal_net_close(fd);
        return NULL;
    }

    /* 读响应（一次性，最多 8KB） */
    static char rbuf[8192];
    size_t got = 0;
    while (got < sizeof(rbuf) - 1) {
        int n = tls ? osal_tls_read(tls, rbuf + got, sizeof(rbuf) - 1 - got)
                    : osal_net_recv(fd, rbuf + got, sizeof(rbuf) - 1 - got);
        if (n <= 0) break;
        got += (size_t)n;
        /* 头读完后按 Content-Length 判断收全 */
        rbuf[got] = '\0';
        char *cl = strstr(rbuf, "Content-Length:");
        char *hdr_end = strstr(rbuf, "\r\n\r\n");
        if (cl && hdr_end) {
            size_t want = (size_t)atoi(cl + 15);
            if (got >= (size_t)(hdr_end - rbuf) + 4 + want)
                break;
        }
    }
    rbuf[got] = '\0';
    if (tls) osal_tls_close(tls);
    osal_net_close(fd);

    if (got == 0) { XZ_LOGE("http", "empty response"); return NULL; }
    char *body_start = strstr(rbuf, "\r\n\r\n");
    if (!body_start) { XZ_LOGE("http", "bad response:\n%.200s", rbuf); return NULL; }
    body_start += 4;
    /* chunked 编码简化处理：只取第一个 chunk */
    if (strncasecmp(rbuf, "HTTP/1.1 2", 10) != 0)
        XZ_LOGW("http", "http status: %.30s", rbuf);
    char *dup = strdup(body_start);
    if (!dup) return NULL;
    /* body 若是 chunked（nginx 一般已解掉），跳过第一行 chunk 长度 */
    if (dup[0] >= '0' && dup[0] <= '9' && dup[1] == '\r' && dup[2] == '\n') {
        memmove(dup, dup + 3, strlen(dup + 3) + 1);
    }
    return dup;
}

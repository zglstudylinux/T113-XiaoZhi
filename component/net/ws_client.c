/*
 * ws_client.c — WebSocket 客户端实装（RFC6455，传输走 OSAL：osal_net + osal_tls）
 *
 * 设计要点：
 * - wss 走 osal_tls（SNI 按主机名设置）；ws:// 明文直走 osal_net。
 * - 客户端→服务器帧必须 MASK（RFC6455 §5.3），掩码用 rand()。
 * - 收包循环阻塞在 osal_tls_read/osal_net_recv，靠 poll 空闲让步，
 *   使 close 标志能被感知。
 * - ping 自动回 pong；close 帧回 close 并退出。
 * - 发送用互斥锁（JSON 线程与音频上行线程并发）。
 */
#include "ws_client.h"

#include "osal/osal_net.h"
#include "osal/osal_tls.h"
#include "osal/osal_mutex.h"
#include "osal/osal_time.h"
#include "osal/osal_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define WS_RCV_BUF_MAX   (256 * 1024)   /* 单帧上限（JSON/opus 都远小于此） */
#define TAG "ws"

/* ---------------- 状态 ---------------- */

typedef struct {
    osal_sock_t   sock;
    osal_tls_t   *tls;        /* wss 时非 NULL */
    int           connected;
    osal_mutex_t  io_lock;    /* TLS 对象非线程安全：读/写互斥（tx_lock 更名） */
    volatile int  closing;
    ws_on_message_t on_msg;
    ws_on_event_t   on_evt;
    void         *user;
    /* 收包重组：rx_buf[parse_off .. rx_len) 是待解析数据。
     * 解析出一帧后回调直接读 rx_buf+payload_off（不搬移数据），
     * 帧消费完把 parse_off 前移到下一帧，再整体压缩一次缓冲 */
    uint8_t      *rx_buf;
    size_t        rx_len;
    size_t        parse_off;
} ws_ctx_t;

static ws_ctx_t g_ctx;

int ws_client_is_connected(void) { return g_ctx.connected; }

/* ---------------- 底层读写 ----------------
 *
 * TLS 对象不是线程安全的：ws 线程读与 capture 线程写并发会破坏
 * SSL 内部状态机（实测症状：读返回 SYSCALL+errno=EAGAIN 的假"断连"，
 * M3 上行音频时必现）。所有读/写都持 io_lock。
 * 读路径先 poll socket 可读（不持锁），可读才持锁短读，
 * 避免持锁阻塞把写线程饿死。
 */

static int io_write_all(const uint8_t *data, size_t len)
{
    size_t off = 0;
    int retry = 0;
    while (off < len) {
        int n;
        int rc;
        osal_mutex_lock(g_ctx.io_lock);
        if (g_ctx.tls)
            rc = osal_tls_write(g_ctx.tls, data + off, len - off);
        else
            rc = osal_net_send(g_ctx.sock, data + off, len - off);
        osal_mutex_unlock(g_ctx.io_lock);
        if (rc == 0) {           /* 全量写完成 */
            off = len;
            break;
        }
        (void)n;
        if (retry++ < 300) {
            osal_sleep_ms(10);
            continue;
        }
        XZ_LOGE(TAG, "write fail");
        return -1;
    }
    return (off == len) ? 0 : -1;
}

/* 返回 >0 数据；0 无数据（本次没读到）；<0 对端关闭/错误 */
static int io_read(uint8_t *buf, int len)
{
    /* 先 poll socket（不持锁），500ms 内无可读即让步 */
    int pr = osal_net_poll(g_ctx.sock, 1, 0, 500);
    if (pr <= 0)
        return 0;    /* 超时：让步（保活/检查 closing） */

    osal_mutex_lock(g_ctx.io_lock);
    int n = g_ctx.tls ? osal_tls_read(g_ctx.tls, buf, (size_t)len)
                      : osal_net_recv(g_ctx.sock, buf, (size_t)len);
    osal_mutex_unlock(g_ctx.io_lock);
    if (n > 0)
        return n;
    if (n == 0)
        return -1;   /* 对端干净关闭 */
    if (n == OSAL_TLS_AGAIN)
        return 0;    /* TLS 记录未收全等，下次再试 */
    XZ_LOGE(TAG, "read fail errno=%d", errno);
    return -1;
}

/* ---------------- 帧发送 ---------------- */

static int send_frame(int opcode, const uint8_t *payload, size_t len)
{
    if (!g_ctx.connected)
        return -1;

    uint8_t hdr[14];
    int hlen = 0;
    hdr[hlen++] = (uint8_t)(0x80 | opcode);   /* FIN + opcode */

    uint8_t mask[4];
    for (int i = 0; i < 4; i++)
        mask[i] = (uint8_t)(rand() & 0xff);

    if (len < 126) {
        hdr[hlen++] = (uint8_t)(0x80 | len);
    } else if (len < 65536) {
        hdr[hlen++] = 0x80 | 126;
        hdr[hlen++] = (uint8_t)(len >> 8);
        hdr[hlen++] = (uint8_t)(len & 0xff);
    } else {
        hdr[hlen++] = 0x80 | 127;
        for (int i = 0; i < 8; i++)
            hdr[hlen++] = (uint8_t)((uint64_t)len >> (56 - i * 8));
    }
    memcpy(hdr + hlen, mask, 4);
    hlen += 4;

    uint8_t *masked = (uint8_t *)malloc(len ? len : 1);
    if (!masked)
        return -1;
    for (size_t i = 0; i < len; i++)
        masked[i] = payload[i] ^ mask[i & 3];

    /* 头+体拼一个连续缓冲，单次写出（单次 SSL_write → 单个 TLS 记录。
     * 分开写 = 两个 TLS 记录，实测服务器在 listen 会话中会因此断连） */
    size_t total = (size_t)hlen + len;
    uint8_t *pkt = (uint8_t *)malloc(total);
    if (!pkt) {
        free(masked);
        return -1;
    }
    memcpy(pkt, hdr, (size_t)hlen);
    memcpy(pkt + hlen, masked, len);

    int rc = io_write_all(pkt, total) == 0 ? 0 : -1;
    free(pkt);
    free(masked);
    return rc;
}

int ws_client_send_text(const char *text)
{
    return send_frame(0x1, (const uint8_t *)text, strlen(text));
}

int ws_client_send_binary(const uint8_t *data, size_t len)
{
    return send_frame(0x2, data, len);
}

/* ---------------- 帧接收 ---------------- */

/* 待解析数据指针/长度 */
#define RX_AVAIL   (g_ctx.rx_len - g_ctx.parse_off)
#define RX_P       (g_ctx.rx_buf + g_ctx.parse_off)

/* 解析一帧头。成功时 payload_off/payload_len 指向 rx_buf 内的位置
 * （不搬移数据），parse_off 前移到帧尾；返回 0。
 * -1 数据不足（parse_off 不动）；-2 协议错。 */
static int parse_one_frame(uint8_t *opcode_out, size_t *payload_off,
                           size_t *payload_len, int *close_code_out)
{
    size_t avail = RX_AVAIL;
    if (avail < 2)
        return -1;

    uint8_t b0 = RX_P[0], b1 = RX_P[1];
    uint8_t opcode = b0 & 0x0f;
    int    masked  = (b1 & 0x80) != 0;
    size_t len = b1 & 0x7f;
    size_t idx = 2;

    if (len == 126) {
        if (avail < idx + 2) return -1;
        len = ((size_t)RX_P[idx] << 8) | RX_P[idx + 1];
        idx += 2;
    } else if (len == 127) {
        if (avail < idx + 8) return -1;
        len = 0;
        for (int i = 0; i < 8; i++)
            len = (len << 8) | RX_P[idx + i];
        idx += 8;
    }
    if (len > WS_RCV_BUF_MAX - 16) {
        XZ_LOGE(TAG, "frame too large %zu", len);
        return -2;
    }
    if (masked) {   /* 服务器→客户端帧不应带掩码，容错跳过 */
        idx += 4;
        if (avail < idx) return -1;
    }
    if (avail < idx + len)
        return -1;   /* 帧体未收全 */

    *opcode_out = opcode;
    *payload_off = g_ctx.parse_off + idx;
    *payload_len = len;
    if (opcode == 0x8 && close_code_out && avail >= idx + 2) {
        uint8_t *p = RX_P + idx;
        *close_code_out = (p[0] << 8) | p[1];
    }
    g_ctx.parse_off += idx + len;   /* 游标前移，数据不动 */
    return 0;
}

/* 缓冲压缩：把未消费数据挪回头部（收包前调用） */
static void rx_compact(void)
{
    if (g_ctx.parse_off == 0)
        return;
    size_t rest = g_ctx.rx_len - g_ctx.parse_off;
    if (rest)
        memmove(g_ctx.rx_buf, RX_P, rest);
    g_ctx.rx_len = rest;
    g_ctx.parse_off = 0;
}

/* ---------------- 握手 ---------------- */

static int http_upgrade(const char *host, const char *port, const char *path,
                        const char *headers)
{
    char req[2048];
    int n = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "%s"
        "\r\n",
        path, host, port, headers ? headers : "");
    if (io_write_all((const uint8_t *)req, (size_t)n) != 0)
        return -1;

    char resp[2048];
    size_t got = 0;
    int timeouts = 0;
    while (got < sizeof(resp) - 1) {
        int n2 = io_read((uint8_t *)resp + got, (int)(sizeof(resp) - 1 - got));
        if (n2 < 0) {
            XZ_LOGE(TAG, "handshake read fail (got=%zu)\n%.200s", got, resp);
            return -1;
        }
        if (n2 == 0) {
            if (g_ctx.closing) return -1;
            if (++timeouts > 100) {   /* 100×1s 超时窗口 */
                XZ_LOGE(TAG, "handshake response timeout");
                return -1;
            }
            continue;
        }
        got += (size_t)n2;
        resp[got] = '\0';
        char *body = strstr(resp, "\r\n\r\n");
        if (body) {
            if (strncmp(resp, "HTTP/1.1 101", 12) != 0 &&
                strncmp(resp, "HTTP/1.0 101", 12) != 0) {
                XZ_LOGE(TAG, "upgrade rejected:\n%.400s", resp);
                return -1;
            }
            body += 4;
            size_t body_len = got - (size_t)(body - resp);
            if (body_len) {
                memcpy(g_ctx.rx_buf, body, body_len);
                g_ctx.rx_len = body_len;
                g_ctx.parse_off = 0;
            }
            return 0;
        }
    }
    XZ_LOGE(TAG, "handshake response too long");
    return -1;
}

/* ---------------- 对外 API ---------------- */

int ws_client_connect(const char *url, const char *headers,
                      ws_on_message_t on_msg, ws_on_event_t on_evt, void *user)
{
    if (url == NULL)
        return -1;

    int use_tls = 0;
    const char *p = url;
    if (strncmp(url, "wss://", 6) == 0) { use_tls = 1; p = url + 6; }
    else if (strncmp(url, "ws://", 5) == 0) { p = url + 5; }
    else { XZ_LOGE(TAG, "bad url %s", url); return -1; }

    static char host[128], port[8], path[192];
    const char *slash = strchr(p, '/');
    size_t hostlen = slash ? (size_t)(slash - p) : strlen(p);
    if (hostlen >= sizeof(host)) hostlen = sizeof(host) - 1;
    memcpy(host, p, hostlen);
    host[hostlen] = '\0';
    char *colon = strchr(host, ':');
    if (colon) {
        snprintf(port, sizeof(port), "%s", colon + 1);
        *colon = '\0';
    } else {
        snprintf(port, sizeof(port), "%d", use_tls ? 443 : 80);
    }
    snprintf(path, sizeof(path), "%s", slash ? slash : "/");

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.rx_buf = (uint8_t *)malloc(WS_RCV_BUF_MAX);
    if (!g_ctx.rx_buf)
        return -1;
    g_ctx.io_lock = osal_mutex_new();
    if (!g_ctx.io_lock) {
        free(g_ctx.rx_buf);
        g_ctx.rx_buf = NULL;
        return -1;
    }
    g_ctx.sock = OSAL_SOCK_INVALID;
    g_ctx.on_msg = on_msg;
    g_ctx.on_evt = on_evt;
    g_ctx.user   = user;
    g_ctx.closing = 0;

    /* TCP：osal 非阻塞 connect + poll 带超时（10s，原行为） */
    if (osal_net_tcp_connect(host, port, 10000, &g_ctx.sock) != OSAL_OK) {
        XZ_LOGE(TAG, "connect %s:%s fail", host, port);
        goto fail;
    }

    if (use_tls) {
        /* osal_tls：SNI + 不校验证书（板上无 CA bundle）+ 握手重试（15s 上限） */
        if (osal_tls_connect(&g_ctx.tls, g_ctx.sock, host, 15000) != OSAL_OK)
            goto fail;
        XZ_LOGI(TAG, "TLS OK");
    }

    if (http_upgrade(host, port, path, headers) != 0)
        goto fail;

    g_ctx.connected = 1;
    if (g_ctx.on_evt)
        g_ctx.on_evt(g_ctx.user, WS_EV_CONNECTED);
    XZ_LOGI(TAG, "connected %s:%s%s", host, port, path);

    /* ---- 收包循环 ---- */
    uint64_t last_ping = osal_uptime_ms64();
    while (!g_ctx.closing) {
        uint8_t opcode;
        size_t poff = 0, plen = 0;
        int close_code = 0;
        int rc = parse_one_frame(&opcode, &poff, &plen, &close_code);
        if (rc == -1) {
            /* 数据不足：压缩缓冲后从 IO 再读 */
            rx_compact();
            int n = io_read(g_ctx.rx_buf + g_ctx.rx_len,
                            (int)(WS_RCV_BUF_MAX - g_ctx.rx_len));
            if (n < 0) {
                XZ_LOGI(TAG, "peer closed");
                break;
            }
            if (n == 0) {
                /* poll 空闲返回：保活 ping（服务器 idle ~60-90s 会踢） */
                uint64_t now = osal_uptime_ms64();
                if (now - last_ping >= 20000) {
                    if (send_frame(0x9, (const uint8_t *)"keepalive", 9) == 0)
                        last_ping = now;
                }
                continue;
            }
            g_ctx.rx_len += (size_t)n;
            continue;
        }
        if (rc == -2)
            break;

        switch (opcode) {
        case 0x1:   /* text */
        case 0x2:   /* binary */
            if (g_ctx.on_msg) {
                if (opcode == 0x1)
                    g_ctx.rx_buf[poff + plen] = '\0';   /* 文本帧补 \0（缓冲有预留） */
                g_ctx.on_msg(g_ctx.user, opcode == 0x2,
                             g_ctx.rx_buf + poff, plen);
            }
            break;
        case 0x8:   /* close：回 close 后退出 */
            XZ_LOGI(TAG, "close frame code=%d", close_code);
            (void)send_frame(0x8, (const uint8_t *)"", 0);
            g_ctx.closing = 1;
            break;
        case 0x9:   /* ping → pong */
            (void)send_frame(0xA, g_ctx.rx_buf + poff, plen);
            break;
        default:    /* pong / 分片（本协议不用分片） */
            break;
        }
        /* 回调可能消费慢，定期压缩（这里每帧后顺手压一次开销可接受） */
        rx_compact();
    }

    g_ctx.connected = 0;
    if (g_ctx.on_evt)
        g_ctx.on_evt(g_ctx.user, WS_EV_DISCONNECTED);
    goto cleanup;
fail:
cleanup:
    if (g_ctx.tls) {
        osal_tls_close(g_ctx.tls);
        g_ctx.tls = NULL;
    }
    if (g_ctx.sock != OSAL_SOCK_INVALID) {
        osal_net_close(g_ctx.sock);
        g_ctx.sock = OSAL_SOCK_INVALID;
    }
    if (g_ctx.io_lock) {
        osal_mutex_free(g_ctx.io_lock);
        g_ctx.io_lock = NULL;
    }
    free(g_ctx.rx_buf);
    g_ctx.rx_buf = NULL;
    return g_ctx.connected ? 0 : -1;
}

void ws_client_close(void)
{
    g_ctx.closing = 1;
}

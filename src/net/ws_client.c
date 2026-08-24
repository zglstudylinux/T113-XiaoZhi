/*
 * ws_client.c — WebSocket 客户端实装（RFC6455 over OpenSSL BIO）
 *
 * 设计要点：
 * - wss 走 SSL_CTX + SSL_connect（SNI 按主机名设置）；
 *   ws:// 明文走 BIO_new_connect。
 * - 客户端→服务器帧必须 MASK（RFC6455 §5.3），掩码用 rand()。
 * - 收包循环阻塞在 BIO_read，靠 nbio + should_retry 超时让步，
 *   使 close 标志能被感知。
 * - ping 自动回 pong；close 帧回 close 并退出。
 * - 发送用互斥锁（JSON 线程与音频上行线程并发）。
 */
#include "ws_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#define WS_RCV_BUF_MAX   (256 * 1024)   /* 单帧上限（JSON/opus 都远小于此） */

/* ---------------- 状态 ---------------- */

typedef struct {
    BIO          *bio;
    SSL          *ssl;        /* wss 时非 NULL */
    SSL_CTX      *ssl_ctx;
    int           connected;
    pthread_mutex_t tx_lock;
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

/* ---------------- 底层读写 ---------------- */

static int bio_write_all(const uint8_t *data, size_t len)
{
    size_t off = 0;
    int retry = 0;
    while (off < len) {
        int n = BIO_write(g_ctx.bio, data + off, (int)(len - off));
        if (n > 0) {
            off += (size_t)n;
            retry = 0;
            continue;
        }
        int err = 0;
        int retryable = 0;
        if (g_ctx.ssl) {
            err = SSL_get_error(g_ctx.ssl, n);
            retryable = (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE);
        } else {
            retryable = BIO_should_retry(g_ctx.bio);
        }
        if (retryable && retry++ < 300) {
            usleep(10 * 1000);
            continue;
        }
        printf("[ws] write fail err=%d\n", err);
        return -1;
    }
    return 0;
}

/* 返回 >0 数据；0 超时重试（SO_RCVTIMEO 到期）；<0 对端关闭/错误 */
static int bio_read(uint8_t *buf, int len)
{
    int n = BIO_read(g_ctx.bio, buf, len);
    if (n > 0)
        return n;
    if (n == 0)
        return -1;   /* 对端干净关闭 */
    int ssl_err = 0;
    if (g_ctx.ssl) {
        ssl_err = SSL_get_error(g_ctx.ssl, n);
        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE)
            return 0;   /* socket 超时（EAGAIN），上层退避重试 */
        printf("[ws] read ssl_err=%d\n", ssl_err);
        return -1;
    }
    if (BIO_should_retry(g_ctx.bio))
        return 0;      /* 明文 socket 超时 */
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

    pthread_mutex_lock(&g_ctx.tx_lock);
    int rc = -1;
    if (bio_write_all(hdr, (size_t)hlen) == 0 &&
        bio_write_all(masked, len) == 0)
        rc = 0;
    pthread_mutex_unlock(&g_ctx.tx_lock);
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

/* ---------------- 帧接收 ---------------- */

/* 待解析数据指针/长度 */
#define RX_AVAIL   (g_ctx.rx_len - g_ctx.parse_off)
#define RX_P       (g_ctx.rx_buf + g_ctx.parse_off)

/* 解析一帧头。成功时 *payload_off/*payload_len 指向 rx_buf 内的位置
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
        printf("[ws] frame too large %zu\n", len);
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
    if (bio_write_all((const uint8_t *)req, (size_t)n) != 0)
        return -1;

    char resp[2048];
    size_t got = 0;
    int timeouts = 0;
    while (got < sizeof(resp) - 1) {
        int n2 = bio_read((uint8_t *)resp + got, (int)(sizeof(resp) - 1 - got));
        if (n2 < 0) {
            printf("[ws] handshake read fail (got=%zu)\n%.200s\n", got, resp);
            return -1;
        }
        if (n2 == 0) {
            if (g_ctx.closing) return -1;
            if (++timeouts > 100) {   /* 100×1s 超时窗口 */
                printf("[ws] handshake response timeout\n");
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
                printf("[ws] upgrade rejected:\n%.400s\n", resp);
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
    printf("[ws] handshake response too long\n");
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
    else { printf("[ws] bad url %s\n", url); return -1; }

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
    pthread_mutex_init(&g_ctx.tx_lock, NULL);
    g_ctx.on_msg = on_msg;
    g_ctx.on_evt = on_evt;
    g_ctx.user   = user;
    g_ctx.closing = 0;

    SSL_library_init();
    SSL_load_error_strings();

    /* TCP：原生 socket + poll 带超时连接（比 BIO nbio 轮询可靠） */
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res) {
        printf("[ws] resolve %s fail\n", host);
        goto fail;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); goto fail; }
    /* 非阻塞 connect + poll */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int cr = connect(fd, res->ai_addr, res->ai_addrlen);
    if (cr != 0 && errno != EINPROGRESS) {
        printf("[ws] connect %s:%s fail\n", host, port);
        close(fd); freeaddrinfo(res);
        goto fail;
    }
    if (cr != 0) {
        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        int pr = poll(&pfd, 1, 10000);
        int soerr = 0; socklen_t slen = sizeof(soerr);
        if (pr <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &slen) != 0 || soerr != 0) {
            printf("[ws] connect %s:%s fail (poll=%d soerr=%d)\n", host, port, pr, soerr);
            close(fd); freeaddrinfo(res);
            goto fail;
        }
    }
    freeaddrinfo(res);
    /* 转回阻塞 + SO_RCVTIMEO/SO_SNDTIMEO：收包循环超时返回 EAGAIN 检查 closing */
    fcntl(fd, F_SETFL, flags);
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* BIO 包装 socket */
    BIO *conn = BIO_new_socket(fd, BIO_CLOSE);
    if (!conn) { close(fd); goto fail; }

    if (use_tls) {
        g_ctx.ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!g_ctx.ssl_ctx) { printf("[ws] SSL_CTX_new fail\n"); BIO_free_all(conn); goto fail; }
        /* 板上无 CA bundle，服务器证书不校验（xiaozhi-esp32 默认同样不校验） */
        SSL_CTX_set_verify(g_ctx.ssl_ctx, SSL_VERIFY_NONE, NULL);
        g_ctx.ssl = SSL_new(g_ctx.ssl_ctx);
        if (!g_ctx.ssl) { BIO_free_all(conn); goto fail; }
        SSL_set_tlsext_host_name(g_ctx.ssl, host);   /* SNI */
        SSL_set_bio(g_ctx.ssl, conn, conn);
        /* SSL_connect 可能因 socket 超时返回 -1/WANT_READ，循环重试 */
        {
            int waited = 0;
            while (1) {
                int r = SSL_connect(g_ctx.ssl);
                if (r == 1) break;
                int err = SSL_get_error(g_ctx.ssl, r);
                if ((err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) &&
                    ++waited < 150) {   /* 最多 ~150×0.1s = 15s */
                    usleep(100 * 1000);
                    continue;
                }
                printf("[ws] TLS handshake fail err=%d(%lu)\n", err,
                       (unsigned long)ERR_get_error());
                goto fail_after_bio;
            }
        }
        /* 关键：IO 必须走 SSL BIO（BIO_read/write → SSL_read/write 加解密）。
         * 若拿 SSL_get_wbio（socket BIO）会绕过加密层发明文，服务器直接断连 */
        g_ctx.bio = BIO_new(BIO_f_ssl());
        if (!g_ctx.bio) goto fail_after_bio;
        BIO_set_ssl(g_ctx.bio, g_ctx.ssl, BIO_CLOSE);
        printf("[ws] TLS OK: %s %s\n",
               SSL_get_cipher_version(g_ctx.ssl), SSL_get_cipher(g_ctx.ssl));
    } else {
        g_ctx.bio = conn;
    }

    if (http_upgrade(host, port, path, headers) != 0)
        goto fail_after_bio;

    g_ctx.connected = 1;
    if (g_ctx.on_evt)
        g_ctx.on_evt(g_ctx.user, WS_EV_CONNECTED);
    printf("[ws] connected %s:%s%s\n", host, port, path);

    /* ---- 收包循环 ---- */
    time_t last_rx = time(NULL), last_ping = time(NULL);
    while (!g_ctx.closing) {
        uint8_t opcode;
        size_t poff = 0, plen = 0;
        int close_code = 0;
        int rc = parse_one_frame(&opcode, &poff, &plen, &close_code);
        if (rc == -1) {
            /* 数据不足：压缩缓冲后从 BIO 再读 */
            rx_compact();
            int n = bio_read(g_ctx.rx_buf + g_ctx.rx_len,
                             (int)(WS_RCV_BUF_MAX - g_ctx.rx_len));
            if (n < 0) {
                printf("[ws] peer closed\n");
                break;
            }
            if (n == 0) {
                /* 空闲保活：每 20s 发 ping（服务器 idle ~60-90s 会踢） */
                time_t now = time(NULL);
                if (now - last_ping >= 20) {
                    if (send_frame(0x9, (const uint8_t *)"keepalive", 9) == 0)
                        last_ping = now;
                }
                usleep(20 * 1000);   /* 无数据退避，兼查 closing */
                continue;
            }
            g_ctx.rx_len += (size_t)n;
            last_rx = time(NULL);
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
            printf("[ws] close frame code=%d\n", close_code);
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
fail_after_bio:
fail:
cleanup:
    /* SSL BIO（BIO_CLOSE）会连带 free SSL；socket BIO 自行 free。
     * 顺序：先 free BIO 链（内部释放 SSL），再兜底 free SSL/ctx */
    if (g_ctx.bio) {
        BIO_free_all(g_ctx.bio);          /* SSL BIO 带 CLOSE 会一起释放 ssl */
        g_ctx.bio = NULL;
        g_ctx.ssl = NULL;                 /* 已被上面连带释放 */
    } else if (g_ctx.ssl) {
        SSL_free(g_ctx.ssl);
        g_ctx.ssl = NULL;
    }
    if (g_ctx.ssl_ctx) SSL_CTX_free(g_ctx.ssl_ctx);
    g_ctx.bio = NULL;
    g_ctx.ssl = NULL;
    g_ctx.ssl_ctx = NULL;
    pthread_mutex_destroy(&g_ctx.tx_lock);
    free(g_ctx.rx_buf);
    g_ctx.rx_buf = NULL;
    return g_ctx.connected ? 0 : -1;
}

void ws_client_close(void)
{
    g_ctx.closing = 1;
}

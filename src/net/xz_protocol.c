/*
 * xz_protocol.c — 小智协议层实装
 *
 * M2：ws 连接（config.ini 可配 URL/token/device_id）→ 发 hello →
 *      收服务器 hello（存 session_id）/ goodbye（未绑定 → 6 位验证码）。
 * 断线重连：指数退避 1s→2s→…→30s。
 *
 * 线程模型：start() 起协议线程；线程内阻塞收包，回调直接在协议线程
 * 触发（UI 必须自己 lv_async_call）。
 */
#include "xz_protocol.h"
#include "ws_client.h"
#include "cJSON.h"
#include "audio_cap.h"
#include "opus_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netpacket/packet.h>
#include <net/if.h>
#include <openssl/ssl.h>

#define CFG_PATH      "/mnt/UDISK/xiaozhi/config.ini"
#define DEF_WS_URL    "wss://api.tenclass.net/xiaozhi/v1/"
#define DEF_TOKEN     "test"

/* ---------------- 配置 ---------------- */

typedef struct {
    char ws_url[192];
    char token[128];
    char device_id[20];
    char client_id[40];
} xz_cfg_t;

static xz_cfg_t g_cfg;

/* 简单 ini 读取：key = value（无节、去首尾空白） */
static void cfg_parse(const char *path, xz_cfg_t *c)
{
    snprintf(c->ws_url, sizeof(c->ws_url), "%s", DEF_WS_URL);
    snprintf(c->token, sizeof(c->token), "%s", DEF_TOKEN);
    snprintf(c->device_id, sizeof(c->device_id), "%s", "00:00:00:00:00:00");
    snprintf(c->client_id, sizeof(c->client_id), "%s", "5df31a2c-8b7e-4a3f-9c1d-2e6f7a8b9c0d");

    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line, *val = eq + 1;
        /* 去空白 */
        while (*key == ' ' || *key == '\t') key++;
        char *e = key + strlen(key);
        while (e > key && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
        while (*val == ' ' || *val == '\t') val++;
        e = val + strlen(val);
        while (e > val && (e[-1] == ' ' || e[-1] == '\t' ||
                           e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';
        if (strcmp(key, "ws_url") == 0)
            snprintf(c->ws_url, sizeof(c->ws_url), "%s", val);
        else if (strcmp(key, "token") == 0)
            snprintf(c->token, sizeof(c->token), "%s", val);
        else if (strcmp(key, "device_id") == 0)
            snprintf(c->device_id, sizeof(c->device_id), "%s", val);
        else if (strcmp(key, "client_id") == 0)
            snprintf(c->client_id, sizeof(c->client_id), "%s", val);
    }
    fclose(f);
}

/* 取 wlan0 MAC 做 Device-Id（失败保留默认） */
static void fill_device_id(xz_cfg_t *c)
{
    struct ifaddrs *ifap;
    if (getifaddrs(&ifap) != 0)
        return;
    for (struct ifaddrs *it = ifap; it; it = it->ifa_next) {
        if (strcmp(it->ifa_name, "wlan0") != 0 || !it->ifa_addr)
            continue;
        struct sockaddr_ll *sl = (struct sockaddr_ll *)it->ifa_addr;
        if (sl->sll_family == AF_PACKET && sl->sll_halen == 6) {
            const uint8_t *m = sl->sll_addr;
            /* 必须小写！服务器 OTA 注册按小写 MAC 记账，
             * 握手 Device-Id 大写会被当作未注册设备直接 close */
            snprintf(c->device_id, sizeof(c->device_id),
                     "%02x:%02x:%02x:%02x:%02x:%02x",
                     m[0], m[1], m[2], m[3], m[4], m[5]);
            /* Client-Id 用 MAC 派生的 UUID（服务器要求 UUID 格式） */
            snprintf(c->client_id, sizeof(c->client_id),
                     "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                     m[0], m[1], m[2], m[3], m[0], m[1], m[2], m[3],
                     m[4], m[5], m[0], m[1], m[2], m[3], m[4], m[5]);
            break;
        }
    }
    freeifaddrs(ifap);
}

/* ---------------- 协议状态 ---------------- */

static pthread_t g_thread;
static atomic_int g_run = 0;
static atomic_int g_ws_ok = 0;
static char g_session_id[80];
static xz_evt_cb_t g_cb;
static void *g_cb_user;

/* OTA 注册结果 */
static char g_ws_token[192];      /* OTA 下发的 ws token */
static char g_activation_code[8]; /* 未绑定时的 6 位验证码 */

const char *xz_protocol_session_id(void)
{
    return g_ws_ok ? g_session_id : NULL;
}

const char *xz_protocol_activation_code(void)
{
    return g_activation_code[0] ? g_activation_code : NULL;
}

/* ---------------- OTA 注册（设备激活 + 拿 ws token / 验证码） ----------------
 *
 * 官方流程（xiaozhi-esp32 / py-xiaozhi 同款）：启动时 POST 固件信息到 OTA
 * 接口完成设备注册；未绑定设备响应里多一个 activation 字段（含 6 位验证码，
 * 用户在 xiaozhi.me 控制台输入绑定）。不带这步直接连 ws 会被服务器空 close。
 */

#define OTA_URL  "https://api.tenclass.net/xiaozhi/ota/"

/* 极简 HTTPS POST（OpenSSL，证书不校验——板上无 CA bundle）。
 * 返回响应 body（malloc，调用方 free）；失败 NULL */
static char *https_post(const char *url, const char *headers[], int nhdr,
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

    /* TCP 连接（非阻塞 + poll 超时） */
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char *svc = port[0] ? port : (use_tls ? "443" : "80");
    if (getaddrinfo(host, svc, &hints, &res) != 0 || !res) {
        printf("[ota] resolve %s fail\n", host);
        return NULL;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return NULL; }
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int cr = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (cr != 0 && errno != EINPROGRESS) { close(fd); return NULL; }
    if (cr != 0) {
        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        if (poll(&pfd, 1, 10000) <= 0) { close(fd); printf("[ota] tcp timeout\n"); return NULL; }
    }
    fcntl(fd, F_SETFL, fl);
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    SSL_CTX *ssl_ctx = NULL;
    SSL *ssl = NULL;
    int rawfd = fd;
    if (use_tls) {
        ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx) { close(fd); return NULL; }
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);
        ssl = SSL_new(ssl_ctx);
        BIO *sbio = BIO_new_socket(fd, BIO_NOCLOSE);
        SSL_set_bio(ssl, sbio, sbio);
        SSL_set_tlsext_host_name(ssl, host);
        if (SSL_connect(ssl) != 1) {
            printf("[ota] tls fail\n");
            SSL_free(ssl); SSL_CTX_free(ssl_ctx); close(fd);
            return NULL;
        }
    }

    /* 组请求 */
    char req[3072];
    int off = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\n"
        "User-Agent: xiaozhi-t113/1.0.0\r\n"
        "Content-Length: %zu\r\n", path, host, strlen(body));
    for (int i = 0; i < nhdr; i++)
        off += snprintf(req + off, sizeof(req) - off, "%s\r\n", headers[i]);
    off += snprintf(req + off, sizeof(req) - off, "\r\n%s", body);
    printf("[ota] POST %s%s body(%zu): %s\n", host, path, strlen(body), body);

    /* 发送 */
    size_t sent = 0;
    while (sent < (size_t)off) {
        int n = ssl ? SSL_write(ssl, req + sent, off - sent)
                    : (int)write(fd, req + sent, (size_t)off - sent);
        if (n <= 0) {
            printf("[ota] send fail\n");
            goto ota_fail;
        }
        sent += (size_t)n;
    }

    /* 读响应（一次性，最多 8KB） */
    static char rbuf[8192];
    size_t got = 0;
    while (got < sizeof(rbuf) - 1) {
        int n = ssl ? SSL_read(ssl, rbuf + got, (int)(sizeof(rbuf) - 1 - got))
                    : (int)read(fd, rbuf + got, sizeof(rbuf) - 1 - got);
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
    if (ssl) { SSL_free(ssl); SSL_CTX_free(ssl_ctx); }
    close(fd);

    if (got == 0) { printf("[ota] empty response\n"); return NULL; }
    char *body_start = strstr(rbuf, "\r\n\r\n");
    if (!body_start) { printf("[ota] bad response:\n%.200s\n", rbuf); return NULL; }
    body_start += 4;
    /* chunked 编码简化处理：只取第一个 chunk */
    if (strncasecmp(rbuf, "HTTP/1.1 2", 10) != 0)
        printf("[ota] http status: %.30s\n", rbuf);
    char *dup = strdup(body_start);
    if (!dup) return NULL;
    /* body 若是 chunked（nginx 一般已解掉），跳过第一行 chunk 长度 */
    if (dup[0] >= '0' && dup[0] <= '9' && dup[1] == '\r' && dup[2] == '\n') {
        memmove(dup, dup + 3, strlen(dup + 3) + 1);
    }
    return dup;

ota_fail:
    if (ssl) { SSL_free(ssl); SSL_CTX_free(ssl_ctx); }
    close(rawfd);
    return NULL;
}

/* OTA 注册：成功返回 0（g_ws_token/g_activation_code 已填） */
static int ota_register(void)
{
    char body[512];
    /* board.type 必须是官方已注册的板型名（面包板兼容方案），
     * version=2 对应官方固件当前协议版本 */
    snprintf(body, sizeof(body),
        "{\"application\":{\"version\":2,\"chip_model_name\":\"esp32s3\","
        "\"board\":{\"type\":\"bread-compact-wifi\",\"mac_address\":\"%s\","
        "\"uuid\":\"%s\"}}}",
        g_cfg.device_id, g_cfg.client_id);

    const char *hdrs[2];
    char dev_hdr[64], cli_hdr[96];
    snprintf(dev_hdr, sizeof(dev_hdr), "Device-Id: %s", g_cfg.device_id);
    snprintf(cli_hdr, sizeof(cli_hdr), "Client-Id: %s", g_cfg.client_id);
    hdrs[0] = dev_hdr;
    hdrs[1] = cli_hdr;

    char *resp = https_post(OTA_URL, hdrs, 2, body);
    if (!resp) {
        /* 退回配置里的 ws_url + token */
        snprintf(g_ws_token, sizeof(g_ws_token), "%s", g_cfg.token);
        return -1;
    }
    printf("[ota] resp: %s\n", resp);

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) {
        snprintf(g_ws_token, sizeof(g_ws_token), "%s", g_cfg.token);
        return -1;
    }
    /* websocket.url + token */
    cJSON *ws = cJSON_GetObjectItem(root, "websocket");
    const char *url = ws ? cJSON_GetStringValue(cJSON_GetObjectItem(ws, "url")) : NULL;
    const char *tok = ws ? cJSON_GetStringValue(cJSON_GetObjectItem(ws, "token")) : NULL;
    if (url) {
        snprintf(g_cfg.ws_url, sizeof(g_cfg.ws_url), "%s", url);
        snprintf(g_ws_token, sizeof(g_ws_token), "%s", tok ? tok : g_cfg.token);
    } else {
        snprintf(g_ws_token, sizeof(g_ws_token), "%s", g_cfg.token);
    }
    /* activation.code —— 未绑定设备的 6 位验证码 */
    cJSON *act = cJSON_GetObjectItem(root, "activation");
    const char *code = act ? cJSON_GetStringValue(cJSON_GetObjectItem(act, "code")) : NULL;
    if (code) {
        snprintf(g_activation_code, sizeof(g_activation_code), "%s", code);
        printf("[ota] ★ 激活验证码: %s\n", g_activation_code);
        if (g_cb) {
            char msg[160];
            snprintf(msg, sizeof(msg), "%s", code);
            g_cb(XZ_EVT_ACTIVATION, msg, g_cb_user);
        }
    }
    cJSON_Delete(root);
    return 0;
}

/* ---------------- 消息构造 ---------------- */

static int send_hello(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON *feat = cJSON_AddObjectToObject(root, "features");
    cJSON_AddBoolToObject(feat, "mcp", 0);
    cJSON_AddBoolToObject(feat, "aec", 0);
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON *ap = cJSON_AddObjectToObject(root, "audio_params");
    cJSON_AddStringToObject(ap, "format", "opus");
    cJSON_AddNumberToObject(ap, "sample_rate", 16000);
    cJSON_AddNumberToObject(ap, "channels", 1);
    cJSON_AddNumberToObject(ap, "frame_duration", 60);
    char *s = cJSON_PrintUnformatted(root);
    int rc = ws_client_send_text(s);
    printf("[xz] >> hello: %s\n", s);
    cJSON_free(s);
    cJSON_Delete(root);
    return rc;
}

int xz_protocol_send_listen_start(void)
{
    cJSON *root = cJSON_CreateObject();
    if (g_session_id[0])
        cJSON_AddStringToObject(root, "session_id", g_session_id);
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "start");
    cJSON_AddStringToObject(root, "mode", "auto");
    char *s = cJSON_PrintUnformatted(root);
    int rc = ws_client_send_text(s);
    printf("[xz] >> listen start: %s\n", s);
    cJSON_free(s);
    cJSON_Delete(root);
    return rc;
}

int xz_protocol_send_listen_stop(void)
{
    cJSON *root = cJSON_CreateObject();
    if (g_session_id[0])
        cJSON_AddStringToObject(root, "session_id", g_session_id);
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "stop");
    char *s = cJSON_PrintUnformatted(root);
    int rc = ws_client_send_text(s);
    printf("[xz] >> listen stop: %s\n", s);
    cJSON_free(s);
    cJSON_Delete(root);
    return rc;
}

int xz_protocol_send_opus(const uint8_t *data, int len)
{
    return ws_client_send_binary(data, (size_t)len);
}

/* ---------------- 采集会话（M3）----------------
 *
 * 按钮 → talk_start：listen start(auto) → 起 capture →
 * capture 线程回调里 opus 编码 → ws binary 上行。
 * 服务器 VAD 判停 → stt 文本 → （M4 前）UI 显示后回待命；
 * 用户也可再点按钮 talk_stop 提前结束（服务器收到 stop 后走识别）。
 */

static atomic_int g_talking = 0;
static int g_opus_frames = 0;         /* 本会话已发帧数（日志用） */
static struct timespec g_opus_t0;

/* capture 线程回调：编码 + 上行 */
static void cap_on_frame(const int16_t *pcm, int samples, void *user)
{
    (void)user;
    if (!atomic_load(&g_talking))
        return;
    uint8_t opus_buf[512];
    int n = opus_enc_encode(pcm, samples, opus_buf, sizeof(opus_buf));
    if (n > 0) {
        int rc = ws_client_send_binary(opus_buf, (size_t)n);
        g_opus_frames++;
        /* 低频心跳日志（首帧 + 每 100 帧 ≈ 6s），诊断供帧节奏 */
        if (g_opus_frames == 1 || g_opus_frames % 100 == 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long ms = (now.tv_sec - g_opus_t0.tv_sec) * 1000
                    + (now.tv_nsec - g_opus_t0.tv_nsec) / 1000000;
            printf("[xz] opus#%d %dB rc=%d t+%ldms\n",
                   g_opus_frames, n, rc, ms);
        }
    }
}

int xz_protocol_talk_start(void)
{
    if (!atomic_load(&g_ws_ok)) {
        printf("[xz] talk_start: 未连接\n");
        return -1;
    }
    if (atomic_load(&g_talking))
        return 0;

    if (opus_enc_init() != 0)
        return -1;
    g_opus_frames = 0;
    clock_gettime(CLOCK_MONOTONIC, &g_opus_t0);
    if (xz_protocol_send_listen_start() != 0) {
        opus_enc_deinit();
        return -1;
    }
    if (audio_cap_start(cap_on_frame, NULL) != 0) {
        xz_protocol_send_listen_stop();
        opus_enc_deinit();
        return -1;
    }
    atomic_store(&g_talking, 1);
    return 0;
}

int xz_protocol_talk_stop(void)
{
    if (!atomic_load(&g_talking))
        return 0;
    atomic_store(&g_talking, 0);
    audio_cap_stop();           /* 先停采集，最后一帧落地 */
    xz_protocol_send_listen_stop();
    opus_enc_deinit();
    return 0;
}

int xz_protocol_talking(void)
{
    return atomic_load(&g_talking);
}

/* ---------------- 下行分发 ---------------- */

static void handle_text(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        printf("[xz] JSON parse fail: %.120s\n", json);
        return;
    }
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(root, "type"));
    if (!type) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "hello") == 0) {
        const char *sid = cJSON_GetStringValue(cJSON_GetObjectItem(root, "session_id"));
        if (sid) {
            snprintf(g_session_id, sizeof(g_session_id), "%s", sid);
            atomic_store(&g_ws_ok, 1);
            printf("[xz] << hello session_id=%s\n", g_session_id);
            if (g_cb) g_cb(XZ_EVT_HELLO_OK, g_session_id, g_cb_user);
        }
    } else if (strcmp(type, "goodbye") == 0) {
        /* 未绑定设备：服务器发 goodbye，TTS 音频里播 6 位验证码 */
        printf("[xz] << goodbye: %s\n", json);
        atomic_store(&g_ws_ok, 0);
        if (g_cb) g_cb(XZ_EVT_GOODBYE, json, g_cb_user);
    } else if (strcmp(type, "stt") == 0) {
        const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(root, "text"));
        /* VAD 判停 → 服务器已收到完整语音，结束本地采集 */
        if (atomic_load(&g_talking))
            xz_protocol_talk_stop();
        if (g_cb && text) g_cb(XZ_EVT_STT, text, g_cb_user);
    } else if (strcmp(type, "tts") == 0) {
        const char *state = cJSON_GetStringValue(cJSON_GetObjectItem(root, "state"));
        if (state && strcmp(state, "start") == 0) {
            if (g_cb) g_cb(XZ_EVT_TTS_START, NULL, g_cb_user);
        } else if (state && strcmp(state, "stop") == 0) {
            if (g_cb) g_cb(XZ_EVT_TTS_STOP, NULL, g_cb_user);
        } else if (state && strcmp(state, "sentence_start") == 0) {
            const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(root, "text"));
            if (g_cb) g_cb(XZ_EVT_TTS_SENTENCE, text ? text : "", g_cb_user);
        }
    } else {
        /* llm/system/mcp 等：M2 阶段只打 log */
        printf("[xz] << %s: %.200s\n", type, json);
    }
    cJSON_Delete(root);
}

/* goodbye 的 TTS 音频里的验证码也走文本：官方服务器会把验证码放在
 * goodbye 前的一条 tts sentence_start 或 alert 消息里（实测以 log 为准）。
 * 二进制帧 M2 忽略（M4 接播放）。 */

static void on_ws_message(void *user, int binary, const uint8_t *data, size_t len)
{
    (void)user;
    if (binary) {
        printf("[xz] << binary %zu bytes (M4 再接播放)\n", len);
        return;
    }
    printf("[xz] << text: %s\n", (const char *)data);
    handle_text((const char *)data);
}

static void on_ws_event(void *user, int event)
{
    (void)user;
    if (event == WS_EV_CONNECTED) {
        if (g_cb) g_cb(XZ_EVT_CONNECTED, NULL, g_cb_user);
        send_hello();
    } else if (event == WS_EV_DISCONNECTED) {
        atomic_store(&g_ws_ok, 0);
        g_session_id[0] = '\0';
        /* 断线时若正在说话：停采集（不发 stop——连接已断） */
        if (atomic_load(&g_talking)) {
            atomic_store(&g_talking, 0);
            audio_cap_stop();
            opus_enc_deinit();
        }
        if (g_cb) g_cb(XZ_EVT_DISCONNECTED, NULL, g_cb_user);
    }
}

/* ---------------- 协议线程 ---------------- */

static void *xz_thread(void *arg)
{
    (void)arg;
    int backoff = 1;   /* 秒 */

    /* 先 OTA 注册设备（拿 ws token + 未绑定时 6 位验证码），
     * 失败也继续——用 config.ini 里的 token 连 */
    ota_register();

    while (atomic_load(&g_run)) {
        char headers[512];
        snprintf(headers, sizeof(headers),
                 "Authorization: Bearer %s\r\n"
                 "Protocol-Version: 1\r\n"
                 "Device-Id: %s\r\n"
                 "Client-Id: %s\r\n"
                 "User-Agent: xiaozhi-t113/1.0.0\r\n",
                 g_ws_token[0] ? g_ws_token : g_cfg.token,
                 g_cfg.device_id, g_cfg.client_id);
        printf("[xz] connecting %s (device %s, token %.12s...)\n",
               g_cfg.ws_url, g_cfg.device_id,
               g_ws_token[0] ? g_ws_token : g_cfg.token);

        /* connect 内部收包循环直到断开 */
        ws_client_connect(g_cfg.ws_url, headers, on_ws_message, on_ws_event, NULL);

        atomic_store(&g_ws_ok, 0);
        g_session_id[0] = '\0';
        printf("[xz] disconnected, retry in %ds\n", backoff);
        for (int i = 0; i < backoff * 10 && atomic_load(&g_run); i++)
            usleep(100 * 1000);
        backoff = backoff >= 30 ? 30 : backoff * 2;
    }
    return NULL;
}

int xz_protocol_start(xz_evt_cb_t cb, void *user)
{
    g_cb = cb;
    g_cb_user = user;
    g_session_id[0] = '\0';
    g_ws_token[0] = '\0';
    g_activation_code[0] = '\0';

    cfg_parse(CFG_PATH, &g_cfg);
    fill_device_id(&g_cfg);
    printf("[xz] cfg: url=%s device=%s client=%s\n",
           g_cfg.ws_url, g_cfg.device_id, g_cfg.client_id);

    atomic_store(&g_run, 1);
    if (pthread_create(&g_thread, NULL, xz_thread, NULL) != 0)
        return -1;
    return 0;
}

void xz_protocol_stop(void)
{
    atomic_store(&g_run, 0);
    ws_client_close();
    pthread_join(g_thread, NULL);
}

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
#include "http_client.h"
#include "app_config.h"
#include "cJSON.h"
#include "audio_cap.h"
#include "audio_play.h"
#include "opus_codec.h"
#include "osal/osal_net.h"
#include "osal/osal_tls.h"
#include "osal/osal_thread.h"
#include "osal/osal_atomic.h"
#include "osal/osal_time.h"
#include "osal/osal_file.h"
#include "osal/osal_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---------------- 配置 ---------------- */

typedef struct {
    char ws_url[192];
    char token[128];
    char device_id[20];
    char client_id[40];
} xz_cfg_t;

static xz_cfg_t g_cfg;

/* ini 字段回调（app_config_load_ini 逐行调） */
static void cfg_kv(void *user, const char *key, const char *val)
{
    xz_cfg_t *c = (xz_cfg_t *)user;
    if (strcmp(key, "ws_url") == 0)
        snprintf(c->ws_url, sizeof(c->ws_url), "%s", val);
    else if (strcmp(key, "token") == 0)
        snprintf(c->token, sizeof(c->token), "%s", val);
    else if (strcmp(key, "device_id") == 0)
        snprintf(c->device_id, sizeof(c->device_id), "%s", val);
    else if (strcmp(key, "client_id") == 0)
        snprintf(c->client_id, sizeof(c->client_id), "%s", val);
}

/* 取 wlan0 MAC 做 Device-Id（失败保留默认） */
static void fill_device_id(xz_cfg_t *c)
{
    uint8_t m[6];
    if (osal_net_get_mac(m) != OSAL_OK)
        return;
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
}

/* ---------------- 协议状态 ---------------- */

static osal_thread_t g_thread;
static osal_atomic_t g_run = OSAL_ATOMIC_INIT_ZERO;
static osal_atomic_t g_ws_ok = OSAL_ATOMIC_INIT_ZERO;
static char g_session_id[80];
static xz_evt_cb_t g_cb;
static void *g_cb_user;

/* OTA 注册结果 */
static char g_ws_token[192];      /* OTA 下发的 ws token */
static char g_activation_code[8]; /* 未绑定时的 6 位验证码 */

/* 服务器下行音频参数（hello 里带，实测 24k mono opus 60ms） */
static int g_srv_audio_rate = 24000;

int xz_protocol_started(void)
{
    return osal_atomic_get(&g_run);
}

const char *xz_protocol_session_id(void)
{
    return osal_atomic_get(&g_ws_ok) ? g_session_id : NULL;
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
 * HTTP 传输在 http_client.c（P5 从本文件抽出）。
 */

/* OTA 注册：成功返回 0（g_ws_token/g_activation_code 已填）。
 * check_change=1 时（断线重连）对比旧验证码只发变化事件：
 *   码出现（解绑/首次）→ XZ_EVT_ACTIVATION；码消失（绑定成功）→ XZ_EVT_BOUND */
static int ota_register(int check_change)
{
    char prev_code[8];
    snprintf(prev_code, sizeof(prev_code), "%s", g_activation_code);

    char body[512];
    /* board.type 必须是官方已注册的板型名（宏在 app_config.h），
     * version=2 对应官方固件当前协议版本 */
    snprintf(body, sizeof(body),
        "{\"application\":{\"version\":2,\"chip_model_name\":\"" XZ_REPORT_CHIP "\","
        "\"board\":{\"type\":\"" XZ_REPORT_BOARD "\",\"mac_address\":\"%s\","
        "\"uuid\":\"%s\"}}}",
        g_cfg.device_id, g_cfg.client_id);

    const char *hdrs[2];
    char dev_hdr[64], cli_hdr[96];
    snprintf(dev_hdr, sizeof(dev_hdr), "Device-Id: %s", g_cfg.device_id);
    snprintf(cli_hdr, sizeof(cli_hdr), "Client-Id: %s", g_cfg.client_id);
    hdrs[0] = dev_hdr;
    hdrs[1] = cli_hdr;

    char *resp = http_post(XZ_OTA_URL, hdrs, 2, body);
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
    } else {
        g_activation_code[0] = '\0';   /* 绑定成功：响应不再带 activation */
    }
    /* 绑定状态变化 → 通知 UI（启动首轮 check_change=0：无码不发） */
    if (check_change) {
        int appeared = g_activation_code[0] && !prev_code[0];
        int cleared = !g_activation_code[0] && prev_code[0];
        if ((appeared || cleared) && g_cb)
            g_cb(appeared ? XZ_EVT_ACTIVATION : XZ_EVT_BOUND,
                 g_activation_code, g_cb_user);
    } else if (g_activation_code[0] && g_cb) {
        g_cb(XZ_EVT_ACTIVATION, g_activation_code, g_cb_user);
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

static osal_atomic_t g_talking = OSAL_ATOMIC_INIT_ZERO;
static int g_opus_frames = 0;         /* 本会话已发帧数（日志用） */
static uint64_t g_opus_t0;            /* osal_uptime_ms64 起点 */

/* capture 线程回调：编码 + 上行 */
static void cap_on_frame(const int16_t *pcm, int samples, void *user)
{
    (void)user;
    if (!osal_atomic_get(&g_talking))
        return;
    uint8_t opus_buf[512];
    int n = opus_enc_encode(pcm, samples, opus_buf, sizeof(opus_buf));
    if (n > 0) {
        int rc = ws_client_send_binary(opus_buf, (size_t)n);
        g_opus_frames++;
        /* 低频心跳日志（首帧 + 每 100 帧 ≈ 6s），诊断供帧节奏 */
        if (g_opus_frames == 1 || g_opus_frames % 100 == 0) {
            XZ_LOGI("xz", "opus#%d %dB rc=%d t+%llums",
                    g_opus_frames, n, rc,
                    (unsigned long long)(osal_uptime_ms64() - g_opus_t0));
        }
    }
}

int xz_protocol_talk_start(void)
{
    if (!osal_atomic_get(&g_ws_ok)) {
        XZ_LOGE("xz", "talk_start: 未连接");
        return -1;
    }
    if (osal_atomic_get(&g_talking))
        return 0;

    if (opus_enc_init() != 0)
        return -1;
    g_opus_frames = 0;
    g_opus_t0 = osal_uptime_ms64();
    if (xz_protocol_send_listen_start() != 0) {
        opus_enc_deinit();
        return -1;
    }
    if (audio_cap_start(cap_on_frame, NULL) != 0) {
        xz_protocol_send_listen_stop();
        opus_enc_deinit();
        return -1;
    }
    osal_atomic_set(&g_talking, 1);
    return 0;
}

int xz_protocol_talk_stop(void)
{
    if (!osal_atomic_get(&g_talking))
        return 0;
    osal_atomic_set(&g_talking, 0);
    audio_cap_stop();           /* 先停采集，最后一帧落地 */
    xz_protocol_send_listen_stop();
    opus_enc_deinit();
    return 0;
}

int xz_protocol_talking(void)
{
    return osal_atomic_get(&g_talking);
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
            osal_atomic_set(&g_ws_ok, 1);
            /* 下行音频参数（M4：解码采样率） */
            cJSON *ap = cJSON_GetObjectItem(root, "audio_params");
            int sr = cJSON_GetObjectItem(ap, "sample_rate")
                     ? cJSON_GetObjectItem(ap, "sample_rate")->valueint : 0;
            if (sr >= 8000 && sr <= 48000) {
                if (sr != g_srv_audio_rate)
                    printf("[xz] srv audio %dHz（旧 %d）\n", sr, g_srv_audio_rate);
                g_srv_audio_rate = sr;
            }
            printf("[xz] << hello session_id=%s audio=%dHz\n",
                   g_session_id, g_srv_audio_rate);
            if (g_cb) g_cb(XZ_EVT_HELLO_OK, g_session_id, g_cb_user);
        }
    } else if (strcmp(type, "goodbye") == 0) {
        /* 未绑定设备：服务器发 goodbye，TTS 音频里播 6 位验证码 */
        printf("[xz] << goodbye: %s\n", json);
        osal_atomic_set(&g_ws_ok, 0);
        if (g_cb) g_cb(XZ_EVT_GOODBYE, json, g_cb_user);
    } else if (strcmp(type, "stt") == 0) {
        const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(root, "text"));
        /* VAD 判停 → 服务器已收到完整语音，结束本地采集 */
        if (osal_atomic_get(&g_talking))
            xz_protocol_talk_stop();
        if (g_cb && text) g_cb(XZ_EVT_STT, text, g_cb_user);
    } else if (strcmp(type, "tts") == 0) {
        const char *state = cJSON_GetStringValue(cJSON_GetObjectItem(root, "state"));
        if (state && strcmp(state, "start") == 0) {
            /* M4：起播放链（解码+重采样+ALSA），后续二进制帧喂进来。
             * 先清残留队列（上一会话被打断/断线留下的旧帧） */
            audio_play_flush();
            if (audio_play_start(g_srv_audio_rate) != 0)
                printf("[xz] audio_play_start fail（TTS 静音）\n");
            if (g_cb) g_cb(XZ_EVT_TTS_START, NULL, g_cb_user);
        } else if (state && strcmp(state, "stop") == 0) {
            /* 等缓冲播完再回 idle（网络线程阻塞 ≤5s，可接受：
             * 播放期间服务器只可能继续推本会话音频/新消息） */
            audio_play_drain();
            /* 关设备：DAPM 断 DAC/HPOUT 电，消除空闲底噪（滋滋声）。
             * 驱动带 ramp 防 pop 时序；下次 tts start 重新打开 */
            audio_play_stop();
            if (g_cb) g_cb(XZ_EVT_TTS_STOP, NULL, g_cb_user);
        } else if (state && strcmp(state, "sentence_start") == 0) {
            const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(root, "text"));
            if (g_cb) g_cb(XZ_EVT_TTS_SENTENCE, text ? text : "", g_cb_user);
        }
    } else if (strcmp(type, "llm") == 0) {
        /* AI 情绪（happy/laughing/sad/...）→ 表情脸 */
        const char *emo = cJSON_GetStringValue(cJSON_GetObjectItem(root, "text"));
        if (g_cb && emo && emo[0]) g_cb(XZ_EVT_EMOTION, emo, g_cb_user);
    } else {
        /* system/mcp 等：只打 log */
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
        /* M4：服务器下行 opus 帧 → 播放队列 */
        static int bin_n = 0;
        bin_n++;
        if (bin_n == 1 || bin_n % 100 == 0)
            printf("[xz] << opus #%d %zuB\n", bin_n, len);
        audio_play_feed(data, (int)len);
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
        osal_atomic_set(&g_ws_ok, 0);
        g_session_id[0] = '\0';
        /* 断线时若正在说话：停采集（不发 stop——连接已断） */
        if (osal_atomic_get(&g_talking)) {
            osal_atomic_set(&g_talking, 0);
            audio_cap_stop();
            opus_enc_deinit();
        }
        /* 正在播放则丢弃余音（连接已断，避免 drain 卡 5s） */
        audio_play_stop();
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
    ota_register(0);

    while (osal_atomic_get(&g_run)) {
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

        osal_atomic_set(&g_ws_ok, 0);
        g_session_id[0] = '\0';
        printf("[xz] disconnected, retry in %ds\n", backoff);
        for (int i = 0; i < backoff * 10 && osal_atomic_get(&g_run); i++)
            osal_sleep_ms(100);
        backoff = backoff >= 30 ? 30 : backoff * 2;
        /* 重连前重查 OTA：用户可能刚在控制台绑定/解绑设备，
         * 绑定状态变化 → ACTIVATION/BOUND 事件刷 UI（服务器 60~90s
         * 踢一次空闲连接，重连是天然的绑定状态刷新点） */
        ota_register(1);
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

    app_config_load_ini(app_config_path("config.ini"), cfg_kv, &g_cfg);
    fill_device_id(&g_cfg);
    printf("[xz] cfg: url=%s device=%s client=%s\n",
           g_cfg.ws_url, g_cfg.device_id, g_cfg.client_id);

    osal_atomic_set(&g_run, 1);
    if (osal_thread_create(&g_thread, xz_thread, NULL) != OSAL_OK)
        return -1;
    return 0;
}

void xz_protocol_stop(void)
{
    osal_atomic_set(&g_run, 0);
    ws_client_close();
    osal_thread_join(g_thread);
}

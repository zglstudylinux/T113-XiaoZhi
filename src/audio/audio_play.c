/*
 * audio_play.c — ALSA 播放线程（M4：opus 解码 → 重采样 → default 设备）
 *
 * 数据路径：网络线程 feed(opus 帧) → 待解码队列（互斥锁 + 条件变量）
 *   → 播放线程取帧 → opus 解码（服务器采样率，实测 24k mono 60ms）
 *   → speexdsp 重采样到 48k → 写 ALSA（snd_pcm_writei 阻塞节流）。
 *
 * 设备："default"（板上 asound.conf 的标准播放路径：plug→softvol→dmix
 * → hw:audiocodec 48k 双声道）。**不要直开 hw**：dmix 共享 + softvol 音量
 * 都在这条链上；裸 hw 16k mono 实测有可闻噪声（对照 bt-speaker 走
 * default 播放干净）。单声道数据复制到双声道（L=R）。
 * 背压：解码队列上限 ~30 帧（1.8s），满了丢最老帧保实时。
 * tts stop → drain() 等环形缓冲（ALSA 自身缓冲）播完再回 idle。
 */
#include "audio_play.h"
#include "opus_codec.h"
#include "resample.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include <time.h>

#define PLAY_DEVICE     "default"
#define PLAY_RATE       48000
#define PLAY_CH         2
#define Q_MAX_FRAMES    30          /* ~1.8s 的 60ms opus 帧 */
#define Q_FRAME_BYTES   512         /* 单帧 opus 最大（32kbps×60ms=240B，留裕量） */
#define DEC_SAMP_MAX    (24000 * 60 / 1000)      /* 24k 60ms = 1440 */
#define RS_OUT_MAX      (DEC_SAMP_MAX * 48 / 24 + 160)  /* 48k 输出上界 */

typedef struct {
    int len;
    uint8_t data[Q_FRAME_BYTES];
} opus_qitem_t;

/* ---- 待解码队列（网络线程生产 / 播放线程消费） ---- */
static opus_qitem_t  g_queue[Q_MAX_FRAMES];
static int           g_q_head, g_q_tail, g_q_count;
static pthread_mutex_t g_qmtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_qcond = PTHREAD_COND_INITIALIZER;

/* ---- 播放线程 ---- */
static snd_pcm_t    *g_pcm;
static pthread_t     g_thr;
static atomic_int    g_run = 0;
static int           g_src_rate = 24000;
static int           g_underruns;

/* ---- mixer：播放音质（DAC HPF 滤直流/低频，否则人声里混滋滋声） ---- */
static void play_set_ctl_enum(const char *name, unsigned item)
{
    snd_ctl_t *ctl = NULL;
    if (snd_ctl_open(&ctl, "hw:audiocodec", 0) != 0)
        return;
    snd_ctl_elem_id_t *id;
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, name);
    snd_ctl_elem_value_t *val;
    snd_ctl_elem_value_alloca(&val);
    snd_ctl_elem_value_set_id(val, id);
    snd_ctl_elem_value_set_enumerated(val, 0, item);
    if (snd_ctl_elem_write(ctl, val) < 0)
        printf("[play] set %s fail\n", name);
    snd_ctl_close(ctl);
}

static void q_push(const uint8_t *opus, int len)
{
    pthread_mutex_lock(&g_qmtx);
    if (g_q_count >= Q_MAX_FRAMES) {
        /* 队满丢最老（实时语音宁丢勿积压） */
        g_q_head = (g_q_head + 1) % Q_MAX_FRAMES;
        g_q_count--;
    }
    opus_qitem_t *it = &g_queue[g_q_tail];
    if (len > Q_FRAME_BYTES)
        len = Q_FRAME_BYTES;
    it->len = len;
    memcpy(it->data, opus, (size_t)len);
    g_q_tail = (g_q_tail + 1) % Q_MAX_FRAMES;
    g_q_count++;
    pthread_cond_signal(&g_qcond);
    pthread_mutex_unlock(&g_qmtx);
}

static opus_qitem_t *q_pop(void)
{
    /* 调用前已持 g_qmtx；返回 NULL 表示空 */
    if (g_q_count == 0)
        return NULL;
    opus_qitem_t *it = &g_queue[g_q_head];
    g_q_head = (g_q_head + 1) % Q_MAX_FRAMES;
    g_q_count--;
    return it;
}

static void *play_thread(void *arg)
{
    (void)arg;
    static int16_t pcm_dec[DEC_SAMP_MAX];
    static int16_t pcm_rs[RS_OUT_MAX];
    static int16_t pcm_st[RS_OUT_MAX * 2];   /* mono → stereo 交织 */
    int frames_out = 0;
#ifdef DIAG_DUMP_PCM
    FILE *df = fopen("/mnt/UDISK/xiaozhi/diag_play.pcm", "wb");
    if (df) printf("[play][diag] dump → /mnt/UDISK/xiaozhi/diag_play.pcm\n");
#endif

    while (atomic_load(&g_run)) {
        opus_qitem_t *it = NULL;
        pthread_mutex_lock(&g_qmtx);
        while (g_q_count == 0 && atomic_load(&g_run))
            pthread_cond_wait(&g_qcond, &g_qmtx);
        it = q_pop();
        pthread_mutex_unlock(&g_qmtx);
        if (!it)
            continue;

        int n = opus_dec_decode(it->data, it->len, pcm_dec, DEC_SAMP_MAX);
        if (n <= 0) {
            printf("[play] decode fail n=%d len=%d\n", n, it->len);
            continue;
        }
        int m = resample_process(pcm_dec, n, pcm_rs, RS_OUT_MAX);
        if (m <= 0)
            continue;
        /* mono → stereo 交织（L=R） */
        for (int i = 0; i < m; i++) {
            pcm_st[i * 2] = pcm_rs[i];
            pcm_st[i * 2 + 1] = pcm_rs[i];
        }
#ifdef DIAG_DUMP_PCM
        if (df) fwrite(pcm_rs, 2, m, df);
#endif

        /* 写 ALSA（欠载自动恢复） */
        const int16_t *p = pcm_st;
        int left = m;                      /* 帧数（交织后每帧 2 样本） */
        while (left > 0 && atomic_load(&g_run)) {
            long w = snd_pcm_writei(g_pcm, p, (snd_pcm_uframes_t)left);
            if (w == -EPIPE) {
                /* underrun：数据没跟上播放 → 周期性爆音（滋息声） */
                g_underruns++;
                printf("[play] *** underrun #%d\n", g_underruns);
                snd_pcm_prepare(g_pcm);
                continue;
            }
            if (w < 0) {
                printf("[play] writei err %ld(%s)\n", w, snd_strerror((int)w));
                snd_pcm_recover(g_pcm, (int)w, 1);
                continue;
            }
            p += w * PLAY_CH;
            left -= (int)w;
        }
        frames_out++;
        if (frames_out == 1 || frames_out % 100 == 0)
            printf("[play] frame#%d dec=%d rs=%d\n", frames_out, n, m);
    }
#ifdef DIAG_DUMP_PCM
    if (df) fclose(df);
#endif
    return NULL;
}

int audio_play_start(int src_rate)
{
    if (atomic_load(&g_run)) {
        /* 已在跑：采样率变化才重配（tts 会话间通常同率） */
        if (g_src_rate == src_rate)
            return 0;
        audio_play_stop();
    }

    if (opus_dec_init(src_rate) != 0)
        return -1;
    if (resample_init(src_rate, PLAY_RATE) != 0) {
        opus_dec_deinit();
        return -1;
    }

    int err = snd_pcm_open(&g_pcm, PLAY_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        printf("[play] open %s fail: %s\n", PLAY_DEVICE, snd_strerror(err));
        opus_dec_deinit();
        resample_deinit();
        return -1;
    }
    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(g_pcm, hw);
    snd_pcm_hw_params_set_access(g_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(g_pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(g_pcm, hw, PLAY_CH);

    unsigned rate = PLAY_RATE;
    int dir = 0;
    snd_pcm_hw_params_set_rate_near(g_pcm, hw, &rate, &dir);
    snd_pcm_uframes_t period = 960;                     /* 20ms @48k */
    snd_pcm_hw_params_set_period_size_near(g_pcm, hw, &period, &dir);
    snd_pcm_uframes_t buf = 960 * 8;                    /* 160ms 缓冲 */
    snd_pcm_hw_params_set_buffer_size_near(g_pcm, hw, &buf);

    err = snd_pcm_hw_params(g_pcm, hw);
    if (err < 0) {
        printf("[play] hw_params fail: %s\n", snd_strerror(err));
        snd_pcm_close(g_pcm); g_pcm = NULL;
        opus_dec_deinit(); resample_deinit();
        return -1;
    }
    snd_pcm_sw_params_t *sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(g_pcm, sw);
    snd_pcm_sw_params_set_start_threshold(g_pcm, sw, period);
    snd_pcm_sw_params_set_avail_min(g_pcm, sw, period);
    snd_pcm_sw_params(g_pcm, sw);
    snd_pcm_prepare(g_pcm);

    g_src_rate = src_rate;
    g_underruns = 0;
    play_set_ctl_enum("DAC HPF Mode", 1);   /* On：滤 DC/低频嗡声 */
    pthread_mutex_lock(&g_qmtx);
    g_q_head = g_q_tail = g_q_count = 0;
    pthread_mutex_unlock(&g_qmtx);

    atomic_store(&g_run, 1);
    if (pthread_create(&g_thr, NULL, play_thread, NULL) != 0) {
        atomic_store(&g_run, 0);
        snd_pcm_close(g_pcm); g_pcm = NULL;
        opus_dec_deinit(); resample_deinit();
        return -1;
    }
    printf("[play] started %d->%d stereo(default/dmix), period=%lu\n",
           src_rate, PLAY_RATE, (unsigned long)period);
    return 0;
}

int audio_play_feed(const uint8_t *opus, int len)
{
    if (!atomic_load(&g_run))
        return -1;
    if (len <= 0 || len > Q_FRAME_BYTES)
        return -1;
    q_push(opus, len);
    return 0;
}

void audio_play_drain(void)
{
    if (!atomic_load(&g_run))
        return;
    /* 等解码队列清空。注意：不直接调 snd_pcm_drain——ALSA handle
     * 不允许与播放线程的 writei 并发（同 M3 SSL 并发坑），
     * 队列清空后硬件缓冲最多剩 ~240ms，等它自然播完即可 */
    for (int i = 0; i < 100; i++) {          /* 最多 5s */
        pthread_mutex_lock(&g_qmtx);
        int cnt = g_q_count;
        pthread_mutex_unlock(&g_qmtx);
        if (cnt == 0)
            break;
        usleep(50 * 1000);
    }
    usleep(300 * 1000);   /* 最后一帧写入 + 硬件缓冲播完的尾巴 */
}

void audio_play_flush(void)
{
    /* 只清解码队列，不碰 PCM（播放线程会欠载自动 prepare 静音）：
     * snd_pcm_drop 若与播放线程 writei 并发会破坏 ALSA 状态 */
    pthread_mutex_lock(&g_qmtx);
    g_q_head = g_q_tail = g_q_count = 0;
    pthread_mutex_unlock(&g_qmtx);
}

void audio_play_stop(void)
{
    if (!atomic_load(&g_run))
        return;
    atomic_store(&g_run, 0);
    pthread_mutex_lock(&g_qmtx);
    pthread_cond_broadcast(&g_qcond);
    pthread_mutex_unlock(&g_qmtx);
    pthread_join(g_thr, NULL);
    if (g_pcm) {
        snd_pcm_drop(g_pcm);
        snd_pcm_close(g_pcm);
        g_pcm = NULL;
    }
    opus_dec_deinit();
    resample_deinit();
    printf("[play] stopped\n");
}

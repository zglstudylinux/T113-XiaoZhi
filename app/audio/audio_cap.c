/*
 * audio_cap.c — ALSA capture 线程（16k S16_LE mono，60ms 周期）
 *
 * M0 实测结论（docs/plan.md M3 节）：
 * - 采集设备必须 "default"：板上 asound.conf 定义 pcm.!default 的 capture
 *   走 CaptureMic（plug + ttable.0.2 把硬件 ch2=ADC3 映射到用户 ch0，
 *   hooks 自动开 MIC3 Switch）→ CaptureDsnoop。直接 hw:audiocodec,0 -c 1
 *   只会采到悬空的 ADC1（底噪）。
 * - mixer 断电回 dts 默认（HPF Off / Gain 19）：启动时自动设
 *   ADC HPF0/1=On（滤直流，否则 DC≈550）、ADC3 Gain=31（麦满增益）。
 *   用 snd_ctl 开控件设备走 ALSA API（不依赖外部 amixer）。
 *
 * 帧对齐：period = 60ms（320 样本/周期 ×3 = 960 样本/帧），攒满一帧
 * 回调一次，上层直接 opus_encode。
 */
#include "audio_cap.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osal/osal_thread.h"
#include "osal/osal_atomic.h"
#include "osal/osal_log.h"

#define CAP_RATE      16000
#define CAP_CH        1
#define FRAME_MS      60
#define FRAME_SAMP    (CAP_RATE * FRAME_MS / 1000)      /* 960 */
#define PERIOD_SAMP   (FRAME_SAMP / 3)                  /* 320 = 20ms */

static snd_pcm_t   *g_pcm;
static osal_thread_t g_thr;
static osal_atomic_t g_run = OSAL_ATOMIC_INIT_ZERO;
static audio_cap_cb_t g_cb;
static void        *g_cb_user;

int audio_cap_running(void) { return osal_atomic_get(&g_run); }

/* ---- mixer：恢复采集必需的控件（进程内 ALSA API，无需 amixer） ---- */
static void cap_set_ctl_enum(snd_ctl_t *ctl, const char *name, unsigned item)
{
    snd_ctl_elem_id_t *id;
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, name);
    snd_ctl_elem_value_t *val;
    snd_ctl_elem_value_alloca(&val);
    snd_ctl_elem_value_set_id(val, id);
    snd_ctl_elem_value_set_enumerated(val, 0, item);
    if (snd_ctl_elem_write(ctl, val) < 0)
        printf("[cap] set %s fail\n", name);
}

static void cap_set_ctl_int(snd_ctl_t *ctl, const char *name, long v)
{
    snd_ctl_elem_id_t *id;
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, name);
    snd_ctl_elem_value_t *val;
    snd_ctl_elem_value_alloca(&val);
    snd_ctl_elem_value_set_id(val, id);
    snd_ctl_elem_value_set_integer(val, 0, v);
    if (snd_ctl_elem_write(ctl, val) < 0)
        printf("[cap] set %s fail\n", name);
}

static void cap_mixer_setup(void)
{
    snd_ctl_t *ctl = NULL;
    if (snd_ctl_open(&ctl, "hw:audiocodec", 0) != 0) {
        printf("[cap] mixer open fail（用 dts 默认，可能有直流）\n");
        return;
    }
    cap_set_ctl_enum(ctl, "ADC HPF0 Mode", 1);   /* Item#1 = On */
    cap_set_ctl_enum(ctl, "ADC HPF1 Mode", 1);
    cap_set_ctl_int(ctl, "ADC3 Gain", 31);
    snd_ctl_close(ctl);
    printf("[cap] mixer: HPF0/1=On ADC3Gain=31\n");
}

static void *cap_thread(void *arg)
{
    (void)arg;
    static int16_t frame[FRAME_SAMP];
    int have = 0;

    while (osal_atomic_get(&g_run)) {
        int n = snd_pcm_readi(g_pcm, frame + have, PERIOD_SAMP);
        if (n == -EPIPE) {                    /* overrun */
            snd_pcm_prepare(g_pcm);
            continue;
        }
        if (n < 0) {
            printf("[cap] readi err %d(%s)\n", n, snd_strerror(n));
            snd_pcm_recover(g_pcm, n, 1);
            continue;
        }
        if (n == 0)
            continue;
        have += n;
        if (have >= FRAME_SAMP) {
            if (g_cb)
                g_cb(frame, FRAME_SAMP, g_cb_user);
            have = 0;
        }
    }
    return NULL;
}

int audio_cap_start(audio_cap_cb_t cb, void *user)
{
    if (osal_atomic_get(&g_run))
        return 0;    /* 已在跑 */

    cap_mixer_setup();

    int err = snd_pcm_open(&g_pcm, "default", SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        printf("[cap] open default fail: %s\n", snd_strerror(err));
        return -1;
    }
    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(g_pcm, hw);
    snd_pcm_hw_params_set_access(g_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(g_pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(g_pcm, hw, CAP_CH);

    unsigned rate = CAP_RATE;
    int dir = 0;
    snd_pcm_hw_params_set_rate_near(g_pcm, hw, &rate, &dir);
    snd_pcm_uframes_t period = PERIOD_SAMP;
    snd_pcm_hw_params_set_period_size_near(g_pcm, hw, &period, &dir);
    snd_pcm_uframes_t buf = FRAME_SAMP * 4;
    snd_pcm_hw_params_set_buffer_size_near(g_pcm, hw, &buf);

    err = snd_pcm_hw_params(g_pcm, hw);
    if (err < 0) {
        printf("[cap] hw_params fail: %s\n", snd_strerror(err));
        snd_pcm_close(g_pcm);
        g_pcm = NULL;
        return -1;
    }
    /* 软参数：start 后 overrun 自动恢复 */
    snd_pcm_sw_params_t *sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(g_pcm, sw);
    snd_pcm_sw_params_set_start_threshold(g_pcm, sw, PERIOD_SAMP);
    snd_pcm_sw_params_set_avail_min(g_pcm, sw, PERIOD_SAMP);
    snd_pcm_sw_params(g_pcm, sw);

    if (snd_pcm_prepare(g_pcm) < 0 || snd_pcm_start(g_pcm) < 0) {
        /* capture start 可能返回 EPIPE（无数据即启动失败），readi 会触发实际启动 */
        printf("[cap] start（延迟到首次 readi）\n");
        snd_pcm_prepare(g_pcm);
    }

    g_cb = cb;
    g_cb_user = user;
    osal_atomic_set(&g_run, 1);
    if (osal_thread_create(&g_thr, cap_thread, NULL) != OSAL_OK) {
        osal_atomic_set(&g_run, 0);
        snd_pcm_close(g_pcm);
        g_pcm = NULL;
        return -1;
    }
    printf("[cap] started 16k mono period=%lu frames\n", (unsigned long)period);
    return 0;
}

void audio_cap_stop(void)
{
    if (!osal_atomic_get(&g_run))
        return;
    osal_atomic_set(&g_run, 0);
    osal_thread_join(g_thr);
    if (g_pcm) {
        snd_pcm_drop(g_pcm);
        snd_pcm_close(g_pcm);
        g_pcm = NULL;
    }
    printf("[cap] stopped\n");
}

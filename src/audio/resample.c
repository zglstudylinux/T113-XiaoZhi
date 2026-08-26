/*
 * resample.c — speexdsp 重采样（下行 24k→16k，M4）
 *
 * 服务器 TTS opus 按其 audio_params.sample_rate（实测 24000）解码，
 * 播放设备 hw:audiocodec 固定 16k → 每帧 speex_resampler_process_int。
 * 同率直通（in==out 时 init 也走 resampler，拷贝语义，安全）。
 * 播放线程独享，无并发问题。
 */
#include "resample.h"
#include <speex/speex_resampler.h>
#include <stdio.h>

static SpeexResamplerState *g_rs;
static int g_in_rate, g_out_rate;

int resample_init(int in_rate, int out_rate)
{
    if (g_rs && g_in_rate == in_rate && g_out_rate == out_rate)
        return 0;
    resample_deinit();
    int err = 0;
    /* 质量 4 = SPEEX_RESAMPLER_QUALITY_DEFAULT，语音够用且 CPU 低 */
    g_rs = speex_resampler_init(1, in_rate, out_rate,
                                SPEEX_RESAMPLER_QUALITY_DEFAULT, &err);
    if (!g_rs || err != RESAMPLER_ERR_SUCCESS) {
        printf("[rs] init %d->%d fail err=%d\n", in_rate, out_rate, err);
        g_rs = NULL;
        return -1;
    }
    g_in_rate = in_rate;
    g_out_rate = out_rate;
    return 0;
}

void resample_deinit(void)
{
    if (g_rs)
        speex_resampler_destroy(g_rs);
    g_rs = NULL;
}

/* 返回写入 out 的样本数；<0 失败。in_len 单位：样本 */
int resample_process(const int16_t *in, int in_len, int16_t *out, int out_max)
{
    if (!g_rs)
        return -1;
    spx_uint32_t in_sz = (spx_uint32_t)in_len;
    spx_uint32_t out_sz = (spx_uint32_t)out_max;
    int rc = speex_resampler_process_int(g_rs, 0,
                                         in, &in_sz, out, &out_sz);
    if (rc != RESAMPLER_ERR_SUCCESS) {
        printf("[rs] process fail rc=%d\n", rc);
        return -1;
    }
    return (int)out_sz;
}

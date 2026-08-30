/*
 * opus_codec.c — libopus 封装（上行编码 16k / 下行解码服务器采样率）
 *
 * M3：encoder（VOIP 应用、32kbps、complexity 5），capture 线程独享。
 * M4：decoder（服务器 hello 下行 audio_params，实测 24k mono 60ms），
 *     播放线程独享（在 audio_play 内调用），无并发问题。
 */
#include "opus_codec.h"
#include <opus/opus.h>
#include <stdio.h>

#define OPUS_SAMPLE_RATE  16000
#define OPUS_CHANNELS     1
#define OPUS_FRAME_MS     60
#define OPUS_FRAME_SAMP   (OPUS_SAMPLE_RATE * OPUS_FRAME_MS / 1000)  /* 960 */

static OpusEncoder *g_enc;

/* ---- 下行 decoder（M4）---- */

static OpusDecoder *g_dec;
static int g_dec_rate = 0;      /* 创建时的采样率（opus 解码采样率可任选） */

int opus_dec_init(int sample_rate)
{
    int err = 0;
    if (g_dec && g_dec_rate == sample_rate)
        return 0;               /* 同采样率复用 */
    opus_dec_deinit();
    /* 服务器 24k opus 用 24k 解码最忠实（FEC/带内采样率信息一致）；
     * 重采样交给 speexdsp */
    g_dec = opus_decoder_create(sample_rate, OPUS_CHANNELS, &err);
    if (g_dec == NULL || err != OPUS_OK) {
        printf("[opus] decoder create fail err=%d\n", err);
        g_dec = NULL;
        return -1;
    }
    g_dec_rate = sample_rate;
    return 0;
}

void opus_dec_deinit(void)
{
    if (g_dec)
        opus_decoder_destroy(g_dec);
    g_dec = NULL;
    g_dec_rate = 0;
}

int opus_dec_frame_samples(int sample_rate)
{
    return sample_rate * OPUS_FRAME_MS / 1000;   /* 24k → 1440 */
}

/* 返回解码样本数（>0）；<0 失败 */
int opus_dec_decode(const uint8_t *data, int len, int16_t *pcm, int pcm_max)
{
    if (g_dec == NULL)
        return -1;
    return opus_decode(g_dec, data, len, pcm, pcm_max, 0);
}

int opus_enc_init(void)
{
    int err = 0;
    g_enc = opus_encoder_create(OPUS_SAMPLE_RATE, OPUS_CHANNELS,
                                OPUS_APPLICATION_VOIP, &err);
    if (g_enc == NULL || err != OPUS_OK) {
        printf("[opus] encoder create fail err=%d\n", err);
        g_enc = NULL;
        return -1;
    }
    opus_encoder_ctl(g_enc, OPUS_SET_BITRATE(32000));
    opus_encoder_ctl(g_enc, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(g_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    return 0;
}

void opus_enc_deinit(void)
{
    if (g_enc)
        opus_encoder_destroy(g_enc);
    g_enc = NULL;
}

int opus_enc_frame_samples(void)
{
    return OPUS_FRAME_SAMP;
}

/* 返回编码字节数（>0）；<0 失败 */
int opus_enc_encode(const int16_t *pcm, int frames, uint8_t *out, int out_max)
{
    if (g_enc == NULL)
        return -1;
    return opus_encode(g_enc, pcm, frames, out, out_max);
}

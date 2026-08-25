/*
 * opus_codec.c — libopus 封装（上行编码 16k mono 60ms 帧）
 *
 * M3：encoder（VOIP 应用、32kbps、complexity 5）。
 * M4 再加 decoder（服务器下行 24k opus → PCM + 重采样）。
 * encoder 只在 capture 线程使用，无并发问题。
 */
#include "opus_codec.h"
#include <opus/opus.h>
#include <stdio.h>

#define OPUS_SAMPLE_RATE  16000
#define OPUS_CHANNELS     1
#define OPUS_FRAME_MS     60
#define OPUS_FRAME_SAMP   (OPUS_SAMPLE_RATE * OPUS_FRAME_MS / 1000)  /* 960 */

static OpusEncoder *g_enc;

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

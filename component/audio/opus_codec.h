/*
 * opus_codec.h — libopus 封装
 *
 * M3：上行 encoder（16k mono 60ms 帧 → opus 字节流）
 * M4：下行 decoder（服务器 audio_params 采样率，实测 24k）
 */
#ifndef OPUS_CODEC_H
#define OPUS_CODEC_H

#include <stdint.h>

int  opus_enc_init(void);
void opus_enc_deinit(void);
int  opus_enc_frame_samples(void);   /* 960（16k×60ms） */
int  opus_enc_encode(const int16_t *pcm, int frames,
                     uint8_t *out, int out_max);

/* 下行解码（播放线程内使用） */
int  opus_dec_init(int sample_rate);
void opus_dec_deinit(void);
int  opus_dec_frame_samples(int sample_rate);  /* 24k → 1440 */
int  opus_dec_decode(const uint8_t *data, int len, int16_t *pcm, int pcm_max);

#endif

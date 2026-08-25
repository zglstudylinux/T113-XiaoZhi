/*
 * opus_codec.h — libopus 封装
 *
 * M3：上行 encoder（16k mono 60ms 帧 → opus 字节流）
 * M4：下行 decoder（预留）
 */
#ifndef OPUS_CODEC_H
#define OPUS_CODEC_H

#include <stdint.h>

int  opus_enc_init(void);
void opus_enc_deinit(void);
int  opus_enc_frame_samples(void);   /* 960（16k×60ms） */
int  opus_enc_encode(const int16_t *pcm, int frames,
                     uint8_t *out, int out_max);

#endif

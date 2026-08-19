/*
 * opus_codec.h — libopus 封装（60ms 帧 16k mono，M3 实装）
 */
#ifndef OPUS_CODEC_H
#define OPUS_CODEC_H

int  opus_codec_init(void);
void opus_codec_deinit(void);

#endif

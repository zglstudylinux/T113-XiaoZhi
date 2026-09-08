/*
 * resample.h — speexdsp 重采样（下行 24k→16k，M4）
 */
#ifndef RESAMPLE_H
#define RESAMPLE_H

#include <stdint.h>

int  resample_init(int in_rate, int out_rate);
void resample_deinit(void);
/* 返回输出样本数；<0 失败 */
int  resample_process(const int16_t *in, int in_len, int16_t *out, int out_max);

#endif

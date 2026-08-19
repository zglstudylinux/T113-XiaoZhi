/*
 * resample.h — speexdsp 重采样（下行 24k→16k 等，M4 实装）
 */
#ifndef RESAMPLE_H
#define RESAMPLE_H

int  resample_init(int in_rate, int out_rate);
void resample_deinit(void);

#endif

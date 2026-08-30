/*
 * audio_cap.h — ALSA 采集（16k S16_LE mono，60ms 帧对齐）
 *
 * M0 结论：必须用 "default" 设备（asound.conf 的 CaptureMic ttable 把
 * 硬件 ch2=ADC3 映射到用户 ch0）；mixer 设置断电回 dts 默认，
 * start 时自动恢复（HPF On + ADC3 Gain 31）。
 */
#ifndef AUDIO_CAP_H
#define AUDIO_CAP_H

#include <stdint.h>

/* 每采满一帧（960 样本 = 60ms）回调一次（capture 线程内！） */
typedef void (*audio_cap_cb_t)(const int16_t *pcm, int samples, void *user);

int  audio_cap_start(audio_cap_cb_t cb, void *user);
void audio_cap_stop(void);
int  audio_cap_running(void);

#endif

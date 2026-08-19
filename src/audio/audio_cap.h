/*
 * audio_cap.h — ALSA 采集（16k S16_LE mono，60ms 帧，M3 实装）
 */
#ifndef AUDIO_CAP_H
#define AUDIO_CAP_H

int  audio_cap_start(void);
void audio_cap_stop(void);

#endif

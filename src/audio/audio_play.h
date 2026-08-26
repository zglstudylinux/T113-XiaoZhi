/*
 * audio_play.h — ALSA 播放（opus 解码 → 重采样 → hw:audiocodec，M4）
 *
 * 网络线程把服务器下行 opus 帧喂进 audio_play_feed()，
 * 内部播放线程解码（24k）→ 重采样（→16k）→ 环形缓冲 → ALSA 写出。
 */
#ifndef AUDIO_PLAY_H
#define AUDIO_PLAY_H

#include <stdint.h>

/* 按服务器 audio_params 初始化（tts start 前调；重复调用安全） */
int  audio_play_start(int src_rate);
void audio_play_stop(void);

/* 喂一帧服务器 opus（网络线程调用；内部拷贝入解码队列）。
 * 0 成功 / -1 未启动 / -2 队满（丢帧） */
int  audio_play_feed(const uint8_t *opus, int len);

/* tts stop 后等待环形缓冲播完（阻塞，网络线程调） */
void audio_play_drain(void);

/* 丢弃未播数据（打断/断线时） */
void audio_play_flush(void);

#endif

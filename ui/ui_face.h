/*
 * ui_face.h — 小智表情脸组件（M5 产品化 UI）
 *
 * 中央 300×300 机器人脸：随设备状态切状态动画（listening/thinking/
 * speaking 多帧轮播），AI 情绪（llm 事件）在 idle 态切情绪脸。
 * 素材 /mnt/UDISK/xiaozhi/images/face_*.png（gen_assets.py 生成）。
 */
#ifndef UI_FACE_H
#define UI_FACE_H

#include "lvgl/lvgl.h"
#include "app_state.h"

/* 在 scr 上建脸（居中）。返回后可立即 set_state/set_emotion */
void ui_face_create(lv_obj_t *scr);

/* 设备状态 → 状态脸（connecting=thinking、listening/speaking=对应动画、
 * error=sad、idle=保持当前情绪脸） */
void ui_face_set_state(xz_state_t s);

/* AI 情绪脸（idle 态才应用；listening/speaking 中状态脸优先） */
void ui_face_set_emotion(const char *emotion);

/* 离开主界面时清理（停轮播 timer，ui_main 的 leave 回调里调用） */
void ui_face_leave(void);

#endif

/*
 * ui_face.c — 小智表情脸组件
 *
 * lv_animimg 只支持 RAM 驻留 lv_img_dsc_t 数组，文件路径轮播用
 * lv_img + lv_timer 换 src 实现（素材 PNG 由 gen_assets.py 生成，
 * LV_IMG_CACHE_DEF_SIZE 缓存当前帧，帧间切换无重解码卡顿）。
 */
#include "ui_face.h"
#include "ui_main.h"
#include "ui_navigate.h"
#include "ui_res.h"
#include <stdio.h>
#include <string.h>

/* 帧序列（静态名字数组，切换时整体替换；路径经 ui_res_img 解析） */
static const char *g_frames[8];
static int          g_frame_cnt;
static int          g_frame_idx;

static lv_obj_t  *g_img;
static lv_timer_t *g_timer;

/* 当前语义状态：idle 态下情绪脸不被状态轮询覆盖 */
static xz_state_t  g_state = XZ_STATE_IDLE;
static char        g_emotion[24];
static char        g_cur_name[32];

/* ---- 帧表 ---- */
static void set_frames(const char *const *names, int cnt)
{
    g_frame_cnt = cnt;
    g_frame_idx = 0;
    for (int i = 0; i < cnt; i++)
        g_frames[i] = names[i];
}

static const char *const F_IDLE[]      = { "idle" };
static const char *const F_CONNECT[]   = { "thinking_1", "thinking_2", "thinking_3" };
static const char *const F_LISTEN[]    = { "listening_1", "listening_2", "listening_3" };
static const char *const F_SPEAK[]     = { "speaking_1", "speaking_2", "speaking_3" };
static const char *const F_SAD[]       = { "sad" };
static const char *const F_THINK[]     = { "thinking_1", "thinking_2", "thinking_3" };

/* 情绪串 → 帧名（xiaozhi-esp32 情绪集合的子集） */
static const char *emotion_face(const char *e)
{
    if (!e || !e[0])                 return "happy";
    if (!strcmp(e, "happy"))         return "happy";
    if (!strcmp(e, "laughing"))      return "laughing";
    if (!strcmp(e, "funny"))         return "laughing";
    if (!strcmp(e, "sad"))           return "sad";
    if (!strcmp(e, "crying"))        return "crying";
    if (!strcmp(e, "angry"))         return "angry";
    if (!strcmp(e, "surprised"))     return "surprised";
    if (!strcmp(e, "loving"))        return "loving";
    if (!strcmp(e, "sleepy"))        return "sleepy";
    return "happy";                  /* 未知情绪容错 */
}

static void face_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (g_frame_cnt <= 0 || g_img == NULL)
        return;
    const char *name = g_frames[g_frame_idx];
    g_frame_idx = (g_frame_idx + 1) % g_frame_cnt;

    /* 名字没变不重设（单帧脸避免无谓重绘） */
    if (strcmp(name, g_cur_name) == 0)
        return;
    snprintf(g_cur_name, sizeof(g_cur_name), "%s", name);
    char fname[64];
    snprintf(fname, sizeof(fname), "face_%s.png", name);
    lv_img_set_src(g_img, ui_res_img(fname));
}

/* 组合当前应显示的帧序列并重启轮播 */
static void apply_face(void)
{
    const char *const *f = F_IDLE;
    int cnt = 1, period = 500;

    switch (g_state) {
    case XZ_STATE_CONNECTING: f = F_CONNECT; cnt = 3; period = 400; break;
    case XZ_STATE_LISTENING:  f = F_LISTEN;  cnt = 3; period = 180; break;
    case XZ_STATE_SPEAKING:   f = F_SPEAK;   cnt = 3; period = 160; break;
    case XZ_STATE_ERROR:      f = F_SAD;     cnt = 1; period = 500; break;
    default:                  /* idle：情绪脸 */
        if (g_emotion[0]) {
            g_frames[0] = emotion_face(g_emotion);
            f = g_frames;
        }
        break;
    }
    set_frames(f, cnt);
    g_cur_name[0] = '\0';            /* 强制下个 tick 立即换图 */
    if (g_timer)
        lv_timer_set_period(g_timer, period);
    face_timer_cb(NULL);
}

/* 离开清理（ui_main 的 leave 回调调用）：停轮播定时器 + 指针置空。
 * 不停的话 timer 仍按周期跑，lv_img_set_src 到已删对象 → 崩溃 */
void ui_face_leave(void)
{
    if (g_timer) {
        lv_timer_del(g_timer);
        g_timer = NULL;
    }
    g_img = NULL;
    g_frame_cnt = 0;
}

void ui_face_create(lv_obj_t *scr)
{
    g_img = lv_img_create(scr);
    lv_obj_set_size(g_img, 300, 300);
    lv_obj_align(g_img, LV_ALIGN_TOP_MID, 0, 66);

    g_timer = lv_timer_create(face_timer_cb, 300, NULL);
    apply_face();
}

void ui_face_set_state(xz_state_t s)
{
    if (g_img == NULL)
        return;
    g_state = s;
    if (s == XZ_STATE_IDLE)
        g_emotion[0] = '\0';         /* 回 idle 清情绪（普通待命脸） */
    apply_face();
}

void ui_face_set_emotion(const char *emotion)
{
    if (g_img == NULL || emotion == NULL || !emotion[0])
        return;
    snprintf(g_emotion, sizeof(g_emotion), "%s", emotion);
    if (g_state == XZ_STATE_IDLE)    /* 仅 idle 立即应用；对话态由状态脸覆盖 */
        apply_face();
}

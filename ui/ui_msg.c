/*
 * ui_msg.c — 线程→UI 消息队列实现
 */
#include "ui_msg.h"
#include "lvgl/lvgl.h"
#include "osal/osal_mutex.h"
#include <stdlib.h>
#include <string.h>

#define Q_MAX 32

typedef struct {
    ui_msg_cb_t cb;
    void       *user;
} msg_t;

static msg_t  g_q[Q_MAX];
static int    g_head, g_tail, g_count;
/* g_mtx 在 ui_msg_init 显式初始化（osal 动态分配） */
static osal_mutex_t g_mtx;
static lv_timer_t *g_pump;
static int g_inited;

/* 主循环定时器：把队列里的消息依次执行（再入队防护：执行中可能再 post） */
static void pump_cb(lv_timer_t *t)
{
    (void)t;
    for (;;) {
        osal_mutex_lock(g_mtx);
        if (g_count == 0) {
            osal_mutex_unlock(g_mtx);
            return;
        }
        msg_t m = g_q[g_head];
        g_head = (g_head + 1) % Q_MAX;
        g_count--;
        osal_mutex_unlock(g_mtx);
        m.cb(m.user);
    }
}

int ui_msg_post(ui_msg_cb_t cb, void *user)
{
    if (!g_inited)
        return -1;
    osal_mutex_lock(g_mtx);
    if (g_count >= Q_MAX) {
        /* 队满丢最旧（UI 消息取最新状态即可） */
        g_head = (g_head + 1) % Q_MAX;
        g_count--;
    }
    g_q[g_tail].cb   = cb;
    g_q[g_tail].user = user;
    g_tail = (g_tail + 1) % Q_MAX;
    g_count++;
    osal_mutex_unlock(g_mtx);
    return 0;
}

void ui_msg_init(void)
{
    g_head = g_tail = g_count = 0;
    g_mtx = osal_mutex_new();
    g_inited = 1;
    g_pump = lv_timer_create(pump_cb, 20, NULL);
}

void ui_msg_deinit(void)
{
    if (g_pump) {
        lv_timer_del(g_pump);
        g_pump = NULL;
    }
    g_inited = 0;
}

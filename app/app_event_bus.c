/*
 * app_event_bus.c — 业务→UI 事件总线实现
 *
 * 单订阅者（ui_facade）+ ui_msg 泵转发。事件从任意线程 post，
 * ui_msg 泵在主线程取出后调 facade handler（可安全操作 LVGL）。
 */
#include "app_event_bus.h"
#include "ui_msg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_EVT_DATA_MAX 512

typedef struct {
    app_evt_t evt;
    char      data[APP_EVT_DATA_MAX];
    int       aux1;
    unsigned  aux_sz;
    /* aux 紧随结构体之后（变长分配） */
} evt_msg_t;

typedef struct {
    void (*handler)(app_evt_t, const char *, int, const void *,
                    unsigned, void *);
    void *user;
} subscriber_t;

static subscriber_t g_sub;
static int g_next_handle = 1;

static void bus_dispatch(void *p)
{
    evt_msg_t *m = (evt_msg_t *)p;
    if (g_sub.handler)
        g_sub.handler(m->evt, m->data, m->aux1,
                      m->aux_sz ? (const void *)(m + 1) : NULL,
                      m->aux_sz, g_sub.user);
    free(m);
}

int app_event_post(app_evt_t evt, const char *data,
                   int aux1, const void *aux, unsigned aux_sz)
{
    if (aux_sz > APP_EVT_DATA_MAX)
        aux_sz = APP_EVT_DATA_MAX;
    evt_msg_t *m = (evt_msg_t *)malloc(sizeof(*m) + aux_sz);
    if (!m)
        return -1;
    m->evt = evt;
    m->data[0] = '\0';
    if (data)
        snprintf(m->data, sizeof(m->data), "%s", data);
    m->aux1 = aux1;
    m->aux_sz = aux_sz;
    if (aux_sz && aux)
        memcpy(m + 1, aux, aux_sz);
    return ui_msg_post(bus_dispatch, m);
}

int app_event_subscribe(void (*handler)(app_evt_t, const char *, int,
                                        const void *, unsigned, void *),
                        void *user)
{
    g_sub.handler = handler;
    g_sub.user = user;
    return g_next_handle++;
}

void app_event_unsubscribe(int handle)
{
    (void)handle;
    g_sub.handler = NULL;
    g_sub.user = NULL;
}

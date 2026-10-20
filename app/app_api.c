/*
 * app_api.c — UI→业务 意图接口实现（薄转发，行为与原直调完全一致）
 */
#include "app_api.h"
#include "app_state.h"
#include "app_events.h"
#include "app_event_bus.h"
#include "ui_main.h"            /* app_api_refresh_server_info → ui_main_refresh_server_info */
#include "wifi/wifi_manager.h"
#include "net/xz_protocol.h"
#include <stdio.h>
#include <string.h>

/* ---- 网络状态 ---- */

int app_api_wifi_connected(void)
{
    return wifi_manager_state() == WIFI_STATE_CONNECTED;
}

int app_api_wifi_state(void)
{
    return (int)wifi_manager_state();
}

void app_api_get_net_info(char *ip, int ip_sz, char *ssid, int ssid_sz)
{
    const char *p = wifi_manager_get_ip();
    const char *s = wifi_manager_get_ssid();
    if (ip)
        snprintf(ip, ip_sz, "%s", p ? p : "");
    if (ssid)
        snprintf(ssid, ssid_sz, "%s", s ? s : "");
}

/* ---- WiFi 配网 ---- */

int app_api_wifi_scan(wifi_ap_t *aps, int max_ap)
{
    return wifi_manager_scan(aps, max_ap);
}

int app_api_wifi_connect(const char *ssid, const char *psk, int timeout_ms)
{
    return wifi_manager_connect(ssid, psk, timeout_ms);
}

void app_api_wifi_save(const char *ssid, const char *psk)
{
    wifi_manager_save_cfg(ssid, psk);
}

/* ---- 语音会话 ---- */

int app_api_talk_start(void)
{
    return xz_protocol_talk_start();
}

int app_api_talking(void)
{
    return xz_protocol_talking();
}

void app_api_talk_stop(void)
{
    xz_protocol_talk_stop();
}

const char *app_api_activation_code(void)
{
    return xz_protocol_activation_code();
}

const char *app_api_session_id(void)
{
    return xz_protocol_session_id();
}

void app_api_refresh_server_info(void)
{
    ui_main_refresh_server_info();
}

/* ---- 生命周期 ---- */

void app_api_ensure_started(void)
{
    if (!app_api_wifi_connected())
        return;
    if (xz_protocol_started())
        return;
    if (xz_protocol_start(controller_event_cb, NULL) == 0)
        app_state_set(XZ_STATE_CONNECTING, "ws connecting");
}

void app_api_wifi_setup_done(int connected)
{
    printf("[app] wifi_setup_done connected=%d\n", connected);
    if (!connected)
        return;
    app_state_init();
    app_event_post(APP_EVT_NAVIGATE, NULL, APP_PAGE_MAIN, NULL, 0);
    app_api_ensure_started();
}

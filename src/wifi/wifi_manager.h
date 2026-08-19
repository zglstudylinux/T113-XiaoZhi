/*
 * wifi_manager.h — WiFi 连接管理（M1 实装：wpa_ctrl 封装）
 */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

typedef enum {
    WIFI_STATE_INACTIVE = 0,
    WIFI_STATE_SCANNING,
    WIFI_STATE_DISCONNECT,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_WRONG_KEY,
} wifi_state_t;

int  wifi_manager_open(void);
void wifi_manager_close(void);

#endif

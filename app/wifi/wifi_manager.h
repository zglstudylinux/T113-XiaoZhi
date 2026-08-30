/*
 * wifi_manager.h — WiFi 连接管理（wpa_supplicant ctrl 接口封装，自实现无库依赖）
 *
 * 板侧链路（M1 已在板上验证）：
 *   1. insmod /lib/modules/5.4.61/8723ds.ko     → wlan0 出现（开机未自动加载）
 *   2. wpa_supplicant -i wlan0 -c /etc/wifi/wpa_supplicant/wpa_supplicant.conf \
 *                      -B -O /var/run/wpa_supplicant
 *      （本板 conf 无 ctrl_interface 行，必须 -O 指定 ctrl 目录）
 *   3. ctrl 接口 = /var/run/wpa_supplicant/wlan0（unix dgram socket，文本协议）
 *   4. udhcpc -i wlan0 拿 IP
 */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>

#define WIFI_MAX_SSID_LEN  32
#define WIFI_MAX_AP        20

typedef enum {
    WIFI_STATE_INACTIVE = 0,    /* wlan0 不存在（驱动未加载） */
    WIFI_STATE_SCANNING,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,       /* wpa 已关联（IP 可能还没拿到） */
    WIFI_STATE_WRONG_KEY,       /* 4-way handshake 失败 */
    WIFI_STATE_ERROR,
} wifi_state_t;

typedef struct {
    char ssid[WIFI_MAX_SSID_LEN + 1];
    int16_t rssi;               /* dBm，负值 */
    uint8_t auth_open;          /* 1=开放网络（无需密码） */
} wifi_ap_t;

/* 初始化：加载驱动（若未加载）→ 起 wpa_supplicant → 建 ctrl 连接。
 * 返回 0 成功。幂等。内部起事件线程维护状态。 */
int  wifi_manager_open(void);
void wifi_manager_close(void);

/* 扫描（同步，约 5 秒，内部完成 scan + 等 results + 解析去重）。返回 AP 数，<0 失败。 */
int  wifi_manager_scan(wifi_ap_t *aps, int max_ap);

/* 连接指定 SSID（REMOVE all → ADD → SET ssid/psk → ENABLE → 轮询 wpa_state，
 * 最长 timeout_ms）。返回 0=关联成功。 */
int  wifi_manager_connect(const char *ssid, const char *psk, int timeout_ms);

/* 断开并移除所有 network 配置 */
int  wifi_manager_disconnect(void);

/* 状态查询 */
wifi_state_t wifi_manager_state(void);
const char *wifi_manager_get_ip(void);     /* 未拿到返回 NULL */
const char *wifi_manager_get_ssid(void);   /* 当前关联 SSID */

/* 配置持久化（/mnt/UDISK/xiaozhi/wifi.cfg，两行文本：ssid\npsk） */
int  wifi_manager_save_cfg(const char *ssid, const char *psk);
int  wifi_manager_load_cfg(char *ssid, int ssid_sz, char *psk, int psk_sz);

/* 用保存的配置连接；返回 0=已连上（含 DHCP） */
int  wifi_manager_auto_connect(void);

/* 起 udhcpc 拿 IP（阻塞最长 timeout_ms）。返回 0 成功。 */
int  wifi_manager_dhcp(int timeout_ms);

#endif

/*
 * app_api.h — UI→业务 意图接口（UI 层唯一允许调用的业务入口）
 *
 * blocking 风格（内部自带工作线程的地方自带），线程模型与 M3 相同。
 * ui/ 只 include 本头，不 include wifi_manager.h / xz_protocol.h ——
 * 分层由编译器强制（ui target 的 include 不含 app 子目录）。
 */
#ifndef APP_API_H
#define APP_API_H

#include <stdint.h>
#include "app_state.h"                  /* xz_state_t */
#include "wifi/wifi_manager.h"          /* wifi_ap_t / wifi_state_t（载荷类型） */

/* controller.c 提供（协议事件入口，app_api_ensure_started 注册给协议层） */
int controller_event_cb(int event, const char *data, void *user);

/* ---- 网络状态（只读查询） ---- */
int  app_api_wifi_connected(void);      /* 1 = 已连 */
int  app_api_wifi_state(void);          /* wifi_state_t */
void app_api_get_net_info(char *ip, int ip_sz, char *ssid, int ssid_sz);

/* ---- WiFi 配网（同步阻塞，UI 侧放工作线程调用） ---- */
int  app_api_wifi_scan(wifi_ap_t *aps, int max_ap);          /* 返回 AP 数 */
int  app_api_wifi_connect(const char *ssid, const char *psk, int timeout_ms);
void app_api_wifi_save(const char *ssid, const char *psk);

/* ---- 语音会话 ---- */
int  app_api_talk_start(void);          /* 0 成功开始 */
int  app_api_talking(void);
void app_api_talk_stop(void);

/* ---- 生命周期（替代原 main.c 被反向调用的两个函数） ---- */
void app_api_ensure_started(void);      /* 进小智页：确保协议在跑（幂等） */
void app_api_wifi_setup_done(int connected);   /* 配网成功回主页并启动 */

#endif

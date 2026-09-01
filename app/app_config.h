/*
 * app_config.h — 配置统一收敛（P5）
 *
 * 两部分：
 *   1. 板上数据目录：所有落盘文件（config.ini / wifi.cfg / diag dump）
 *      的路径从 app_config_data_dir() 派生——原 /mnt/UDISK/xiaozhi 散落
 *      6 处，现仅 CMake XZ_DATA_DIR 一处。
 *   2. 协议/固件常量：OTA URL、UA、板型上报名等（原 xz_protocol.c 内部
 *      #define，现集中声明，改配置不改代码逻辑）。
 *
 * 运行时可调项（ws_url/token 等）不在此——仍由 config.ini 承载
 * （app_config_load_ini 解析，xz_protocol 持有）。
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* 数据目录（板上 /mnt/UDISK/xiaozhi；host 用 build 目录旁 ./data）。
 * 返回静态串，进程内稳定。 */
const char *app_config_data_dir(void);

/* 数据目录下文件完整路径（返回静态串——单缓冲，别并存两次调用的返回值） */
const char *app_config_path(const char *name);

/* ---- 固件/协议常量（原散落 xz_protocol.c / http 请求里） ---- */
#define XZ_FW_VERSION        "1.0.0"
#define XZ_HTTP_UA           "xiaozhi-t113/" XZ_FW_VERSION
#define XZ_OTA_URL           "https://api.tenclass.net/xiaozhi/ota/"
/* OTA 上报的板型/芯片名：必须是服务器已注册的板型（面包板兼容方案） */
#define XZ_REPORT_CHIP       "esp32s3"
#define XZ_REPORT_BOARD      "bread-compact-wifi"

/* config.ini 默认值（文件缺失或字段为空时生效） */
#define XZ_DEF_WS_URL        "wss://api.tenclass.net/xiaozhi/v1/"
#define XZ_DEF_TOKEN         "test"

/* ini 解析：key = value 行式。回调对每个字段调用（key/val 已去空白）。
 * 供 xz_protocol 使用——避免配置结构体泄漏到公共头。 */
typedef void (*app_config_kv_cb)(void *user, const char *key, const char *val);
int app_config_load_ini(const char *path, app_config_kv_cb cb, void *user);

#endif

/*
 * wifi_manager.c — WiFi 连接管理实装
 *
 * 自实现 wpa_supplicant ctrl 接口（unix DGRAM socket + 文本协议，参考
 * app_sdk wpa_manager.c 的命令序列；不依赖 libwpa_client.so——板上没有）。
 *
 * 线程模型：open() 起一个事件线程（attach 监听 CONNECTED/DISCONNECTED/
 * WRONG_KEY），命令收发走独立 cmd socket（请求-应答，无事件混入）。
 */
#include "wifi_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <stdatomic.h>

#define WPA_IFACE        "wlan0"
#define WPA_CONF         "/etc/wifi/wpa_supplicant/wpa_supplicant.conf"
#define WPA_CTRL_DIR     "/var/run/wpa_supplicant"
#define WPA_CTRL_PATH    WPA_CTRL_DIR "/" WPA_IFACE
#define DRV_KO           "/lib/modules/5.4.61/8723ds.ko"

#define WIFI_CFG_PATH    "/mnt/UDISK/xiaozhi/wifi.cfg"

#define REPLY_MAX        4096

/* ---------------- ctrl socket（DGRAM，协议：sendto 请求 / recvfrom 应答） -------- */

static int ctrl_open(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    /* 客户端必须先 bind 本地地址，否则 wpa_supplicant 无法回包。
     * 后缀用静态计数器 —— 同线程多次 ctrl_open 也能 bind 到不同路径 */
    static int cli_seq = 0;
    struct sockaddr_un local = { .sun_family = AF_UNIX };
    snprintf(local.sun_path, sizeof(local.sun_path),
             "/tmp/wpa_cli_%d_%d", getpid(), cli_seq++);
    unlink(local.sun_path);
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        close(fd);
        return -1;
    }
    struct sockaddr_un dest = { .sun_family = AF_UNIX };
    strncpy(dest.sun_path, path, sizeof(dest.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        close(fd);
        unlink(local.sun_path);
        return -1;
    }
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

/* 发一条命令拿应答（cmd 不带 \n；reply 以 \0 结尾）。0=成功 */
static int ctrl_cmd(int fd, const char *cmd, char *reply, int reply_sz)
{
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "%s", cmd);
    if (send(fd, buf, n, 0) < 0)
        return -1;
    int len = recv(fd, reply, reply_sz - 1, 0);
    if (len < 0) {
        printf("[wifi] ctrl_cmd '%s' recv err=%d\n", cmd, errno);
        return -1;
    }
    reply[len] = '\0';
    printf("[wifi] << '%s' (%d bytes): %.200s\n", cmd, len, reply);
    /* 应答可能带 unsolicited 事件行，取最后一个 OK/FAIL 或原样返回 */
    return 0;
}

static int ctrl_ok(int fd, const char *cmd)
{
    char reply[256];
    if (ctrl_cmd(fd, cmd, reply, sizeof(reply)) < 0)
        return -1;
    return strncmp(reply, "OK", 2) == 0 ? 0 : -1;
}

/* ---------------- 模块状态 ---------------- */

static int g_cmd_fd = -1;              /* 命令通道 */
static int g_evt_fd = -1;              /* 事件通道（attach） */
static pthread_t g_evt_thread;
static atomic_int g_run = 0;
static atomic_int g_state = WIFI_STATE_INACTIVE;
static char g_cur_ssid[WIFI_MAX_SSID_LEN + 1];
static char g_cur_ip[16];

static void set_state(wifi_state_t s) { atomic_store(&g_state, s); }

wifi_state_t wifi_manager_state(void) { return (wifi_state_t)atomic_load(&g_state); }
const char *wifi_manager_get_ip(void)  { return g_cur_ip[0] ? g_cur_ip : NULL; }
const char *wifi_manager_get_ssid(void){ return g_cur_ssid[0] ? g_cur_ssid : NULL; }

/* ---------------- 底层拉起 ---------------- */

static int iface_exists(void)
{
    struct stat st;
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/net/%s", WPA_IFACE);
    return stat(path, &st) == 0;
}

static int wpa_running(void)
{
    return access(WPA_CTRL_PATH, F_OK) == 0;
}

/* 加载驱动 + 起 wpa_supplicant（幂等，system 调用） */
static int bring_up(void)
{
    if (!iface_exists()) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "insmod %s 2>/dev/null", DRV_KO);
        int retry;
        for (retry = 0; retry < 10 && !iface_exists(); retry++) {
            system(cmd);
            usleep(500 * 1000);
        }
        if (!iface_exists()) {
            printf("[wifi] 8723ds 驱动加载失败\n");
            return -1;
        }
        printf("[wifi] 8723ds 已加载\n");
    }
    if (!wpa_running()) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd),
                 "wpa_supplicant -i %s -c %s -B -O %s",
                 WPA_IFACE, WPA_CONF, WPA_CTRL_DIR);
        int retry;
        for (retry = 0; retry < 10 && !wpa_running(); retry++) {
            system(cmd);
            usleep(500 * 1000);
        }
        if (!wpa_running()) {
            printf("[wifi] wpa_supplicant 启动失败\n");
            return -1;
        }
        printf("[wifi] wpa_supplicant 已启动\n");
    }
    return 0;
}

/* ---------------- 事件线程 ---------------- */

static void poll_status(void)
{
    char reply[REPLY_MAX];
    if (ctrl_cmd(g_cmd_fd, "STATUS", reply, sizeof(reply)) != 0)
        return;
    char *wpa = strstr(reply, "wpa_state=");
    if (!wpa)
        return;
    if (strncmp(wpa + 10, "COMPLETED", 9) == 0)
        set_state(WIFI_STATE_CONNECTED);
    else if (strncmp(wpa + 10, "SCANNING", 8) == 0)
        set_state(WIFI_STATE_SCANNING);
    else if (strncmp(wpa + 10, "ASSOCIATING", 11) == 0)
        set_state(WIFI_STATE_CONNECTING);
    else
        set_state(WIFI_STATE_DISCONNECTED);
    /* 顺带更新 ssid/ip */
    char *ssid = strstr(reply, "\nssid=") ?: strstr(reply, "ssid=");
    char *ip = strstr(reply, "ip_address=");
    if (ssid) {
        ssid += (ssid[0] == '\n') ? 6 : 5;
        char *eol = strchr(ssid, '\n');
        int len = eol ? (int)(eol - ssid) : (int)strlen(ssid);
        if (len > WIFI_MAX_SSID_LEN) len = WIFI_MAX_SSID_LEN;
        memcpy(g_cur_ssid, ssid, len);
        g_cur_ssid[len] = '\0';
    } else {
        g_cur_ssid[0] = '\0';
    }
    if (ip) {
        ip += 11;
        char *eol = strchr(ip, '\n');
        int len = eol ? (int)(eol - ip) : (int)strlen(ip);
        if (len > 15) len = 15;
        memcpy(g_cur_ip, ip, len);
        g_cur_ip[len] = '\0';
    }
}

static void *event_thread(void *arg)
{
    (void)arg;
    char buf[1024];
    while (atomic_load(&g_run)) {
        if (g_evt_fd < 0) {
            usleep(500 * 1000);
            continue;
        }
        /* 阻塞收事件（5s 超时兜底，回来检查 g_run） */
        int len = recv(g_evt_fd, buf, sizeof(buf) - 1, 0);
        if (len <= 0) {
            continue;
        }
        buf[len] = '\0';
        if (strstr(buf, "CTRL-EVENT-CONNECTED")) {
            set_state(WIFI_STATE_CONNECTED);
            system("udhcpc -i " WPA_IFACE " -t 5 -T 2 -A 3 -q >/dev/null 2>&1 &");
            ctrl_ok(g_cmd_fd, "SAVE_CONFIG");
            poll_status();
        } else if (strstr(buf, "CTRL-EVENT-DISCONNECTED")) {
            set_state(WIFI_STATE_DISCONNECTED);
            g_cur_ip[0] = '\0';
        } else if (strstr(buf, "CTRL-EVENT-SSID-TEMP-DISABLED") ||
                   strstr(buf, "WPA: 4-Way Handshake failed")) {
            set_state(WIFI_STATE_WRONG_KEY);
        } else if (strstr(buf, "CTRL-EVENT-SCAN-RESULTS")) {
            if (wifi_manager_state() == WIFI_STATE_SCANNING)
                set_state(WIFI_STATE_DISCONNECTED);
        }
    }
    return NULL;
}

/* ---------------- 公共 API ---------------- */

int wifi_manager_open(void)
{
    if (g_cmd_fd >= 0)
        return 0;

    if (bring_up() != 0) {
        set_state(WIFI_STATE_ERROR);
        return -1;
    }

    g_cmd_fd = ctrl_open(WPA_CTRL_PATH);
    if (g_cmd_fd < 0) {
        printf("[wifi] ctrl 命令通道连接失败\n");
        set_state(WIFI_STATE_ERROR);
        return -1;
    }
    /* 事件通道：单独一个 socket + ATTACH */
    g_evt_fd = ctrl_open(WPA_CTRL_PATH);
    if (g_evt_fd >= 0)
        ctrl_ok(g_evt_fd, "ATTACH");

    atomic_store(&g_run, 1);
    pthread_create(&g_evt_thread, NULL, event_thread, NULL);

    poll_status();
    printf("[wifi] open 完成 state=%d\n", wifi_manager_state());
    return 0;
}

void wifi_manager_close(void)
{
    atomic_store(&g_run, 0);
    if (g_evt_thread)
        pthread_join(g_evt_thread, NULL);   /* 事件 recv 5s 超时后自然退出 */
    if (g_evt_fd >= 0) close(g_evt_fd);
    if (g_cmd_fd >= 0) close(g_cmd_fd);
    g_evt_fd = g_cmd_fd = -1;
}

int wifi_manager_scan(wifi_ap_t *aps, int max_ap)
{
    if (g_cmd_fd < 0 || aps == NULL || max_ap <= 0)
        return -1;

    set_state(WIFI_STATE_SCANNING);
    if (ctrl_ok(g_cmd_fd, "SCAN") != 0) {
        /* SCAN 可能因正在扫描返回 FAIL-BUSY，直接试取结果 */
        char rep[64];
        ctrl_cmd(g_cmd_fd, "SCAN_RESULTS", rep, sizeof(rep));
        if (rep[0] == '\0') {
            set_state(WIFI_STATE_DISCONNECTED);
            return -1;
        }
    }
    usleep(3500 * 1000);   /* 等扫描完成（dmesg 观察 2.5~3.5s） */

    char reply[REPLY_MAX];
    if (ctrl_cmd(g_cmd_fd, "SCAN_RESULTS", reply, sizeof(reply)) != 0) {
        set_state(WIFI_STATE_DISCONNECTED);
        return -1;
    }
    set_state(WIFI_STATE_DISCONNECTED);

    /* 解析：bssid / freq / signal / flags / ssid（ssid 可含空格，取行尾） */
    int count = 0;
    char *line = strtok(reply, "\n");
    line = strtok(NULL, "\n");          /* 跳过表头 */
    for (; line && count < max_ap; line = strtok(NULL, "\n")) {
        char bssid[32];
        int freq, sig;
        char flags[128] = "";
        char ssid[WIFI_MAX_SSID_LEN + 1] = "";
        int n = sscanf(line, "%31s %d %d %127s %32[^\n]",
                       bssid, &freq, &sig, flags, ssid);
        if (n < 4 || ssid[0] == '\0')
            continue;
        /* 去重（同名 SSID 取信号强的） */
        int i, dup = 0;
        for (i = 0; i < count; i++) {
            if (strcmp(aps[i].ssid, ssid) == 0) {
                if (sig > aps[i].rssi)
                    aps[i].rssi = (int16_t)sig;
                dup = 1;
                break;
            }
        }
        if (dup)
            continue;
        strncpy(aps[count].ssid, ssid, WIFI_MAX_SSID_LEN);
        aps[count].ssid[WIFI_MAX_SSID_LEN] = '\0';
        aps[count].rssi = (int16_t)sig;
        aps[count].auth_open = (strstr(flags, "WPA") == NULL &&
                                strstr(flags, "WEP") == NULL) ? 1 : 0;
        count++;
    }
    /* 按 RSSI 降序（简单插入排序，n≤20） */
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (aps[j].rssi > aps[i].rssi) {
                wifi_ap_t t = aps[i]; aps[i] = aps[j]; aps[j] = t;
            }
        }
    }
    return count;
}

int wifi_manager_connect(const char *ssid, const char *psk, int timeout_ms)
{
    if (g_cmd_fd < 0 || ssid == NULL || ssid[0] == '\0')
        return -1;

    char cmd[256], reply[256];

    ctrl_ok(g_cmd_fd, "REMOVE_NETWORK all");

    ctrl_cmd(g_cmd_fd, "ADD_NETWORK", reply, sizeof(reply));
    int net_id = atoi(reply);
    if (net_id < 0) {
        printf("[wifi] ADD_NETWORK 失败: %s\n", reply);
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "SET_NETWORK %d ssid \"%s\"", net_id, ssid);
    if (ctrl_ok(g_cmd_fd, cmd) != 0) return -1;

    if (psk && psk[0]) {
        snprintf(cmd, sizeof(cmd), "SET_NETWORK %d psk \"%s\"", net_id, psk);
        if (ctrl_ok(g_cmd_fd, cmd) != 0) return -1;
    } else {
        snprintf(cmd, sizeof(cmd), "SET_NETWORK %d key_mgmt NONE", net_id);
        if (ctrl_ok(g_cmd_fd, cmd) != 0) return -1;
    }

    snprintf(cmd, sizeof(cmd), "ENABLE_NETWORK %d", net_id);
    if (ctrl_ok(g_cmd_fd, cmd) != 0) return -1;
    snprintf(cmd, sizeof(cmd), "SELECT_NETWORK %d", net_id);
    if (ctrl_ok(g_cmd_fd, cmd) != 0) return -1;

    set_state(WIFI_STATE_CONNECTING);
    strncpy(g_cur_ssid, ssid, WIFI_MAX_SSID_LEN);
    g_cur_ssid[WIFI_MAX_SSID_LEN] = '\0';

    /* 轮询等关联结果：主动 STATUS 为准（事件线程可能错过 CONNECTED） */
    int waited = 0;
    while (waited < timeout_ms) {
        usleep(300 * 1000);
        waited += 300;
        poll_status();
        wifi_state_t st = wifi_manager_state();
        if (st == WIFI_STATE_CONNECTED)
            return wifi_manager_dhcp(15000);
        if (st == WIFI_STATE_WRONG_KEY)
            return -2;
    }
    set_state(WIFI_STATE_DISCONNECTED);
    return -1;
}

int wifi_manager_disconnect(void)
{
    if (g_cmd_fd < 0)
        return -1;
    g_cur_ip[0] = '\0';
    g_cur_ssid[0] = '\0';
    set_state(WIFI_STATE_DISCONNECTED);
    return ctrl_ok(g_cmd_fd, "REMOVE_NETWORK all");
}

int wifi_manager_dhcp(int timeout_ms)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "udhcpc -i %s -t 5 -T 2 -A 3 -n -q >/dev/null 2>&1",
             WPA_IFACE);
    /* -n: 拿不到 IP 就退出（失败非 0）；整个调用受 alarm 限制 */
    int waited = 0, ok = -1;
    while (waited < timeout_ms) {
        if (system(cmd) == 0) { ok = 0; break; }
        waited += 5000;
    }
    if (ok == 0)
        poll_status();
    return ok;
}

/* ---------------- 配置持久化 ---------------- */

int wifi_manager_save_cfg(const char *ssid, const char *psk)
{
    FILE *f = fopen(WIFI_CFG_PATH, "w");
    if (!f) {
        printf("[wifi] 保存配置失败: %s\n", WIFI_CFG_PATH);
        return -1;
    }
    fprintf(f, "%s\n%s\n", ssid ? ssid : "", psk ? psk : "");
    fclose(f);
    return 0;
}

int wifi_manager_load_cfg(char *ssid, int ssid_sz, char *psk, int psk_sz)
{
    FILE *f = fopen(WIFI_CFG_PATH, "r");
    if (!f)
        return -1;
    char buf[128];
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return -1; }
    buf[strcspn(buf, "\n")] = '\0';
    snprintf(ssid, ssid_sz, "%s", buf);
    if (!fgets(buf, sizeof(buf), f)) buf[0] = '\0';
    buf[strcspn(buf, "\n")] = '\0';
    snprintf(psk, psk_sz, "%s", buf);
    fclose(f);
    return (ssid[0] != '\0') ? 0 : -1;
}

int wifi_manager_auto_connect(void)
{
    char ssid[64], psk[64];
    if (wifi_manager_load_cfg(ssid, sizeof(ssid), psk, sizeof(psk)) != 0)
        return -1;
    /* wpa_supplicant 自身配置还在时重启 app，网络可能仍是关联状态 —— 直接用 */
    poll_status();
    if (wifi_manager_state() == WIFI_STATE_CONNECTED &&
        strcmp(g_cur_ssid, ssid) == 0) {
        printf("[wifi] 已处于连接状态（%s），跳过重连\n", ssid);
        /* IP 可能因 app 重启丢失 —— 补一次 DHCP */
        if (wifi_manager_get_ip() == NULL)
            wifi_manager_dhcp(15000);
        return 0;
    }
    printf("[wifi] 自动重连 %s\n", ssid);
    return wifi_manager_connect(ssid, psk[0] ? psk : NULL, 20000);
}

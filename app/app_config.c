/*
 * app_config.c — 配置统一收敛实现（见 app_config.h）
 *
 * 数据目录解析顺序（与 ui_res 同思路，进程内一次）：
 *   1. $XZ_DATA_DIR 环境变量
 *   2. 编译期 XZ_DATA_DIR（t113=/mnt/UDISK/xiaozhi；host=<build>/../data）
 */
#include "app_config.h"
#include "osal/osal_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef XZ_DATA_DIR
#define XZ_DATA_DIR "."
#endif

#define MAX_PATH_LEN 256

static char g_dir[MAX_PATH_LEN];
static char g_path[MAX_PATH_LEN];
static int  g_resolved;

const char *app_config_data_dir(void)
{
    if (!g_resolved) {
        g_resolved = 1;
        const char *env = getenv("XZ_DATA_DIR");
        snprintf(g_dir, sizeof(g_dir), "%s",
                 (env && env[0]) ? env : XZ_DATA_DIR);
    }
    return g_dir;
}

const char *app_config_path(const char *name)
{
    snprintf(g_path, sizeof(g_path), "%s/%s", app_config_data_dir(), name);
    return g_path;
}

int app_config_load_ini(const char *path, app_config_kv_cb cb, void *user)
{
    osal_file_t *f = osal_fopen(path, "r");
    if (!f)
        return -1;
    char line[256];
    while (osal_getline(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = line, *val = eq + 1;
        /* 去首尾空白（与原 cfg_parse 逐字符逻辑等价） */
        while (*key == ' ' || *key == '\t') key++;
        char *e = key + strlen(key);
        while (e > key && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
        while (*val == ' ' || *val == '\t') val++;
        e = val + strlen(val);
        while (e > val && (e[-1] == ' ' || e[-1] == '\t' ||
                           e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';
        cb(user, key, val);
    }
    osal_fclose(f);
    return 0;
}

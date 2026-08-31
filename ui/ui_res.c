/*
 * ui_res.c — UI 资源路径运行时解析（详见 ui_res.h）
 */
#include "ui_res.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#ifndef XZ_RES_PATH
#define XZ_RES_PATH "."          /* 编译期兜底（CMake 注入） */
#endif

#define MAX_RES_PATH 256

static char g_res_dir[MAX_RES_PATH];
static int  g_res_resolved;
static char g_img_path[MAX_RES_PATH];
static char g_font_path[MAX_RES_PATH];

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* dir 下是否有 images/bg_main.png（资源根目录的标志文件） */
static int looks_like_res_dir(const char *dir)
{
    char probe[MAX_RES_PATH];
    snprintf(probe, sizeof(probe), "%s/images/bg_main.png", dir);
    return exists(probe);
}

/* 解析资源根目录（进程内只跑一次） */
static void resolve(void)
{
    if (g_res_resolved)
        return;
    g_res_resolved = 1;

    /* 1. 显式环境变量 */
    const char *env = getenv("XZ_RES_DIR");
    if (env && env[0]) {
        snprintf(g_res_dir, sizeof(g_res_dir), "%s", env);
        return;
    }

    /* exe 真实路径（/proc/self/exe，Linux 通用） */
    char self[MAX_RES_PATH];
    self[0] = '\0';
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0)
        self[n] = '\0';
    char *slash = strrchr(self, '/');
    if (slash)
        *slash = '\0';           /* self = exe 所在目录 */

    /* 2. exe 同目录（板上：/mnt/UDISK/xiaozhi/xiaozhi → /mnt/UDISK/xiaozhi） */
    if (slash && looks_like_res_dir(self)) {
        snprintf(g_res_dir, sizeof(g_res_dir), "%s", self);
        return;
    }

    /* 3. exe 上级 assets/（仓库开发：build-host/xiaozhi → ../assets）；
     *    CI zip 布局（xiaozhi/xiaozhi + xiaozhi/assets/）同样命中 */
    if (slash) {
        char cand[MAX_RES_PATH];
        snprintf(cand, sizeof(cand), "%s/assets", self);
        if (looks_like_res_dir(cand)) {
            snprintf(g_res_dir, sizeof(g_res_dir), "%s", cand);
            return;
        }
    }

    /* 4. 编译期兜底（t113=/mnt/UDISK/xiaozhi；host=仓库 assets 绝对路径） */
    snprintf(g_res_dir, sizeof(g_res_dir), "%s", XZ_RES_PATH);
}

const char *ui_res_dir(void)
{
    resolve();
    return g_res_dir;
}

const char *ui_res_img(const char *name)
{
    /* LVGL 文件路径：盘符 S: + 绝对路径（lv_conf.h: LV_FS_POSIX_LETTER='S',
     * LV_FS_POSIX_PATH=""） */
    snprintf(g_img_path, sizeof(g_img_path), "S:%s/images/%s", ui_res_dir(), name);
    return g_img_path;
}

const char *ui_res_font(const char *name)
{
    /* FreeType 走标准 fopen：真实路径，不能带 LVGL 盘符 */
    snprintf(g_font_path, sizeof(g_font_path), "%s/fonts/%s", ui_res_dir(), name);
    return g_font_path;
}

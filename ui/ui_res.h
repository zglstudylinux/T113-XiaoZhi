/*
 * ui_res.h — UI 资源路径运行时解析
 *
 * 同一份产物跑三种布局（板上 / CI artifact 解压目录 / 仓库开发）：
 *   1. $XZ_RES_DIR 环境变量（显式覆盖）
 *   2. exe 同目录下 images/bg_main.png 存在（板上：/mnt/UDISK/xiaozhi/xiaozhi）
 *   3. exe 同目录 assets/ 或 上级 assets/ 存在（CI zip / 仓库 build-host/）
 *   4. 编译期 XZ_RES_PATH 兜底（t113=/mnt/UDISK/xiaozhi，host=仓库 assets）
 *
 * 两条取径 API 的区别：lv_img_set_src 的文件路径要带 LVGL 盘符（S:，
 * 走 lv_fs→POSIX），FreeType 字体走标准 fopen（真实路径，不能带盘符）。
 */
#ifndef UI_RES_H
#define UI_RES_H

#ifdef __cplusplus
extern "C" {
#endif

/* 资源根目录（真实路径，如 /mnt/UDISK/xiaozhi；懒解析，首次调用后缓存） */
const char *ui_res_dir(void);

/* images/ 下文件的 LVGL 路径（S: 前缀）。
 * 返回静态缓存指针，长期有效（lv_img_set_src 只存指针不拷贝字符串） */
const char *ui_res_img(const char *name);

/* fonts/ 下文件的真实路径（FreeType 用；返回静态缓冲） */
const char *ui_res_font(const char *name);

#ifdef __cplusplus
}
#endif
#endif /* UI_RES_H */

/*
 * ui_msg.h — 线程→UI 消息队列（线程安全）
 *
 * LVGL 的 lv_mem / lv_timer / lv_async_call 都不是线程安全的
 * （lv_async_call 内部 lv_mem_alloc + lv_timer_create）。工作线程直接调
 * 会与主循环并发分配 LVGL 堆 → tlsf 元数据写坏 → 随机 SIGSEGV
 * （实测崩在 lv_async_timer_cb，反复切页压测复现）。
 *
 * 改法：线程只写自带的互斥队列（malloc，glibc 堆线程安全），
 * 主循环的泵定时器每 20ms 取出并执行。
 */
#ifndef UI_MSG_H
#define UI_MSG_H

typedef void (*ui_msg_cb_t)(void *user);

/* 线程安全：任意线程调用，cb 在主循环执行（可安全操作 LVGL）。
 * 返回 0 已入队；内存不足返回 -1（cb 不执行） */
int ui_msg_post(ui_msg_cb_t cb, void *user);

/* 主循环初始化/停止（停止时清空队列，未执行的消息释放） */
void ui_msg_init(void);
void ui_msg_deinit(void);

#endif

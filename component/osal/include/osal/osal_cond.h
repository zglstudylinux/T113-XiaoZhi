/*
 * osal_cond.h — 条件变量（音频播放队列生产者/消费者用）
 */
#ifndef OSAL_COND_H
#define OSAL_COND_H

#include "osal_types.h"
#include "osal_mutex.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_cond *osal_cond_t;

#define OSAL_COND_WAIT_FOREVER 0xFFFFFFFFu

osal_cond_t osal_cond_new(void);
/* 返回 1=被唤醒，0=超时 */
int  osal_cond_wait(osal_cond_t c, osal_mutex_t m, uint32_t timeout_ms);
void osal_cond_signal(osal_cond_t c);
void osal_cond_broadcast(osal_cond_t c);
void osal_cond_free(osal_cond_t c);

#ifdef __cplusplus
}
#endif
#endif /* OSAL_COND_H */

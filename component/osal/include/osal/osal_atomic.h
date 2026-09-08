/*
 * osal_atomic.h — 原子整型（替代 stdatomic，MCU 侧可能无 <stdatomic.h>）
 */
#ifndef OSAL_ATOMIC_H
#define OSAL_ATOMIC_H

#include "osal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { volatile int v; } osal_atomic_t;

/* osal_atomic_init 用于静态实例初始化；运行时置位用 osal_atomic_set */
#define OSAL_ATOMIC_INIT(v) { (v) }
#define OSAL_ATOMIC_INIT_ZERO { 0 }

void osal_atomic_set(osal_atomic_t *a, int v);
int  osal_atomic_get(const osal_atomic_t *a);

#ifdef __cplusplus
}
#endif
#endif /* OSAL_ATOMIC_H */

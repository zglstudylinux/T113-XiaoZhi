/*
 * osal_mutex.h — 互斥锁
 */
#ifndef OSAL_MUTEX_H
#define OSAL_MUTEX_H

#include "osal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_mutex *osal_mutex_t;

osal_mutex_t osal_mutex_new(void);
void         osal_mutex_lock(osal_mutex_t m);
void         osal_mutex_unlock(osal_mutex_t m);
void         osal_mutex_free(osal_mutex_t m);

#ifdef __cplusplus
}
#endif
#endif /* OSAL_MUTEX_H */

/*
 * osal_thread.h — 线程
 */
#ifndef OSAL_THREAD_H
#define OSAL_THREAD_H

#include "osal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osal_thread *osal_thread_t;
typedef void *(*osal_thread_fn_t)(void *arg);

int  osal_thread_create(osal_thread_t *out, osal_thread_fn_t fn, void *arg);
void osal_thread_detach(osal_thread_t t);
void osal_thread_join(osal_thread_t t);

#ifdef __cplusplus
}
#endif
#endif /* OSAL_THREAD_H */

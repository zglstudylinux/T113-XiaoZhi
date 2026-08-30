/*
 * osal_thread.c — POSIX 实现：pthread 封装
 */
#include "osal/osal_thread.h"
#include <stdlib.h>
#include <pthread.h>

int osal_thread_create(osal_thread_t *out, osal_thread_fn_t fn, void *arg)
{
    pthread_t *h = (pthread_t *)malloc(sizeof(pthread_t));
    if (!h)
        return OSAL_ERR;
    if (pthread_create(h, NULL, fn, arg) != 0) {
        free(h);
        return OSAL_ERR;
    }
    *out = (osal_thread_t)h;
    return OSAL_OK;
}

void osal_thread_detach(osal_thread_t t)
{
    pthread_t *h = (pthread_t *)t;
    if (h) {
        pthread_detach(*h);
        free(h);
    }
}

void osal_thread_join(osal_thread_t t)
{
    pthread_t *h = (pthread_t *)t;
    if (h) {
        pthread_join(*h, NULL);
        free(h);
    }
}

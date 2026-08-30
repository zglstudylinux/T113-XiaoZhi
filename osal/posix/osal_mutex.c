/*
 * osal_mutex.c — POSIX 实现
 */
#include "osal/osal_mutex.h"
#include <stdlib.h>
#include <pthread.h>

osal_mutex_t osal_mutex_new(void)
{
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (!m)
        return NULL;
    if (pthread_mutex_init(m, NULL) != 0) {
        free(m);
        return NULL;
    }
    return (osal_mutex_t)m;
}

void osal_mutex_lock(osal_mutex_t m)   { if (m) pthread_mutex_lock((pthread_mutex_t *)m); }
void osal_mutex_unlock(osal_mutex_t m) { if (m) pthread_mutex_unlock((pthread_mutex_t *)m); }

void osal_mutex_free(osal_mutex_t m)
{
    if (m) {
        pthread_mutex_destroy((pthread_mutex_t *)m);
        free(m);
    }
}

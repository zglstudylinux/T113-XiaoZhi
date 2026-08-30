/*
 * osal_cond.c — POSIX 实现：条件变量 + 带超时等待
 */
#include "osal/osal_cond.h"
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

osal_cond_t osal_cond_new(void)
{
    pthread_cond_t *c = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
    if (!c)
        return NULL;
    if (pthread_cond_init(c, NULL) != 0) {
        free(c);
        return NULL;
    }
    return (osal_cond_t)c;
}

int osal_cond_wait(osal_cond_t c, osal_mutex_t m, uint32_t timeout_ms)
{
    pthread_cond_t *pc = (pthread_cond_t *)c;
    pthread_mutex_t *pm = (pthread_mutex_t *)m;
    if (!pc || !pm)
        return 0;

    if (timeout_ms == 0xFFFFFFFFu) {   /* 永久等待 */
        return pthread_cond_wait(pc, pm) == 0 ? 1 : 0;
    }

    struct timespec abst;
    clock_gettime(CLOCK_REALTIME, &abst);
    abst.tv_sec  += (time_t)(timeout_ms / 1000U);
    abst.tv_nsec += (long)(timeout_ms % 1000U) * 1000000L;
    if (abst.tv_nsec >= 1000000000L) {
        abst.tv_sec  += 1;
        abst.tv_nsec -= 1000000000L;
    }

    int rc;
    do {
        rc = pthread_cond_timedwait(pc, pm, &abst);
    } while (rc == EINTR);

    return (rc == 0) ? 1 : 0;   /* ETIMEDOUT → 0 */
}

void osal_cond_signal(osal_cond_t c)    { if (c) pthread_cond_signal((pthread_cond_t *)c); }
void osal_cond_broadcast(osal_cond_t c) { if (c) pthread_cond_broadcast((pthread_cond_t *)c); }

void osal_cond_free(osal_cond_t c)
{
    if (c) {
        pthread_cond_destroy((pthread_cond_t *)c);
        free(c);
    }
}

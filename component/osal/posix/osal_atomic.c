/*
 * osal_atomic.c — POSIX 实现：GCC 原子内建（4 字节对齐 int 读写在 armv7/x86
 * 上本身原子，这里用 __atomic 保证跨平台语义一致）
 */
#include "osal/osal_atomic.h"

void osal_atomic_set(osal_atomic_t *a, int v)
{
    __atomic_store_n(&a->v, v, __ATOMIC_SEQ_CST);
}

int osal_atomic_get(const osal_atomic_t *a)
{
    return __atomic_load_n(&a->v, __ATOMIC_SEQ_CST);
}

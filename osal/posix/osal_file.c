/*
 * osal_file.c — POSIX 实现：stdio 透传
 */
#include "osal/osal_file.h"

osal_file_t *osal_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int osal_fread(void *buf, size_t n, osal_file_t *f)
{
    return (int)fread(buf, 1, n, f);
}

int osal_fwrite(const void *buf, size_t n, osal_file_t *f)
{
    return (int)fwrite(buf, 1, n, f);
}

void osal_fclose(osal_file_t *f)
{
    if (f)
        fclose(f);
}

/*
 * osal_file.h — 文件 IO（配置读取 / wifi.cfg / 诊断 dump；MCU 映射 FATFS）
 */
#ifndef OSAL_FILE_H
#define OSAL_FILE_H

#include "osal_types.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 当前实现直接透传 FILE*（posix）；MCU 侧换 FILE* → FIL* */
typedef FILE osal_file_t;

osal_file_t *osal_fopen(const char *path, const char *mode);
int   osal_fread(void *buf, size_t n, osal_file_t *f);     /* 返回读到的字节数 */
int   osal_fwrite(const void *buf, size_t n, osal_file_t *f);
void  osal_fclose(osal_file_t *f);

#ifdef __cplusplus
}
#endif
#endif /* OSAL_FILE_H */

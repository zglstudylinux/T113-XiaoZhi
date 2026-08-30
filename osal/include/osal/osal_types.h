/*
 * osal_types.h — OSAL 公共类型（纯 C99，业务层唯一可见的 OSAL 类型面）
 *
 * OSAL 分层说明见 docs/architecture-refactor.md：
 * 接口头在 osal/include/osal/，POSIX 参考实现在 osal/posix/；
 * 移植到 MCU/RTOS 时整体替换 osal/posix/ 为目标平台实现，业务层零改动。
 */
#ifndef OSAL_TYPES_H
#define OSAL_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 返回值约定：0 成功 / -1 失败（与项目现有约定一致） */
#define OSAL_OK   0
#define OSAL_ERR  (-1)

#ifdef __cplusplus
}
#endif
#endif /* OSAL_TYPES_H */

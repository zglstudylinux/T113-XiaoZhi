/*
 * http_client.h — 极简 HTTP 客户端（P5 从 xz_protocol.c 的 https_post 抽出）
 *
 * 传输走 osal_net + osal_tls（与 ws_client 同一套 OSAL 设施）。
 * 证书不校验（板上无 CA bundle，与 ws_client 一致）。
 */
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

/* POST url（http:// 或 https://），携带 nhdr 个附加头与 body。
 * 返回响应 body（malloc，调用方 free）；失败 NULL。
 * 非 2xx 不算失败（调用方看 body 内容），仅告警。 */
char *http_post(const char *url, const char *headers[], int nhdr,
                const char *body);

#endif

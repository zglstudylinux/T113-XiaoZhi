/*
 * ws_client.h — WebSocket 客户端（RFC6455 over OpenSSL BIO，M2 实装）
 */
#ifndef WS_CLIENT_H
#define WS_CLIENT_H

int ws_client_init(void);
void ws_client_deinit(void);

#endif

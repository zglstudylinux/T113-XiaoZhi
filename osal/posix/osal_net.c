/*
 * osal_net.c — POSIX 实现：getaddrinfo + 非阻塞 connect + poll 三态
 *
 * 等价迁移自 ws_client.c 原生 socket 连接段（M2/M3 验证过的行为）：
 * - AF_INET + SOCK_STREAM
 * - 非阻塞 connect + poll(POLLOUT) 等待 + SO_ERROR 校验
 * - 连上后转回阻塞（收发路径各自处理超时）
 */
#include "osal/osal_net.h"
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <ifaddrs.h>

int osal_net_tcp_connect(const char *host, const char *port,
                         uint32_t timeout_ms, osal_sock_t *out)
{
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
        return OSAL_ERR;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return OSAL_ERR;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int cr = connect(fd, res->ai_addr, res->ai_addrlen);
    if (cr != 0 && errno != EINPROGRESS) {
        close(fd);
        freeaddrinfo(res);
        return OSAL_ERR;
    }
    if (cr != 0) {
        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        int pr = poll(&pfd, 1, (int)timeout_ms);
        int soerr = 0;
        socklen_t slen = sizeof(soerr);
        if (pr <= 0 ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &slen) != 0 ||
            soerr != 0) {
            close(fd);
            freeaddrinfo(res);
            return OSAL_ERR;
        }
    }
    freeaddrinfo(res);

    fcntl(fd, F_SETFL, flags);   /* 转回阻塞 */
    *out = fd;
    return OSAL_OK;
}

int osal_net_send(osal_sock_t fd, const void *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, (const char *)buf + off, len - off, 0);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        return OSAL_ERR;
    }
    return OSAL_OK;
}

int osal_net_recv(osal_sock_t fd, void *buf, size_t len)
{
    ssize_t n;
    do {
        n = recv(fd, buf, len, 0);
    } while (n < 0 && errno == EINTR);
    if (n > 0)
        return (int)n;
    if (n == 0)
        return 0;    /* 对端关闭 */
    return OSAL_ERR;
}

int osal_net_poll(osal_sock_t fd, int want_read, int want_write, uint32_t timeout_ms)
{
    struct pollfd pfd = {
        .fd = fd,
        .events = (short)((want_read ? POLLIN : 0) | (want_write ? POLLOUT : 0)),
    };
    int pr;
    do {
        pr = poll(&pfd, 1, (int)timeout_ms);
    } while (pr < 0 && errno == EINTR);
    if (pr == 0)
        return 0;
    if (pr < 0)
        return OSAL_ERR;
    return 1;
}

void osal_net_close(osal_sock_t fd)
{
    if (fd != OSAL_SOCK_INVALID)
        close(fd);
}

int osal_net_get_mac(uint8_t mac[6])
{
    struct ifaddrs *ifap;
    if (getifaddrs(&ifap) != 0)
        return OSAL_ERR;
    int found = OSAL_ERR;
    /* wlan0 优先，其次任意非 lo 的 AF_PACKET 网卡 */
    for (int pass = 0; pass < 2 && found != OSAL_OK; pass++) {
        for (struct ifaddrs *it = ifap; it; it = it->ifa_next) {
            if (!it->ifa_addr || it->ifa_addr->sa_family != AF_PACKET)
                continue;
            struct sockaddr_ll *sl = (struct sockaddr_ll *)it->ifa_addr;
            if (sl->sll_halen != 6)
                continue;
            if (pass == 0 && strcmp(it->ifa_name, "wlan0") != 0)
                continue;
            if (pass == 1 && strcmp(it->ifa_name, "lo") == 0)
                continue;
            memcpy(mac, sl->sll_addr, 6);
            found = OSAL_OK;
            break;
        }
    }
    freeifaddrs(ifap);
    return found;
}

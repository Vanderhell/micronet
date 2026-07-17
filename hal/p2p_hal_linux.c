#if !defined(_WIN32)
#if defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE < 200112L)
#undef _POSIX_C_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#if defined(_XOPEN_SOURCE) && (_XOPEN_SOURCE < 600)
#undef _XOPEN_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#endif

#include "../src/transport/p2p_hal.h"

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

static int p2p_hal_wsa_ready = 0;

static int p2p_hal_make_nonblocking(SOCKET fd)
{
    u_long enabled = 1;
    return ioctlsocket(fd, FIONBIO, &enabled) == 0 ? 0 : -1;
}

static int p2p_hal_sock_open(uint16_t port)
{
    SOCKET fd;
    BOOL yes = TRUE;
    struct sockaddr_in addr;
    DWORD bytes_returned = 0;
    BOOL new_behavior = FALSE;

    if (!p2p_hal_wsa_ready) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return -1;
        }
        p2p_hal_wsa_ready = 1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == INVALID_SOCKET) {
        return -1;
    }

    (void)setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof(yes));
    (void)WSAIoctl(fd,
                   SIO_UDP_CONNRESET,
                   &new_behavior,
                   sizeof(new_behavior),
                   NULL,
                   0,
                   &bytes_returned,
                   NULL,
                   NULL);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        p2p_hal_make_nonblocking(fd) != 0) {
        closesocket(fd);
        return -1;
    }

    return (int)fd;
}

static void p2p_hal_sock_close(int fd)
{
    if (fd >= 0) {
        closesocket((SOCKET)fd);
    }
}

static int p2p_hal_sock_send(int fd,
                             const uint8_t *ip,
                             uint16_t port,
                             const uint8_t *data,
                             size_t len)
{
    struct sockaddr_in addr;

    if (ip == NULL || data == NULL) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, ip, 4U);

    return (int)sendto((SOCKET)fd,
                       (const char *)data,
                       (int)len,
                       0,
                       (const struct sockaddr *)&addr,
                       sizeof(addr));
}

static int p2p_hal_sock_recv(int fd,
                             uint8_t *ip,
                             uint16_t *port,
                             uint8_t *buf,
                             size_t buf_len)
{
    struct sockaddr_in addr;
    int addr_len = (int)sizeof(addr);
    int recv_len;

    recv_len = (int)recvfrom((SOCKET)fd,
                             (char *)buf,
                             (int)buf_len,
                             0,
                             (struct sockaddr *)&addr,
                             &addr_len);
    if (recv_len == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAECONNRESET) {
            return 0;
        }
        return -1;
    }

    if (ip != NULL) {
        memcpy(ip, &addr.sin_addr, 4U);
    }
    if (port != NULL) {
        *port = ntohs(addr.sin_port);
    }

    return recv_len;
}

static uint32_t p2p_hal_now_ms(void)
{
    return (uint32_t)GetTickCount64();
}

static bool p2p_hal_free_heap(size_t *out_bytes)
{
    MEMORYSTATUSEX ms;

    if (out_bytes == NULL) {
        return false;
    }

    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) {
        return false;
    }

    *out_bytes = (size_t)ms.ullAvailPhys;
    return true;
}

#else

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int p2p_hal_make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int p2p_hal_sock_open(uint16_t port)
{
    int fd;
    int yes = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    (void)setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        p2p_hal_make_nonblocking(fd) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void p2p_hal_sock_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

static int p2p_hal_sock_send(int fd,
                             const uint8_t *ip,
                             uint16_t port,
                             const uint8_t *data,
                             size_t len)
{
    struct sockaddr_in addr;

    if (ip == NULL || data == NULL) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr, ip, 4U);

    return (int)sendto(fd, data, len, 0, (const struct sockaddr *)&addr, sizeof(addr));
}

static int p2p_hal_sock_recv(int fd,
                             uint8_t *ip,
                             uint16_t *port,
                             uint8_t *buf,
                             size_t buf_len)
{
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int recv_len;

    recv_len = (int)recvfrom(fd, buf, buf_len, 0, (struct sockaddr *)&addr, &addr_len);
    if (recv_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }

    if (ip != NULL) {
        memcpy(ip, &addr.sin_addr.s_addr, 4U);
    }
    if (port != NULL) {
        *port = ntohs(addr.sin_port);
    }

    return recv_len;
}

static uint32_t p2p_hal_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL));
}

static bool p2p_hal_free_heap(size_t *out_bytes)
{
    (void)out_bytes;
    return false;
}

#endif

static const p2p_hal_t p2p_hal_instance = {
    p2p_hal_sock_open,
    p2p_hal_sock_close,
    p2p_hal_sock_send,
    p2p_hal_sock_recv,
    p2p_hal_now_ms,
    p2p_hal_free_heap
};

const p2p_hal_t *p2p_hal_default(void)
{
    return &p2p_hal_instance;
}

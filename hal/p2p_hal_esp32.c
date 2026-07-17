#include "../src/transport/p2p_hal.h"

#if defined(ESP_PLATFORM)

#include <errno.h>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_heap_caps.h"

static int p2p_hal_make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int p2p_hal_esp_sock_open(uint16_t port)
{
    int fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd < 0) {
        return -1;
    }

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

static void p2p_hal_esp_sock_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

static int p2p_hal_esp_sock_send(int fd,
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

static int p2p_hal_esp_sock_recv(int fd,
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

static uint32_t p2p_hal_esp_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool p2p_hal_esp_free_heap(size_t *out_bytes)
{
    if (out_bytes == NULL) {
        return false;
    }
    *out_bytes = (size_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    return true;
}

static const p2p_hal_t p2p_hal_esp32 = {
    p2p_hal_esp_sock_open,
    p2p_hal_esp_sock_close,
    p2p_hal_esp_sock_send,
    p2p_hal_esp_sock_recv,
    p2p_hal_esp_now_ms,
    p2p_hal_esp_free_heap
};

const p2p_hal_t *p2p_hal_default(void)
{
    return &p2p_hal_esp32;
}

const p2p_hal_t *p2p_hal_esp32_default(void)
{
    return &p2p_hal_esp32;
}

#endif

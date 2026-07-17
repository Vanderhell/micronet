#ifndef P2P_HAL_H
#define P2P_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int (*sock_open)(uint16_t port);
    void (*sock_close)(int fd);
    int (*sock_send)(int fd, const uint8_t *ip, uint16_t port,
                     const uint8_t *data, size_t len);
    int (*sock_recv)(int fd, uint8_t *ip, uint16_t *port,
                     uint8_t *buf, size_t buf_len);
    uint32_t (*now_ms)(void);
    bool (*free_heap)(size_t *out_bytes);
} p2p_hal_t;

const p2p_hal_t *p2p_hal_default(void);
const p2p_hal_t *p2p_hal_esp32_default(void);

#endif

#ifndef P2P_TRANSPORT_H
#define P2P_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p2p_hal.h"

#ifndef P2P_MAX_PACKET_SIZE
#define P2P_MAX_PACKET_SIZE 512U
#endif

#define P2P_TRANSPORT_HEADER_SIZE 8U
#define P2P_TRANSPORT_MAGIC_0 ((uint8_t)'P')
#define P2P_TRANSPORT_MAGIC_1 ((uint8_t)'2')
#define P2P_TRANSPORT_VERSION 0x01U

enum {
    P2P_PACKET_FLAG_ACK = 1u << 0,
    P2P_PACKET_FLAG_HEARTBEAT = 1u << 1,
    P2P_PACKET_FLAG_COMPRESSED = 1u << 2
};

typedef enum {
    P2P_OK = 0,
    P2P_ERR_SOCK = -1,
    P2P_ERR_STUN = -2,
    P2P_ERR_TIMEOUT = -3,
    P2P_ERR_RETRY = -4,
    P2P_ERR_BUF_FULL = -5,
    P2P_ERR_BAD_PACKET = -6,
    P2P_ERR_INVALID_ARG = -7,
} p2p_err_t;

typedef struct {
    const char *stun_host;
    uint16_t stun_port;
    uint16_t local_port;
    uint32_t heartbeat_ms;
    uint32_t timeout_ms;
    uint8_t retry_count;
    uint32_t retry_delay_ms;
    size_t rx_buf_size;
    size_t tx_buf_size;
} p2p_transport_config_t;

typedef struct {
    uint8_t data[P2P_MAX_PACKET_SIZE];
    size_t len;
    uint32_t timestamp;
    uint8_t remote_ip[4];
    uint16_t remote_port;
    uint8_t flags;
    uint16_t seq;
} p2p_packet_t;

typedef struct {
    uint8_t *storage;
    size_t element_size;
    size_t capacity;
    size_t count;
} micoring_t;

#ifndef P2P_MICROTIMER_T_DEFINED
#define P2P_MICROTIMER_T_DEFINED
typedef struct {
    uint32_t interval_ms;
    uint32_t last_ms;
    bool armed;
} microtimer_t;
#endif

#ifndef P2P_MICRORES_T_DEFINED
#define P2P_MICRORES_T_DEFINED
typedef struct {
    uint8_t max_retries;
    uint32_t retry_delay_ms;
} microres_t;
#endif

typedef struct {
    uint16_t seq;
    uint8_t ip[4];
    uint16_t port;
    uint8_t data[P2P_TRANSPORT_HEADER_SIZE + P2P_MAX_PACKET_SIZE];
    size_t len;
    uint8_t retries_done;
    uint32_t last_send_ms;
    bool in_use;
} p2p_transport_retry_entry_t;

typedef struct {
    int sock_fd;
    uint8_t external_ip[4];
    uint16_t external_port;
    bool stun_resolved;
    micoring_t rx_ring;
    micoring_t tx_ring;
    microtimer_t heartbeat_timer;
    microtimer_t timeout_timer;
    microres_t retry_ctx;

    const p2p_hal_t *hal;
    p2p_transport_config_t config;
    uint16_t next_seq;
    uint32_t last_activity_ms;
    bool timeout_latched;
    uint8_t last_peer_ip[4];
    uint16_t last_peer_port;
    bool last_peer_valid;
} p2p_transport_t;

p2p_err_t p2p_transport_init(p2p_transport_t *ctx, const p2p_transport_config_t *cfg);
p2p_err_t p2p_transport_stun_resolve(p2p_transport_t *ctx);
p2p_err_t p2p_transport_get_external_addr(p2p_transport_t *ctx, uint8_t ip[4], uint16_t *port);
p2p_err_t p2p_transport_send(p2p_transport_t *ctx, const uint8_t ip[4], uint16_t port,
                             const uint8_t *data, size_t len);
p2p_err_t p2p_transport_recv(p2p_transport_t *ctx, p2p_packet_t *out_packet);
p2p_err_t p2p_transport_tick(p2p_transport_t *ctx);
void p2p_transport_deinit(p2p_transport_t *ctx);

#endif

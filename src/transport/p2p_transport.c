#include "p2p_transport.h"

#include <stdlib.h>
#include <string.h>

static size_t p2p_transport_slot_count(size_t requested_bytes, size_t element_size)
{
    size_t slots;

    if (element_size == 0U) {
        return 0U;
    }

    slots = requested_bytes / element_size;
    return slots == 0U ? 1U : slots;
}

static int p2p_transport_ring_init(micoring_t *ring, size_t element_size, size_t requested_bytes)
{
    size_t capacity;

    if (ring == NULL || element_size == 0U) {
        return 0;
    }

    capacity = p2p_transport_slot_count(requested_bytes, element_size);
    ring->storage = (uint8_t *)calloc(capacity, element_size);
    if (ring->storage == NULL) {
        return 0;
    }

    ring->element_size = element_size;
    ring->capacity = capacity;
    ring->count = 0U;
    return 1;
}

static void p2p_transport_ring_deinit(micoring_t *ring)
{
    if (ring == NULL) {
        return;
    }

    free(ring->storage);
    ring->storage = NULL;
    ring->element_size = 0U;
    ring->capacity = 0U;
    ring->count = 0U;
}

static void *p2p_transport_ring_at(micoring_t *ring, size_t index)
{
    if (ring == NULL || ring->storage == NULL || index >= ring->capacity) {
        return NULL;
    }

    return ring->storage + (index * ring->element_size);
}

static int p2p_transport_ring_push(micoring_t *ring, const void *item)
{
    void *dst;

    if (ring == NULL || item == NULL || ring->count >= ring->capacity) {
        return 0;
    }

    dst = p2p_transport_ring_at(ring, ring->count);
    if (dst == NULL) {
        return 0;
    }

    memcpy(dst, item, ring->element_size);
    ring->count++;
    return 1;
}

static int p2p_transport_ring_pop_front(micoring_t *ring, void *out_item)
{
    if (ring == NULL || ring->count == 0U) {
        return 0;
    }

    if (out_item != NULL) {
        memcpy(out_item, ring->storage, ring->element_size);
    }

    if (ring->count > 1U) {
        memmove(ring->storage,
                ring->storage + ring->element_size,
                (ring->count - 1U) * ring->element_size);
    }

    ring->count--;
    memset(ring->storage + (ring->count * ring->element_size), 0, ring->element_size);
    return 1;
}

static int p2p_transport_retry_remove(micoring_t *ring, uint16_t seq)
{
    size_t i;

    if (ring == NULL || ring->count == 0U) {
        return 0;
    }

    for (i = 0U; i < ring->count; ++i) {
        p2p_transport_retry_entry_t *entry =
            (p2p_transport_retry_entry_t *)p2p_transport_ring_at(ring, i);
        if (entry != NULL && entry->in_use && entry->seq == seq) {
            if ((i + 1U) < ring->count) {
                memmove(entry,
                        (uint8_t *)entry + ring->element_size,
                        (ring->count - i - 1U) * ring->element_size);
            }
            ring->count--;
            memset(ring->storage + (ring->count * ring->element_size), 0, ring->element_size);
            return 1;
        }
    }

    return 0;
}

static uint16_t p2p_transport_read_u16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

static void p2p_transport_write_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)((value >> 8) & 0xFFU);
    dst[1] = (uint8_t)(value & 0xFFU);
}

static int p2p_transport_encode_rle(const uint8_t *src, size_t src_len, uint8_t *dst, size_t *dst_len)
{
    size_t in_pos = 0U;
    size_t out_pos = 0U;

    if (src == NULL || dst == NULL || dst_len == NULL) {
        return 0;
    }

    while (in_pos < src_len) {
        uint8_t run = 1U;
        while ((in_pos + run) < src_len && run < 255U && src[in_pos] == src[in_pos + run]) {
            run++;
        }

        if ((out_pos + 2U) > P2P_MAX_PACKET_SIZE) {
            return 0;
        }

        dst[out_pos++] = run;
        dst[out_pos++] = src[in_pos];
        in_pos += run;
    }

    *dst_len = out_pos;
    return 1;
}

static int p2p_transport_decode_rle(const uint8_t *src, size_t src_len, uint8_t *dst, size_t *dst_len)
{
    size_t in_pos = 0U;
    size_t out_pos = 0U;

    if (src == NULL || dst == NULL || dst_len == NULL || (src_len % 2U) != 0U) {
        return 0;
    }

    while (in_pos < src_len) {
        uint8_t run = src[in_pos++];
        uint8_t value = src[in_pos++];

        if ((out_pos + run) > P2P_MAX_PACKET_SIZE) {
            return 0;
        }

        memset(dst + out_pos, value, run);
        out_pos += run;
    }

    *dst_len = out_pos;
    return 1;
}

static p2p_err_t p2p_transport_send_wire(p2p_transport_t *ctx,
                                         const uint8_t ip[4],
                                         uint16_t port,
                                         const uint8_t *wire,
                                         size_t wire_len)
{
    int sent;

    if (ctx == NULL || ip == NULL || wire == NULL || wire_len == 0U) {
        return P2P_ERR_INVALID_ARG;
    }

    sent = ctx->hal->sock_send(ctx->sock_fd, ip, port, wire, wire_len);
    if (sent < 0 || (size_t)sent != wire_len) {
        return P2P_ERR_SOCK;
    }

    memcpy(ctx->last_peer_ip, ip, sizeof(ctx->last_peer_ip));
    ctx->last_peer_port = port;
    ctx->last_peer_valid = true;
    ctx->last_activity_ms = ctx->hal->now_ms();
    ctx->timeout_timer.last_ms = ctx->last_activity_ms;
    return P2P_OK;
}

static p2p_err_t p2p_transport_send_ack(p2p_transport_t *ctx,
                                        const uint8_t ip[4],
                                        uint16_t port,
                                        uint16_t seq)
{
    uint8_t wire[P2P_TRANSPORT_HEADER_SIZE];

    memset(wire, 0, sizeof(wire));
    wire[0] = P2P_TRANSPORT_MAGIC_0;
    wire[1] = P2P_TRANSPORT_MAGIC_1;
    wire[2] = P2P_TRANSPORT_VERSION;
    wire[3] = P2P_PACKET_FLAG_ACK;
    p2p_transport_write_u16(&wire[4], seq);
    p2p_transport_write_u16(&wire[6], 0U);
    return p2p_transport_send_wire(ctx, ip, port, wire, sizeof(wire));
}

static p2p_err_t p2p_transport_queue_retry(p2p_transport_t *ctx,
                                           const uint8_t ip[4],
                                           uint16_t port,
                                           uint16_t seq,
                                           const uint8_t *wire,
                                           size_t wire_len,
                                           uint32_t now_ms)
{
    p2p_transport_retry_entry_t entry;

    if (ctx == NULL || ip == NULL || wire == NULL || wire_len > sizeof(entry.data)) {
        return P2P_ERR_INVALID_ARG;
    }

    if (ctx->retry_ctx.max_retries == 0U) {
        return P2P_OK;
    }

    memset(&entry, 0, sizeof(entry));
    entry.seq = seq;
    memcpy(entry.ip, ip, sizeof(entry.ip));
    entry.port = port;
    memcpy(entry.data, wire, wire_len);
    entry.len = wire_len;
    entry.last_send_ms = now_ms;
    entry.in_use = true;

    if (!p2p_transport_ring_push(&ctx->tx_ring, &entry)) {
        return P2P_ERR_BUF_FULL;
    }

    return P2P_OK;
}

static p2p_err_t p2p_transport_handle_wire(p2p_transport_t *ctx,
                                           const uint8_t *wire,
                                           size_t wire_len,
                                           const uint8_t remote_ip[4],
                                           uint16_t remote_port,
                                           int *queued_user_packet)
{
    p2p_packet_t packet;
    uint16_t payload_len;
    uint16_t seq;
    uint8_t flags;
    size_t decoded_len = 0U;

    if (ctx == NULL || wire == NULL || remote_ip == NULL || queued_user_packet == NULL) {
        return P2P_ERR_INVALID_ARG;
    }

    if (wire_len < P2P_TRANSPORT_HEADER_SIZE) {
        return P2P_ERR_BAD_PACKET;
    }

    if (wire[0] != P2P_TRANSPORT_MAGIC_0 || wire[1] != P2P_TRANSPORT_MAGIC_1 ||
        wire[2] != P2P_TRANSPORT_VERSION) {
        return P2P_ERR_BAD_PACKET;
    }

    flags = wire[3];
    seq = p2p_transport_read_u16(&wire[4]);
    payload_len = p2p_transport_read_u16(&wire[6]);

    if ((size_t)payload_len != (wire_len - P2P_TRANSPORT_HEADER_SIZE) ||
        payload_len > P2P_MAX_PACKET_SIZE) {
        return P2P_ERR_BAD_PACKET;
    }

    ctx->last_activity_ms = ctx->hal->now_ms();
    ctx->timeout_timer.last_ms = ctx->last_activity_ms;
    memcpy(ctx->last_peer_ip, remote_ip, sizeof(ctx->last_peer_ip));
    ctx->last_peer_port = remote_port;
    ctx->last_peer_valid = true;

    if ((flags & P2P_PACKET_FLAG_ACK) != 0U) {
        p2p_transport_retry_remove(&ctx->tx_ring, seq);
        return P2P_OK;
    }

    memset(&packet, 0, sizeof(packet));
    if (payload_len > 0U) {
        if ((flags & P2P_PACKET_FLAG_COMPRESSED) != 0U) {
            if (!p2p_transport_decode_rle(wire + P2P_TRANSPORT_HEADER_SIZE,
                                          payload_len,
                                          packet.data,
                                          &decoded_len)) {
                return P2P_ERR_BAD_PACKET;
            }
        } else {
            memcpy(packet.data, wire + P2P_TRANSPORT_HEADER_SIZE, payload_len);
            decoded_len = payload_len;
        }
    }

    packet.len = decoded_len;
    packet.timestamp = ctx->last_activity_ms;
    memcpy(packet.remote_ip, remote_ip, sizeof(packet.remote_ip));
    packet.remote_port = remote_port;
    packet.flags = flags;
    packet.seq = seq;

    if (p2p_transport_send_ack(ctx, remote_ip, remote_port, seq) != P2P_OK) {
        return P2P_ERR_SOCK;
    }

    if ((flags & P2P_PACKET_FLAG_HEARTBEAT) != 0U) {
        return P2P_OK;
    }

    if (!p2p_transport_ring_push(&ctx->rx_ring, &packet)) {
        return P2P_ERR_BUF_FULL;
    }

    *queued_user_packet = 1;
    return P2P_OK;
}

static p2p_err_t p2p_transport_poll_socket(p2p_transport_t *ctx, int *queued_user_packet)
{
    uint8_t remote_ip[4];
    uint16_t remote_port = 0U;
    uint8_t wire[P2P_TRANSPORT_HEADER_SIZE + P2P_MAX_PACKET_SIZE];
    int recv_len;
    p2p_err_t last_err = P2P_OK;

    if (ctx == NULL || queued_user_packet == NULL) {
        return P2P_ERR_INVALID_ARG;
    }

    *queued_user_packet = 0;
    for (;;) {
        recv_len = ctx->hal->sock_recv(ctx->sock_fd,
                                       remote_ip,
                                       &remote_port,
                                       wire,
                                       sizeof(wire));
        if (recv_len == 0) {
            return last_err;
        }

        if (recv_len < 0) {
            return P2P_ERR_SOCK;
        }

        last_err = p2p_transport_handle_wire(ctx,
                                             wire,
                                             (size_t)recv_len,
                                             remote_ip,
                                             remote_port,
                                             queued_user_packet);
        if (last_err != P2P_OK) {
            return last_err;
        }
    }
}

static p2p_err_t p2p_transport_send_internal(p2p_transport_t *ctx,
                                             const uint8_t ip[4],
                                             uint16_t port,
                                             const uint8_t *data,
                                             size_t len,
                                             uint8_t extra_flags,
                                             int enqueue_retry)
{
    uint8_t compressed[P2P_MAX_PACKET_SIZE];
    uint8_t wire[P2P_TRANSPORT_HEADER_SIZE + P2P_MAX_PACKET_SIZE];
    const uint8_t *payload = data;
    size_t payload_len = len;
    size_t compressed_len = 0U;
    uint8_t flags = extra_flags;
    uint16_t seq;
    uint32_t now_ms;
    p2p_err_t err;

    if (ctx == NULL || ip == NULL || (len > 0U && data == NULL) || len > P2P_MAX_PACKET_SIZE) {
        return P2P_ERR_INVALID_ARG;
    }

    if (len > 0U && p2p_transport_encode_rle(data, len, compressed, &compressed_len) &&
        compressed_len < len) {
        payload = compressed;
        payload_len = compressed_len;
        flags |= P2P_PACKET_FLAG_COMPRESSED;
    }

    seq = ctx->next_seq++;
    if (ctx->next_seq == 0U) {
        ctx->next_seq = 1U;
    }

    wire[0] = P2P_TRANSPORT_MAGIC_0;
    wire[1] = P2P_TRANSPORT_MAGIC_1;
    wire[2] = P2P_TRANSPORT_VERSION;
    wire[3] = flags;
    p2p_transport_write_u16(&wire[4], seq);
    p2p_transport_write_u16(&wire[6], (uint16_t)payload_len);
    if (payload_len > 0U) {
        memcpy(wire + P2P_TRANSPORT_HEADER_SIZE, payload, payload_len);
    }

    now_ms = ctx->hal->now_ms();
    err = p2p_transport_send_wire(ctx, ip, port, wire, P2P_TRANSPORT_HEADER_SIZE + payload_len);
    if (err != P2P_OK) {
        return err;
    }

    if (enqueue_retry && (flags & P2P_PACKET_FLAG_ACK) == 0U) {
        err = p2p_transport_queue_retry(ctx,
                                        ip,
                                        port,
                                        seq,
                                        wire,
                                        P2P_TRANSPORT_HEADER_SIZE + payload_len,
                                        now_ms);
    }

    return err;
}

p2p_err_t p2p_transport_init(p2p_transport_t *ctx, const p2p_transport_config_t *cfg)
{
    if (ctx == NULL || cfg == NULL) {
        return P2P_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->sock_fd = -1;
    ctx->hal = cfg->hal;
    if (ctx->hal == NULL) {
        return P2P_ERR_SOCK;
    }

    ctx->config = *cfg;
    ctx->retry_ctx.max_retries = cfg->retry_count;
    ctx->retry_ctx.retry_delay_ms = cfg->retry_delay_ms;
    ctx->heartbeat_timer.interval_ms = cfg->heartbeat_ms;
    ctx->timeout_timer.interval_ms = cfg->timeout_ms;
    ctx->heartbeat_timer.armed = cfg->heartbeat_ms > 0U;
    ctx->timeout_timer.armed = cfg->timeout_ms > 0U;
    ctx->last_activity_ms = ctx->hal->now_ms();
    ctx->heartbeat_timer.last_ms = ctx->last_activity_ms;
    ctx->timeout_timer.last_ms = ctx->last_activity_ms;
    ctx->next_seq = 1U;
    ctx->last_stun_ms = ctx->last_activity_ms;
    ctx->stun_requested = false;
    ctx->stun_attempted = false;

    if (!p2p_transport_ring_init(&ctx->rx_ring, sizeof(p2p_packet_t), cfg->rx_buf_size)) {
        p2p_transport_deinit(ctx);
        return P2P_ERR_BUF_FULL;
    }

    if (!p2p_transport_ring_init(&ctx->tx_ring,
                                 sizeof(p2p_transport_retry_entry_t),
                                 cfg->tx_buf_size)) {
        p2p_transport_deinit(ctx);
        return P2P_ERR_BUF_FULL;
    }

    ctx->sock_fd = ctx->hal->sock_open(cfg->local_port);
    if (ctx->sock_fd < 0) {
        p2p_transport_deinit(ctx);
        return P2P_ERR_SOCK;
    }

    return P2P_OK;
}


void p2p_transport_request_stun_resolve(p2p_transport_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->stun_requested = true;
}

p2p_err_t p2p_transport_get_external_addr(p2p_transport_t *ctx, uint8_t ip[4], uint16_t *port)
{
    if (ctx == NULL || ip == NULL || port == NULL || !ctx->stun_resolved) {
        return P2P_ERR_INVALID_ARG;
    }

    memcpy(ip, ctx->external_ip, sizeof(ctx->external_ip));
    *port = ctx->external_port;
    return P2P_OK;
}

p2p_err_t p2p_transport_send(p2p_transport_t *ctx, const uint8_t ip[4], uint16_t port,
                             const uint8_t *data, size_t len)
{
    return p2p_transport_send_internal(ctx, ip, port, data, len, 0U, 1);
}

p2p_err_t p2p_transport_send_with_flags(p2p_transport_t *ctx, const uint8_t ip[4], uint16_t port,
                                        const uint8_t *data, size_t len, uint8_t flags)
{
    return p2p_transport_send_internal(ctx, ip, port, data, len, flags, 1);
}


p2p_err_t p2p_transport_recv(p2p_transport_t *ctx, p2p_packet_t *out_packet)
{
    int queued_user_packet = 0;
    p2p_err_t err;

    if (ctx == NULL || out_packet == NULL) {
        return P2P_ERR_INVALID_ARG;
    }

    memset(out_packet, 0, sizeof(*out_packet));

    if (p2p_transport_ring_pop_front(&ctx->rx_ring, out_packet)) {
        return P2P_OK;
    }

    err = p2p_transport_poll_socket(ctx, &queued_user_packet);
    if (err != P2P_OK) {
        return err;
    }

    if (queued_user_packet && p2p_transport_ring_pop_front(&ctx->rx_ring, out_packet)) {
        return P2P_OK;
    }

    return P2P_ERR_NO_PACKET;
}


p2p_err_t p2p_transport_tick(p2p_transport_t *ctx)
{
    uint32_t now_ms;
    size_t i = 0U;
    int queued_user_packet = 0;
    p2p_err_t err;

    if (ctx == NULL) {
        return P2P_ERR_INVALID_ARG;
    }

    err = p2p_transport_poll_socket(ctx, &queued_user_packet);
    if (err != P2P_OK) {
        return err;
    }

    now_ms = ctx->hal->now_ms();
    if (ctx->stun_requested) {
        ctx->stun_requested = false;
        ctx->stun_attempted = true;
        if (p2p_transport_stun_resolve(ctx) == P2P_OK) {
            ctx->last_stun_ms = now_ms;
        }
    }

    if (ctx->config.stun_resolve_on_init && !ctx->stun_attempted && !ctx->stun_resolved) {
        ctx->stun_attempted = true;
        if (p2p_transport_stun_resolve(ctx) == P2P_OK) {
            ctx->last_stun_ms = now_ms;
        }
    }

    if (ctx->config.stun_refresh_ms > 0U && ctx->stun_resolved &&
        (now_ms - ctx->last_stun_ms) >= ctx->config.stun_refresh_ms) {
        if (p2p_transport_stun_resolve(ctx) == P2P_OK) {
            ctx->last_stun_ms = now_ms;
        }
    }
    if (ctx->timeout_timer.armed &&
        (now_ms - ctx->timeout_timer.last_ms) >= ctx->timeout_timer.interval_ms) {
        ctx->timeout_latched = true;
        return P2P_ERR_TIMEOUT;
    }

    if (ctx->heartbeat_timer.armed && ctx->last_peer_valid &&
        (now_ms - ctx->heartbeat_timer.last_ms) >= ctx->heartbeat_timer.interval_ms) {
        err = p2p_transport_send_internal(ctx,
                                          ctx->last_peer_ip,
                                          ctx->last_peer_port,
                                          NULL,
                                          0U,
                                          P2P_PACKET_FLAG_HEARTBEAT,
                                          0);
        if (err != P2P_OK) {
            return err;
        }
        ctx->heartbeat_timer.last_ms = now_ms;
    }

    while (i < ctx->tx_ring.count) {
        p2p_transport_retry_entry_t *entry =
            (p2p_transport_retry_entry_t *)p2p_transport_ring_at(&ctx->tx_ring, i);
        if (entry == NULL || !entry->in_use) {
            ++i;
            continue;
        }

        if ((now_ms - entry->last_send_ms) < ctx->retry_ctx.retry_delay_ms) {
            ++i;
            continue;
        }

        if (entry->retries_done >= ctx->retry_ctx.max_retries) {
            p2p_transport_retry_remove(&ctx->tx_ring, entry->seq);
            return P2P_ERR_RETRY;
        }

        err = p2p_transport_send_wire(ctx, entry->ip, entry->port, entry->data, entry->len);
        if (err != P2P_OK) {
            return err;
        }

        entry->retries_done++;
        entry->last_send_ms = now_ms;
        ++i;
    }

    return P2P_OK;
}

void p2p_transport_deinit(p2p_transport_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->hal != NULL && ctx->sock_fd >= 0) {
        ctx->hal->sock_close(ctx->sock_fd);
    }

    p2p_transport_ring_deinit(&ctx->rx_ring);
    p2p_transport_ring_deinit(&ctx->tx_ring);
    memset(ctx, 0, sizeof(*ctx));
    ctx->sock_fd = -1;
}

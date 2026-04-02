#include "p2p_protocol.h"

#include <string.h>
#include <time.h>

enum {
    P2P_PROTO_STATE_BOOT = 0,
    P2P_PROTO_STATE_INIT = 1,
    P2P_PROTO_STATE_STUN = 2,
    P2P_PROTO_STATE_READY = 3,
    P2P_PROTO_STATE_ACTIVE = 4,
    P2P_PROTO_STATE_DISCONNECTING = 5,
    P2P_PROTO_STATE_ISOLATED = 6
};

static uint32_t p2p_protocol_now_ms(void)
{
    return (uint32_t)((uint64_t)time(NULL) * 1000ULL);
}

static int p2p_protocol_is_zero32(const uint8_t value[32])
{
    static const uint8_t zero32[32] = {0};
    return memcmp(value, zero32, 32U) == 0;
}

static p2p_pending_t *p2p_protocol_find_pending(p2p_protocol_t *ctx, uint16_t msg_id)
{
    uint8_t i;

    for (i = 0U; i < ctx->pending_count; ++i) {
        if (ctx->pending[i].msg_id == msg_id) {
            return &ctx->pending[i];
        }
    }

    return NULL;
}

static void p2p_protocol_remove_pending(p2p_protocol_t *ctx, uint8_t idx)
{
    if ((idx + 1U) < ctx->pending_count) {
        memmove(&ctx->pending[idx],
                &ctx->pending[idx + 1U],
                (size_t)(ctx->pending_count - idx - 1U) * sizeof(ctx->pending[0]));
    }
    ctx->pending_count--;
    memset(&ctx->pending[ctx->pending_count], 0, sizeof(ctx->pending[0]));
}

static p2p_proto_err_t p2p_protocol_send_encoded(p2p_protocol_t *ctx,
                                                 const p2p_message_t *msg,
                                                 const uint8_t dst_pubkey[32])
{
    uint8_t plain[1U + 2U + 4U + 32U + 32U + 16U + 2U + P2P_MAX_PAYLOAD];
    uint8_t cipher[1024U];
    size_t plain_len = sizeof(plain);
    size_t cipher_len = sizeof(cipher);

    if (p2p_protocol_serialize(msg, plain, &plain_len) != P2P_PROTO_OK) {
        return P2P_PROTO_ERR_SERIALIZE;
    }

    if (dst_pubkey != NULL && !p2p_protocol_is_zero32(dst_pubkey)) {
        if (p2p_security_encrypt(ctx->security, dst_pubkey, plain, plain_len, cipher, &cipher_len) != P2P_SEC_OK) {
            return P2P_PROTO_ERR_SERIALIZE;
        }
    } else {
        memcpy(cipher, plain, plain_len);
        cipher_len = plain_len;
    }

    if (ctx->transport->last_peer_valid) {
        if (p2p_transport_send(ctx->transport,
                               ctx->transport->last_peer_ip,
                               ctx->transport->last_peer_port,
                               cipher,
                               cipher_len) != P2P_OK) {
            return P2P_PROTO_ERR_SERIALIZE;
        }
    }

    return P2P_PROTO_OK;
}

p2p_proto_err_t p2p_protocol_init(p2p_protocol_t *ctx,
                                  const p2p_protocol_config_t *cfg,
                                  p2p_transport_t *transport,
                                  p2p_security_t *security,
                                  p2p_network_t *network,
                                  p2p_data_t *data)
{
    if (ctx == NULL || cfg == NULL || transport == NULL || security == NULL ||
        network == NULL || data == NULL) {
        return P2P_PROTO_ERR_NO_HANDLER;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->next_msg_id = 1U;
    ctx->retry.retry_interval_ms = cfg->retry_interval_ms;
    ctx->retry.retry_count = cfg->retry_count;
    ctx->log.level = cfg->log_level;
    ctx->transport = transport;
    ctx->security = security;
    ctx->network = network;
    ctx->data = data;
    ctx->custom_handler = cfg->custom_handler;
    memcpy(ctx->self_node_id, network->self.node_id, 32U);
    ctx->fsm.state = P2P_PROTO_STATE_INIT;
    ctx->fsm.state = P2P_PROTO_STATE_STUN;
    ctx->fsm.state = P2P_PROTO_STATE_READY;
    return P2P_PROTO_OK;
}

p2p_proto_err_t p2p_protocol_send(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    p2p_message_t copy;
    p2p_pending_t *pending;

    if (ctx == NULL || msg == NULL) {
        return P2P_PROTO_ERR_SERIALIZE;
    }

    copy = *msg;
    if (copy.msg_id == 0U) {
        copy.msg_id = ctx->next_msg_id++;
        if (ctx->next_msg_id == 0U) {
            ctx->next_msg_id = 1U;
        }
    }
    if (copy.timestamp == 0U) {
        copy.timestamp = p2p_protocol_now_ms();
    }
    if (memcmp(copy.src, "\0", 1U) == 0) {
        memcpy(copy.src, ctx->self_node_id, 32U);
    }

    if (ctx->pending_count >= P2P_MAX_PENDING) {
        return P2P_PROTO_ERR_PENDING;
    }

    if (p2p_protocol_send_encoded(ctx, &copy, copy.dst) != P2P_PROTO_OK) {
        return P2P_PROTO_ERR_SERIALIZE;
    }

    pending = &ctx->pending[ctx->pending_count++];
    memset(pending, 0, sizeof(*pending));
    pending->msg_id = copy.msg_id;
    pending->sent_at = p2p_protocol_now_ms();
    pending->msg = copy;
    return P2P_PROTO_OK;
}

p2p_proto_err_t p2p_protocol_broadcast(p2p_protocol_t *ctx,
                                       const uint8_t group_hash[16],
                                       const p2p_message_t *msg)
{
    p2p_message_t copy;

    if (ctx == NULL || group_hash == NULL || msg == NULL) {
        return P2P_PROTO_ERR_SERIALIZE;
    }

    copy = *msg;
    memcpy(copy.group_hash, group_hash, 16U);
    memset(copy.dst, 0, 32U);
    return p2p_protocol_send(ctx, &copy);
}

p2p_proto_err_t p2p_protocol_on_packet(p2p_protocol_t *ctx,
                                       const uint8_t *data, size_t len,
                                       const uint8_t src_ip[4], uint16_t src_port)
{
    uint8_t plain[1024U];
    size_t plain_len;
    p2p_message_t msg;
    uint8_t i;

    (void)src_ip;
    (void)src_port;
    if (ctx == NULL || data == NULL) {
        return P2P_PROTO_ERR_PARSE;
    }

    if (len > sizeof(plain)) {
        return P2P_PROTO_ERR_PARSE;
    }
    memcpy(plain, data, len);
    plain_len = len;

    if (p2p_protocol_parse(&msg, plain, plain_len) != P2P_PROTO_OK) {
        return P2P_PROTO_ERR_PARSE;
    }

    for (i = 0U; i < ctx->pending_count; ++i) {
        if (ctx->pending[i].msg_id == msg.msg_id &&
            (msg.type == P2P_MSG_HELLO_ACK || msg.type == P2P_MSG_DATA_RESPONSE || msg.type == P2P_MSG_QUERY_RESP)) {
            p2p_protocol_remove_pending(ctx, i);
            break;
        }
    }

    return p2p_protocol_dispatch(ctx, &msg);
}

p2p_proto_err_t p2p_protocol_register_handler(p2p_protocol_t *ctx,
                                              uint8_t msg_type,
                                              void (*handler)(const p2p_message_t *))
{
    if (ctx == NULL || handler == NULL || msg_type < P2P_MSG_CUSTOM) {
        return P2P_PROTO_ERR_NO_HANDLER;
    }

    ctx->custom_handlers[msg_type - P2P_MSG_CUSTOM] = handler;
    return P2P_PROTO_OK;
}

p2p_proto_err_t p2p_protocol_tick(p2p_protocol_t *ctx)
{
    uint32_t now_ms;
    uint8_t i = 0U;

    if (ctx == NULL) {
        return P2P_PROTO_ERR_NO_HANDLER;
    }

    (void)p2p_transport_tick(ctx->transport);
    (void)p2p_network_tick(ctx->network);
    (void)p2p_data_tick(ctx->data);

    now_ms = p2p_protocol_now_ms();
    while (i < ctx->pending_count) {
        p2p_pending_t *pending = &ctx->pending[i];
        if ((now_ms - pending->sent_at) < ctx->retry.retry_interval_ms) {
            ++i;
            continue;
        }

        if (pending->retry_count >= ctx->retry.retry_count) {
            p2p_protocol_remove_pending(ctx, i);
            return P2P_PROTO_ERR_RETRY;
        }

        if (p2p_protocol_send_encoded(ctx, &pending->msg, pending->msg.dst) != P2P_PROTO_OK) {
            return P2P_PROTO_ERR_RETRY;
        }

        pending->retry_count++;
        pending->sent_at = now_ms;
        ++i;
    }

    return P2P_PROTO_OK;
}

void p2p_protocol_deinit(p2p_protocol_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
}

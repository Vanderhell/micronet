#include "p2p_protocol.h"

#include <string.h>

enum {
    P2P_PROTO_STATE_BOOT = 0,
    P2P_PROTO_STATE_INIT = 1,
    P2P_PROTO_STATE_STUN = 2,
    P2P_PROTO_STATE_READY = 3,
    P2P_PROTO_STATE_ACTIVE = 4,
    P2P_PROTO_STATE_DISCONNECTING = 5,
    P2P_PROTO_STATE_ISOLATED = 6
};

static int p2p_protocol_is_zero32(const uint8_t value[32])
{
    static const uint8_t zero32[32] = {0};
    return memcmp(value, zero32, 32U) == 0;
}

static p2p_endpoint_t *p2p_protocol_endpoint_find_any(p2p_protocol_t *ctx, const uint8_t node_id[32])
{
    uint8_t i;

    if (ctx == NULL || node_id == NULL) {
        return NULL;
    }

    for (i = 0U; i < ctx->endpoint_count; ++i) {
        if (ctx->endpoints[i].valid && memcmp(ctx->endpoints[i].node_id, node_id, 32U) == 0) {
            return &ctx->endpoints[i];
        }
    }

    return NULL;
}

static p2p_endpoint_t *p2p_protocol_endpoint_find_authenticated(p2p_protocol_t *ctx, const uint8_t node_id[32])
{
    p2p_endpoint_t *ep = p2p_protocol_endpoint_find_any(ctx, node_id);
    if (ep == NULL) {
        return NULL;
    }
    if (ep->state != P2P_ENDPOINT_AUTHENTICATED) {
        return NULL;
    }
    return ep;
}

static p2p_endpoint_t *p2p_protocol_endpoint_get_or_add(p2p_protocol_t *ctx, const uint8_t node_id[32])
{
    p2p_endpoint_t *ep;

    ep = p2p_protocol_endpoint_find_any(ctx, node_id);
    if (ep != NULL) {
        return ep;
    }

    if (ctx == NULL || node_id == NULL || ctx->endpoint_count >= 32U) {
        return NULL;
    }

    ep = &ctx->endpoints[ctx->endpoint_count++];
    memset(ep, 0, sizeof(*ep));
    memcpy(ep->node_id, node_id, 32U);
    ep->state = P2P_ENDPOINT_PENDING;
    ep->valid = true;
    return ep;
}

static const uint8_t *p2p_protocol_endpoint_lookup_by_addr(p2p_protocol_t *ctx,
                                                           const uint8_t ip[4], uint16_t port)
{
    uint8_t i;

    if (ctx == NULL || ip == NULL) {
        return NULL;
    }

    for (i = 0U; i < ctx->endpoint_count; ++i) {
        p2p_endpoint_t *ep = &ctx->endpoints[i];
        if (!ep->valid || ep->state != P2P_ENDPOINT_AUTHENTICATED) {
            continue;
        }
        if (ep->local_port == port && memcmp(ep->local_ip, ip, 4U) == 0) {
            return ep->node_id;
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

static int p2p_protocol_msg_is_handshake(uint8_t msg_type)
{
    return msg_type == P2P_MSG_HELLO || msg_type == P2P_MSG_HELLO_ACK;
}

static p2p_proto_err_t p2p_protocol_send_encoded(p2p_protocol_t *ctx,
                                                 const p2p_message_t *msg,
                                                 const uint8_t dst_pubkey[32])
{
    uint8_t plain[MNET_PROTOCOL_SERIALIZED_HEADER_SIZE + P2P_MAX_PAYLOAD];
    uint8_t cipher[P2P_MAX_PACKET_SIZE];
    size_t plain_len = sizeof(plain);
    size_t cipher_len = sizeof(cipher);
    const uint8_t *dst_ip = NULL;
    uint16_t dst_port = 0U;
    uint8_t flags = 0U;

    if (ctx == NULL || msg == NULL) {
        return P2P_PROTO_ERR_SERIALIZE;
    }

    if (p2p_protocol_serialize(msg, plain, &plain_len) != P2P_PROTO_OK) {
        return P2P_PROTO_ERR_SERIALIZE;
    }

    if (dst_pubkey != NULL && !p2p_protocol_is_zero32(dst_pubkey)) {
        int is_handshake = p2p_protocol_msg_is_handshake(msg->type);
        p2p_endpoint_t *ep = p2p_protocol_endpoint_find_any(ctx, dst_pubkey);
        if (ep == NULL) {
            return P2P_PROTO_ERR_NO_ROUTE;
        }
        if (!is_handshake && ep->state != P2P_ENDPOINT_AUTHENTICATED) {
            return P2P_PROTO_ERR_NO_ROUTE;
        }
        dst_ip = ep->local_ip;
        dst_port = ep->local_port;

        if (!is_handshake) {
            if (!p2p_security_is_authenticated(ctx->security, dst_pubkey)) {
                return P2P_PROTO_ERR_NO_ROUTE;
            }
            if (p2p_security_encrypt(ctx->security, dst_pubkey, plain, plain_len, cipher, &cipher_len) != P2P_SEC_OK) {
                return P2P_PROTO_ERR_SERIALIZE;
            }
            flags |= P2P_PACKET_FLAG_ENCRYPTED;
        } else {
            memcpy(cipher, plain, plain_len);
            cipher_len = plain_len;
        }
    } else {
        if (!ctx->transport->last_peer_valid) {
            return P2P_PROTO_ERR_NO_ROUTE;
        }
        dst_ip = ctx->transport->last_peer_ip;
        dst_port = ctx->transport->last_peer_port;
        memcpy(cipher, plain, plain_len);
        cipher_len = plain_len;
    }

    if (p2p_transport_send_with_flags(ctx->transport, dst_ip, dst_port, cipher, cipher_len, flags) != P2P_OK) {
        return P2P_PROTO_ERR_SERIALIZE;
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
    ctx->now_ms = cfg->now_ms;
    ctx->transport = transport;
    ctx->security = security;
    ctx->network = network;
    ctx->data = data;
    ctx->custom_handler = cfg->custom_handler;
    ctx->data_response_handler = cfg->data_response_handler;
    memcpy(ctx->self_node_id, network->self.node_id, 32U);
    ctx->fsm.state = P2P_PROTO_STATE_INIT;
    ctx->fsm.state = P2P_PROTO_STATE_STUN;
    ctx->fsm.state = P2P_PROTO_STATE_READY;
    if (ctx->now_ms == NULL) {
        return P2P_PROTO_ERR_NO_HANDLER;
    }
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
        copy.timestamp = ctx->now_ms();
    }
    if (memcmp(copy.src, "\0", 1U) == 0) {
        memcpy(copy.src, ctx->self_node_id, 32U);
    }

    if (ctx->pending_count >= P2P_MAX_PENDING) {
        return P2P_PROTO_ERR_PENDING;
    }

    {
        p2p_proto_err_t send_err = p2p_protocol_send_encoded(ctx, &copy, copy.dst);
        if (send_err != P2P_PROTO_OK) {
            return send_err;
        }
    }

    if (copy.type == P2P_MSG_HELLO_ACK) {
        return P2P_PROTO_OK;
    }

    pending = &ctx->pending[ctx->pending_count++];
    memset(pending, 0, sizeof(*pending));
    pending->msg_id = copy.msg_id;
    pending->sent_at = ctx->now_ms();
    pending->msg = copy;
    return P2P_PROTO_OK;
}

p2p_proto_err_t p2p_protocol_broadcast(p2p_protocol_t *ctx,
                                       const uint8_t group_hash[16],
                                       const p2p_message_t *msg)
{
    p2p_message_t copy;
    uint8_t i;
    uint8_t sent_count = 0U;
    uint8_t members[P2P_MAX_MEMBERS][32];
    uint8_t member_count = 0U;

    if (ctx == NULL || group_hash == NULL || msg == NULL) {
        return P2P_PROTO_ERR_SERIALIZE;
    }

    if (p2p_network_group_members(ctx->network, group_hash, members, P2P_MAX_MEMBERS, &member_count) != P2P_NET_OK) {
        return P2P_PROTO_ERR_NO_ROUTE;
    }
    if (member_count == 0U) {
        return P2P_PROTO_ERR_NO_ROUTE;
    }

    copy = *msg;
    memcpy(copy.group_hash, group_hash, 16U);
    for (i = 0U; i < member_count; ++i) {
        p2p_endpoint_t *ep = p2p_protocol_endpoint_find_authenticated(ctx, members[i]);

        if (ep == NULL) {
            continue;
        }

        memcpy(copy.dst, ep->node_id, 32U);
        if (p2p_protocol_send_encoded(ctx, &copy, ep->node_id) == P2P_PROTO_OK) {
            sent_count++;
        }
    }

    return sent_count > 0U ? P2P_PROTO_OK : P2P_PROTO_ERR_NO_ROUTE;
}

p2p_proto_err_t p2p_protocol_on_packet(p2p_protocol_t *ctx,
                                       const uint8_t *data, size_t len,
                                       const uint8_t src_ip[4], uint16_t src_port,
                                       uint8_t transport_flags)
{
    uint8_t plain[MNET_PROTOCOL_SERIALIZED_HEADER_SIZE + P2P_MAX_PAYLOAD];
    size_t plain_len = sizeof(plain);
    p2p_message_t msg;
    uint8_t i;
    const uint8_t *peer_id = NULL;

    if (ctx == NULL || data == NULL) {
        return P2P_PROTO_ERR_PARSE;
    }

    if (len > sizeof(plain)) {
        return P2P_PROTO_ERR_PARSE;
    }

    if ((transport_flags & P2P_PACKET_FLAG_ENCRYPTED) != 0U) {
        peer_id = p2p_protocol_endpoint_lookup_by_addr(ctx, src_ip, src_port);
        if (peer_id == NULL) {
            return P2P_PROTO_ERR_PARSE;
        }
        if (p2p_security_decrypt(ctx->security, peer_id, data, len, plain, &plain_len) != P2P_SEC_OK) {
            return P2P_PROTO_ERR_PARSE;
        }
    } else {
        memcpy(plain, data, len);
        plain_len = len;
    }

    if (p2p_protocol_parse(&msg, plain, plain_len) != P2P_PROTO_OK) {
        return P2P_PROTO_ERR_PARSE;
    }

    if (peer_id != NULL) {
        if (memcmp(msg.src, peer_id, 32U) != 0) {
            return P2P_PROTO_ERR_PARSE;
        }
    }

    if (src_ip != NULL) {
        p2p_node_t peer_node;
        memset(&peer_node, 0, sizeof(peer_node));
        memcpy(peer_node.node_id, msg.src, 32U);
        if (src_ip != NULL) {
            memcpy(peer_node.external_ip, src_ip, 4U);
        }
        peer_node.external_port = src_port;
        peer_node.first_seen = ctx->now_ms();
        peer_node.last_seen = peer_node.first_seen;
        peer_node.is_online = true;
        peer_node.is_authorized = p2p_security_is_authenticated(ctx->security, msg.src);
        if (p2p_network_sync_apply(ctx->network, &peer_node) == P2P_NET_OK) {
            p2p_node_t synced;
            if (p2p_network_find_node(ctx->network, msg.src, &synced) == P2P_NET_OK && synced.is_authorized) {
                p2p_endpoint_t *ep = p2p_protocol_endpoint_find_any(ctx, msg.src);
                if (ep != NULL) {
                    memcpy(ep->local_ip, src_ip, 4U);
                    ep->local_port = src_port;
                    memcpy(ep->public_ip, src_ip, 4U);
                    ep->public_port = src_port;
                }
            }
        }
        if (peer_id != NULL) {
            /* Encrypted packets must already be mapped to an authenticated endpoint. */
            p2p_endpoint_t *ep = p2p_protocol_endpoint_find_authenticated(ctx, peer_id);
            if (ep != NULL) {
                memcpy(ep->local_ip, src_ip, 4U);
                ep->local_port = src_port;
                memcpy(ep->public_ip, src_ip, 4U);
                ep->public_port = src_port;
                ep->last_seen_ms = ctx->now_ms();
            }
        } else if (p2p_protocol_msg_is_handshake(msg.type)) {
            /* Plaintext handshake may only create/update a PENDING endpoint candidate. */
            p2p_endpoint_t *ep = p2p_protocol_endpoint_get_or_add(ctx, msg.src);
            if (ep != NULL) {
                if (ep->state != P2P_ENDPOINT_AUTHENTICATED) {
                    memcpy(ep->local_ip, src_ip, 4U);
                    ep->local_port = src_port;
                    memcpy(ep->public_ip, src_ip, 4U);
                    ep->public_port = src_port;
                    ep->state = P2P_ENDPOINT_PENDING;
                }
                ep->last_seen_ms = ctx->now_ms();
            }
        }
    }

    for (i = 0U; i < ctx->pending_count; ++i) {
        if (ctx->pending[i].msg_id == msg.msg_id &&
            (msg.type == P2P_MSG_HELLO_ACK || msg.type == P2P_MSG_DATA_RESPONSE || msg.type == P2P_MSG_QUERY_RESP)) {
            p2p_protocol_remove_pending(ctx, i);
            break;
        }
    }

    {
        p2p_proto_err_t derr = p2p_protocol_dispatch(ctx, &msg);
        if (derr == P2P_PROTO_OK && p2p_security_is_authenticated(ctx->security, msg.src)) {
            p2p_endpoint_t *ep = p2p_protocol_endpoint_find_any(ctx, msg.src);
            if (ep != NULL && ep->state != P2P_ENDPOINT_AUTHENTICATED) {
                ep->state = P2P_ENDPOINT_AUTHENTICATED;
            }
        }
        return derr;
    }
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
    uint8_t drained = 0U;
    p2p_packet_t pkt;

    if (ctx == NULL) {
        return P2P_PROTO_ERR_NO_HANDLER;
    }

    (void)p2p_transport_tick(ctx->transport);

    while (drained < 16U) {
        p2p_err_t rx_err = p2p_transport_recv(ctx->transport, &pkt);
        if (rx_err == P2P_ERR_NO_PACKET) {
            break;
        }
        if (rx_err != P2P_OK) {
            return P2P_PROTO_ERR_PARSE;
        }
        if (pkt.len > 0U) {
            {
                p2p_proto_err_t perr = p2p_protocol_on_packet(ctx, pkt.data, pkt.len, pkt.remote_ip, pkt.remote_port, pkt.flags);
                if (perr != P2P_PROTO_OK) {
                    return perr;
                }
            }
        }
        drained++;
    }

    if (ctx->transport->stun_resolved) {
        uint8_t ip[4];
        uint16_t port;
        if (p2p_transport_get_external_addr(ctx->transport, ip, &port) == P2P_OK) {
            memcpy(ctx->network->self.external_ip, ip, 4U);
            ctx->network->self.external_port = port;
        }
    }

    (void)p2p_network_tick(ctx->network);
    (void)p2p_data_tick(ctx->data);

    now_ms = ctx->now_ms();
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

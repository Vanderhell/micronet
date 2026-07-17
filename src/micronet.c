#include "../include/micronet.h"

#include "micronet_internal.h"

#include <stdio.h>
#include <string.h>
mnet_context_t g_mnet;

static uint32_t mnet_now_ms(void)
{
    if (g_mnet.transport.hal != NULL && g_mnet.transport.hal->now_ms != NULL) {
        return g_mnet.transport.hal->now_ms();
    }
    return 0U;
}

mnet_context_t *mnet_internal_context(void)
{
    return &g_mnet;
}

static mnet_err_t mnet_map_network_err(p2p_net_err_t err)
{
    switch (err) {
        case P2P_NET_OK:
            return MNET_OK;
        case P2P_NET_ERR_NOT_FOUND:
            return MNET_ERR_NOT_FOUND;
        case P2P_NET_ERR_NODE_FULL:
        case P2P_NET_ERR_GROUP_FULL:
            return MNET_ERR_FULL;
        case P2P_NET_ERR_NOT_MEMBER:
        case P2P_NET_ERR_NO_INVITE:
            return MNET_ERR_ACCESS;
        default:
            return MNET_ERR_INTERNAL;
    }
}

static mnet_err_t mnet_map_data_err(p2p_data_err_t err)
{
    switch (err) {
        case P2P_DATA_OK:
            return MNET_OK;
        case P2P_DATA_ERR_NOT_FOUND:
            return MNET_ERR_NOT_FOUND;
        case P2P_DATA_ERR_ACCESS:
            return MNET_ERR_ACCESS;
        case P2P_DATA_ERR_FULL:
            return MNET_ERR_FULL;
        case P2P_DATA_ERR_OFFLINE:
            return MNET_ERR_OFFLINE;
        case P2P_DATA_ERR_TIMEOUT:
            return MNET_ERR_TIMEOUT;
        default:
            return MNET_ERR_INTERNAL;
    }
}

static mnet_err_t mnet_map_proto_err(p2p_proto_err_t err)
{
    switch (err) {
        case P2P_PROTO_OK:
            return MNET_OK;
        case P2P_PROTO_ERR_PENDING:
            return MNET_ERR_FULL;
        case P2P_PROTO_ERR_RETRY:
            return MNET_ERR_TIMEOUT;
        case P2P_PROTO_ERR_NO_ROUTE:
            return MNET_ERR_OFFLINE;
        case P2P_PROTO_ERR_NO_HANDLER:
            return MNET_ERR_NOT_FOUND;
        default:
            return MNET_ERR_INTERNAL;
    }
}

static void mnet_network_event_adapter(const microbus_event_t *event, void *user)
{
    const p2p_node_t *node;

    (void)user;
    if (event == NULL || event->payload == NULL) {
        return;
    }

    node = (const p2p_node_t *)event->payload;
    switch ((p2p_network_event_id_t)event->event_id) {
        case P2P_EVENT_NODE_ONLINE:
            if (g_mnet.cfg.on_node_online != NULL) {
                g_mnet.cfg.on_node_online(node->node_id);
            }
            break;
        case P2P_EVENT_NODE_OFFLINE:
            if (g_mnet.cfg.on_node_offline != NULL) {
                g_mnet.cfg.on_node_offline(node->node_id);
            }
            break;
        default:
            break;
    }
}

static void mnet_request_adapter(int err, const void *value, size_t len)
{
    void (*cb)(mnet_err_t, const void *, size_t) = g_mnet.request_cb;

    g_mnet.request_cb = NULL;
    g_mnet.request_inflight = false;
    if (cb != NULL) {
        cb(mnet_map_data_err((p2p_data_err_t)err), value, len);
    }
}

static void mnet_protocol_request_completion(p2p_proto_err_t status,
                                             const p2p_message_t *msg,
                                             void *user)
{
    mnet_context_t *ctx = (mnet_context_t *)user;
    uint8_t response_status;
    uint16_t value_len;
    const void *value;
    void (*cb)(mnet_err_t, const void *, size_t);

    if (ctx == NULL) {
        return;
    }

    cb = ctx->request_cb;
    ctx->request_cb = NULL;
    ctx->request_inflight = false;
    if (cb == NULL) {
        return;
    }

    if (status == P2P_PROTO_ERR_RETRY) {
        cb(MNET_ERR_TIMEOUT, NULL, 0U);
        return;
    }

    if (status != P2P_PROTO_OK || msg == NULL || msg->type != P2P_MSG_DATA_RESPONSE) {
        cb(MNET_ERR_PROTOCOL, NULL, 0U);
        return;
    }
    if (memcmp(msg->src, ctx->request_node_id, 32U) != 0) {
        cb(MNET_ERR_PROTOCOL, NULL, 0U);
        return;
    }
    if (msg->payload_len < 3U) {
        cb(MNET_ERR_PROTOCOL, NULL, 0U);
        return;
    }

    response_status = msg->payload[0];
    value_len = (uint16_t)(((uint16_t)msg->payload[1] << 8) | msg->payload[2]);
    if ((size_t)(3U + value_len) != msg->payload_len) {
        cb(MNET_ERR_PROTOCOL, NULL, 0U);
        return;
    }
    value = value_len > 0U ? (const void *)(msg->payload + 3U) : NULL;

    switch (response_status) {
        case 0:
            cb(MNET_OK, value, value_len);
            break;
        case 1:
            cb(MNET_ERR_NOT_FOUND, NULL, 0U);
            break;
        case 2:
            cb(MNET_ERR_ACCESS, NULL, 0U);
            break;
        default:
            cb(MNET_ERR_INTERNAL, NULL, 0U);
            break;
    }
}

static void mnet_list_vars_adapter(int err, const char **names, uint8_t count)
{
    void (*cb)(mnet_err_t, const char **, uint8_t) = g_mnet.list_vars_cb;

    g_mnet.list_vars_cb = NULL;
    if (cb != NULL) {
        cb(mnet_map_data_err((p2p_data_err_t)err), names, count);
    }
}

static void mnet_query_adapter(int err, const p2p_row_t *rows, uint8_t count)
{
    void (*cb)(mnet_err_t, const mnet_row_t *, uint8_t) = g_mnet.query_cb;

    g_mnet.query_cb = NULL;
    if (cb != NULL) {
        cb(mnet_map_data_err((p2p_data_err_t)err), (const mnet_row_t *)rows, count);
    }
}

static void mnet_metrics_adapter(int err, const p2p_metrics_t *metrics)
{
    void (*cb)(mnet_err_t, const mnet_metrics_t *) = g_mnet.metrics_cb;

    g_mnet.metrics_cb = NULL;
    if (cb != NULL) {
        cb(mnet_map_data_err((p2p_data_err_t)err), (const mnet_metrics_t *)metrics);
    }
}

static void mnet_protocol_custom_adapter(const p2p_message_t *msg)
{
    uint8_t idx;

    if (msg == NULL) {
        return;
    }

    if (g_mnet.cfg.on_custom_msg != NULL) {
        g_mnet.cfg.on_custom_msg((const mnet_message_t *)msg);
    }

    if (msg->type < P2P_MSG_CUSTOM) {
        return;
    }

    idx = (uint8_t)(msg->type - P2P_MSG_CUSTOM);
    if (g_mnet.custom_handlers[idx] != NULL) {
        g_mnet.custom_handlers[idx](msg->src, msg->payload, msg->payload_len);
    }
}

static mnet_err_t mnet_require_init(void)
{
    return g_mnet.initialized ? MNET_OK : MNET_ERR_NOT_INIT;
}

static p2p_node_t *mnet_find_peer_mut(const uint8_t node_id[32])
{
    uint8_t i;

    if (node_id == NULL) {
        return NULL;
    }

    if (memcmp(g_mnet.network.self.node_id, node_id, 32U) == 0) {
        return &g_mnet.network.self;
    }

    for (i = 0U; i < g_mnet.network.node_count; ++i) {
        if (memcmp(g_mnet.network.nodes[i].node_id, node_id, 32U) == 0) {
            return &g_mnet.network.nodes[i];
        }
    }

    return NULL;
}

static p2p_endpoint_t *mnet_find_endpoint_mut(const uint8_t node_id[32])
{
    uint8_t i;

    if (node_id == NULL) {
        return NULL;
    }

    for (i = 0U; i < g_mnet.protocol.endpoint_count; ++i) {
        if (g_mnet.protocol.endpoints[i].valid &&
            memcmp(g_mnet.protocol.endpoints[i].node_id, node_id, 32U) == 0) {
            return &g_mnet.protocol.endpoints[i];
        }
    }

    return NULL;
}

static p2p_endpoint_t *mnet_find_endpoint_by_addr(const uint8_t ip[4], uint16_t port)
{
    uint8_t i;

    if (ip == NULL || port == 0U) {
        return NULL;
    }

    for (i = 0U; i < g_mnet.protocol.endpoint_count; ++i) {
        if (!g_mnet.protocol.endpoints[i].valid) {
            continue;
        }
        if (memcmp(g_mnet.protocol.endpoints[i].local_ip, ip, 4U) == 0 &&
            g_mnet.protocol.endpoints[i].local_port == port) {
            return &g_mnet.protocol.endpoints[i];
        }
        if (memcmp(g_mnet.protocol.endpoints[i].public_ip, ip, 4U) == 0 &&
            g_mnet.protocol.endpoints[i].public_port == port) {
            return &g_mnet.protocol.endpoints[i];
        }
    }

    return NULL;
}

static int mnet_peer_is_sendable(const uint8_t node_id[32])
{
    p2p_node_t *peer;
    p2p_endpoint_t *ep;

    if (node_id == NULL) {
        return 0;
    }

    peer = mnet_find_peer_mut(node_id);
    if (peer == NULL || !peer->is_authorized) {
        return 0;
    }

    ep = mnet_find_endpoint_mut(node_id);
    return ep != NULL && ep->valid && ep->state == P2P_ENDPOINT_AUTHENTICATED;
}

static p2p_endpoint_t *mnet_get_or_add_endpoint(const uint8_t node_id[32])
{
    p2p_endpoint_t *ep = mnet_find_endpoint_mut(node_id);

    if (ep != NULL) {
        return ep;
    }
    if (g_mnet.protocol.endpoint_count >= (uint8_t)(sizeof(g_mnet.protocol.endpoints) / sizeof(g_mnet.protocol.endpoints[0]))) {
        return NULL;
    }

    ep = &g_mnet.protocol.endpoints[g_mnet.protocol.endpoint_count++];
    memset(ep, 0, sizeof(*ep));
    memcpy(ep->node_id, node_id, 32U);
    ep->valid = true;
    ep->state = P2P_ENDPOINT_PENDING;
    return ep;
}

static mnet_err_t mnet_group_validate_pair(const uint8_t group_hash[16], const uint8_t group_key[16])
{
    uint8_t derived[16];

    if (group_hash == NULL || group_key == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (p2p_network_group_derive_hash(group_key, derived) != P2P_NET_OK) {
        return MNET_ERR_INTERNAL;
    }
    return memcmp(derived, group_hash, 16U) == 0 ? MNET_OK : MNET_ERR_ACCESS;
}

static mnet_err_t mnet_group_sync_local_join(const uint8_t group_hash[16], const uint8_t group_key[16])
{
    mnet_err_t err;

    err = mnet_group_validate_pair(group_hash, group_key);
    if (err != MNET_OK) {
        return err;
    }

    if (p2p_security_group_add(&g_mnet.security, group_hash, group_key) != P2P_SEC_OK) {
        return g_mnet.security.group_count >= P2P_MAX_GROUPS ? MNET_ERR_FULL : MNET_ERR_INTERNAL;
    }
    err = mnet_map_network_err(p2p_network_group_join(&g_mnet.network, group_hash, group_key));
    if (err != MNET_OK) {
        (void)p2p_security_group_remove(&g_mnet.security, group_hash);
        return err;
    }
    return MNET_OK;
}

static mnet_err_t mnet_group_sync_local_leave(const uint8_t group_hash[16])
{
    uint8_t group_key[16];
    mnet_err_t err;

    if (group_hash == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    err = p2p_security_group_find_key(&g_mnet.security, group_hash, group_key) == P2P_SEC_OK
              ? MNET_OK
              : MNET_ERR_NOT_FOUND;
    if (err != MNET_OK) {
        return err;
    }

    if (p2p_security_group_remove(&g_mnet.security, group_hash) != P2P_SEC_OK) {
        return MNET_ERR_INTERNAL;
    }
    if (p2p_network_group_leave(&g_mnet.network, group_hash) != P2P_NET_OK) {
        (void)p2p_security_group_add(&g_mnet.security, group_hash, group_key);
        return MNET_ERR_INTERNAL;
    }
    return MNET_OK;
}

static size_t mnet_group_pack_invite(uint8_t *out,
                                     size_t out_len,
                                     const uint8_t group_hash[16],
                                     const uint8_t group_key[16],
                                     const uint8_t inviter[32])
{
    if (out == NULL || out_len < (1U + 16U + 16U + 32U) || group_hash == NULL || group_key == NULL || inviter == NULL) {
        return 0U;
    }
    out[0] = 1U;
    memcpy(out + 1U, group_hash, 16U);
    memcpy(out + 17U, group_key, 16U);
    memcpy(out + 33U, inviter, 32U);
    return 65U;
}

static size_t mnet_group_pack_member(uint8_t *out,
                                     size_t out_len,
                                     const uint8_t group_hash[16],
                                     const uint8_t peer_id[32])
{
    if (out == NULL || out_len < (1U + 16U + 32U) || group_hash == NULL || peer_id == NULL) {
        return 0U;
    }
    out[0] = 1U;
    memcpy(out + 1U, group_hash, 16U);
    memcpy(out + 17U, peer_id, 32U);
    return 49U;
}

static mnet_err_t mnet_group_send_member_update(uint8_t msg_type,
                                                const uint8_t group_hash[16],
                                                const uint8_t peer_id[32],
                                                bool require_all)
{
    uint8_t members[P2P_MAX_MEMBERS][32];
    uint8_t count = 0U;
    uint8_t payload[1U + 16U + 32U];
    p2p_message_t msg;
    uint8_t i;
    bool sent_any = false;

    if (group_hash == NULL || peer_id == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (p2p_network_group_members(&g_mnet.network, group_hash, members, P2P_MAX_MEMBERS, &count) != P2P_NET_OK) {
        return MNET_ERR_NOT_FOUND;
    }
    if (count == 0U) {
        return MNET_ERR_NOT_FOUND;
    }
    if (mnet_group_pack_member(payload, sizeof(payload), group_hash, peer_id) == 0U) {
        return MNET_ERR_INTERNAL;
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = msg_type;
    memcpy(msg.group_hash, group_hash, 16U);
    memcpy(msg.payload, payload, sizeof(payload));
    msg.payload_len = sizeof(payload);

    for (i = 0U; i < count; ++i) {
        p2p_node_t *peer;

        if (memcmp(members[i], g_mnet.security.node_pubkey, 32U) == 0) {
            continue;
        }
        peer = mnet_find_peer_mut(members[i]);
        if (peer == NULL || !peer->is_online || !peer->is_authorized || !mnet_peer_is_sendable(members[i])) {
            if (require_all) {
                return MNET_ERR_OFFLINE;
            }
            continue;
        }

        memcpy(msg.dst, members[i], 32U);
        if (p2p_protocol_send(&g_mnet.protocol, &msg) == P2P_PROTO_OK) {
            sent_any = true;
        } else if (require_all) {
            return MNET_ERR_OFFLINE;
        }
    }

    return sent_any ? MNET_OK : MNET_ERR_OFFLINE;
}

static void mnet_remove_endpoint(const uint8_t node_id[32])
{
    uint8_t i;

    for (i = 0U; i < g_mnet.protocol.endpoint_count; ++i) {
        if (g_mnet.protocol.endpoints[i].valid &&
            memcmp(g_mnet.protocol.endpoints[i].node_id, node_id, 32U) == 0) {
            if ((i + 1U) < g_mnet.protocol.endpoint_count) {
                memmove(&g_mnet.protocol.endpoints[i],
                        &g_mnet.protocol.endpoints[i + 1U],
                        (size_t)(g_mnet.protocol.endpoint_count - i - 1U) * sizeof(g_mnet.protocol.endpoints[0]));
            }
            memset(&g_mnet.protocol.endpoints[g_mnet.protocol.endpoint_count - 1U], 0, sizeof(g_mnet.protocol.endpoints[0]));
            g_mnet.protocol.endpoint_count--;
            return;
        }
    }
}

static void mnet_update_peer_endpoint(const p2p_node_t *peer)
{
    p2p_endpoint_t *ep;

    if (peer == NULL) {
        return;
    }

    ep = mnet_get_or_add_endpoint(peer->node_id);
    if (ep == NULL) {
        ep = mnet_find_endpoint_by_addr(peer->external_ip, peer->external_port);
        if (ep != NULL) {
            memcpy(ep->node_id, peer->node_id, 32U);
            ep->valid = true;
        }
    }
    if (ep == NULL) {
        return;
    }

    memcpy(ep->local_ip, peer->external_ip, 4U);
    ep->local_port = peer->external_port;
    memcpy(ep->public_ip, peer->external_ip, 4U);
    ep->public_port = peer->external_port;
    ep->last_seen_ms = peer->last_seen;
    ep->state = peer->is_authorized ? P2P_ENDPOINT_AUTHENTICATED : P2P_ENDPOINT_PENDING;
    ep->valid = true;
}

static void mnet_peer_info_from_node(const p2p_node_t *node, mnet_peer_info_t *out_peer)
{
    uint8_t i;

    if (node == NULL || out_peer == NULL) {
        return;
    }

    memset(out_peer, 0, sizeof(*out_peer));
    memcpy(out_peer->node_id, node->node_id, 32U);
    memcpy(out_peer->ip, node->external_ip, 4U);
    out_peer->port = node->external_port;
    out_peer->last_seen_ms = node->last_seen;
    out_peer->is_online = node->is_online;
    out_peer->is_authorized = node->is_authorized;
    out_peer->group_count = node->group_count;
    for (i = 0U; i < node->group_count && i < MNET_MAX_GROUPS; ++i) {
        memcpy(out_peer->groups[i], node->group_hashes[i], 16U);
    }
}

static bool mnet_peer_matches_group(const p2p_node_t *node, const uint8_t group_hash[16])
{
    uint8_t i;

    if (node == NULL || group_hash == NULL) {
        return false;
    }

    for (i = 0U; i < node->group_count; ++i) {
        if (memcmp(node->group_hashes[i], group_hash, 16U) == 0) {
            return true;
        }
    }

    return false;
}

static void mnet_make_synthetic_node_id(const uint8_t ip[4], uint16_t port, uint8_t out[32])
{
    uint64_t state;
    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
    char spec[32];
    uint8_t i;
    uint64_t hash = 1469598103934665603ULL;

    if (out == NULL) {
        return;
    }

    snprintf(spec,
             sizeof(spec),
             "%u.%u.%u.%u:%u",
             (unsigned)ip[0],
             (unsigned)ip[1],
             (unsigned)ip[2],
             (unsigned)ip[3],
             (unsigned)port);
    for (i = 0U; spec[i] != '\0'; ++i) {
        hash ^= (unsigned char)spec[i];
        hash *= 1099511628211ULL;
    }
    state = hash;
    a = state * 0x9E3779B97F4A7C15ULL;
    b = a ^ 0xBF58476D1CE4E5B9ULL;
    c = b ^ 0x94D049BB133111EBULL;
    d = c ^ 0xD6E8FEB86659FD93ULL;
    for (i = 0U; i < 8U; ++i) {
        out[i] = (uint8_t)(a >> (i * 8U));
        out[8U + i] = (uint8_t)(b >> (i * 8U));
        out[16U + i] = (uint8_t)(c >> (i * 8U));
        out[24U + i] = (uint8_t)(d >> (i * 8U));
    }
}

static void mnet_deliver_custom_local(uint8_t msg_type,
                                      const uint8_t src[32],
                                      const uint8_t *payload,
                                      size_t len,
                                      const uint8_t group_hash[16])
{
    uint8_t idx;
    mnet_message_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = msg_type;
    memcpy(msg.src, src, 32U);
    if (group_hash != NULL) {
        memcpy(msg.group_hash, group_hash, 16U);
    }
    if (payload != NULL && len > 0U) {
        memcpy(msg.payload, payload, len);
        msg.payload_len = len;
    }

    if (g_mnet.cfg.on_custom_msg != NULL) {
        g_mnet.cfg.on_custom_msg(&msg);
    }

    if (msg_type >= P2P_MSG_CUSTOM) {
        idx = (uint8_t)(msg_type - P2P_MSG_CUSTOM);
        if (idx < (uint8_t)(sizeof(g_mnet.custom_handlers) / sizeof(g_mnet.custom_handlers[0])) &&
            g_mnet.custom_handlers[idx] != NULL) {
            g_mnet.custom_handlers[idx](msg.src, msg.payload, msg.payload_len);
        }
    }
}

static size_t mnet_gossip_node_wire_size(void)
{
    return 32U + 4U + 2U + 32U + 4U + 4U + 1U + (MNET_MAX_GROUPS * 16U) + 1U + 1U + 1U + 4U;
}

static void mnet_write_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)((value >> 8) & 0xFFU);
    dst[1] = (uint8_t)(value & 0xFFU);
}

static void mnet_write_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)((value >> 24) & 0xFFU);
    dst[1] = (uint8_t)((value >> 16) & 0xFFU);
    dst[2] = (uint8_t)((value >> 8) & 0xFFU);
    dst[3] = (uint8_t)(value & 0xFFU);
}

static size_t mnet_build_gossip_payload(uint8_t *out, size_t out_len)
{
    size_t node_size;
    size_t needed;
    size_t offset;
    uint8_t i;
    p2p_node_t self;

    if (out == NULL) {
        return 0U;
    }

    node_size = mnet_gossip_node_wire_size();
    needed = 2U + node_size;
    if (out_len < needed) {
        return 0U;
    }

    memset(&self, 0, sizeof(self));
    memcpy(self.node_id, g_mnet.network.self.node_id, 32U);
    memcpy(self.external_ip, g_mnet.network.self.external_ip, 4U);
    self.external_port = g_mnet.cfg.local_port;
    memcpy(self.invited_by, g_mnet.network.self.invited_by, 32U);
    self.first_seen = g_mnet.network.self.first_seen != 0U ? g_mnet.network.self.first_seen : mnet_now_ms();
    self.last_seen = mnet_now_ms();
    self.group_count = g_mnet.network.self.group_count;
    for (i = 0U; i < self.group_count && i < MNET_MAX_GROUPS; ++i) {
        memcpy(self.group_hashes[i], g_mnet.network.self.group_hashes[i], 16U);
    }
    self.is_online = true;
    self.is_authorized = true;
    self.db_version = mnet_now_ms();

    out[0] = 1U;
    out[1] = 1U;
    offset = 2U;
    memcpy(out + offset, self.node_id, 32U);
    offset += 32U;
    memcpy(out + offset, self.external_ip, 4U);
    offset += 4U;
    mnet_write_u16(out + offset, self.external_port);
    offset += 2U;
    memcpy(out + offset, self.invited_by, 32U);
    offset += 32U;
    mnet_write_u32(out + offset, self.first_seen);
    offset += 4U;
    mnet_write_u32(out + offset, self.last_seen);
    offset += 4U;
    out[offset++] = self.group_count;
    memset(out + offset, 0, MNET_MAX_GROUPS * 16U);
    if (self.group_count > 0U) {
        memcpy(out + offset, self.group_hashes, (size_t)self.group_count * 16U);
    }
    offset += MNET_MAX_GROUPS * 16U;
    out[offset++] = self.is_online ? 1U : 0U;
    out[offset++] = 0U;
    out[offset++] = self.is_authorized ? 1U : 0U;
    mnet_write_u32(out + offset, self.db_version);
    return needed;
}

mnet_err_t mnet_init(const mnet_config_t *cfg)
{
    p2p_transport_config_t transport_cfg;
    p2p_security_config_t security_cfg;
    p2p_network_config_t network_cfg;
    p2p_data_config_t data_cfg;
    p2p_protocol_config_t protocol_cfg;
    uint8_t i;

    if (cfg == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (g_mnet.initialized) {
        return MNET_ERR_INTERNAL;
    }

    memset(&g_mnet, 0, sizeof(g_mnet));
    g_mnet.cfg = *cfg;
    if (g_mnet.cfg.network_mode > MNET_MODE_STUN_EXPERIMENTAL) {
        g_mnet.cfg.network_mode = MNET_MODE_LAN_ONLY;
    }
    if (cfg->max_nodes > P2P_MAX_NODES ||
        cfg->max_vars > P2P_MAX_VARS ||
        cfg->max_pending > P2P_MAX_PENDING ||
        cfg->group_count > P2P_MAX_GROUPS) {
        return MNET_ERR_INVALID_ARG;
    }

    memset(&transport_cfg, 0, sizeof(transport_cfg));
    if (cfg->network_mode == MNET_MODE_STUN_EXPERIMENTAL && cfg->stun_enabled) {
        transport_cfg.stun_host = cfg->stun_host;
        transport_cfg.stun_port = cfg->stun_port != 0U ? cfg->stun_port : 19302U;
        transport_cfg.stun_resolve_on_init = true;
    } else {
        transport_cfg.stun_host = NULL;
        transport_cfg.stun_port = 0U;
        transport_cfg.stun_resolve_on_init = false;
    }
    transport_cfg.local_port = cfg->local_port;
    transport_cfg.heartbeat_ms = cfg->heartbeat_ms != 0U ? cfg->heartbeat_ms : 5000U;
    transport_cfg.timeout_ms = cfg->offline_timeout_ms != 0U ? cfg->offline_timeout_ms : 15000U;
    transport_cfg.retry_count = cfg->retry_count != 0U ? cfg->retry_count : 3U;
    transport_cfg.retry_delay_ms = cfg->retry_interval_ms != 0U ? cfg->retry_interval_ms : 2000U;
    transport_cfg.rx_buf_size = sizeof(p2p_packet_t) * 8U;
    transport_cfg.tx_buf_size = sizeof(p2p_transport_retry_entry_t) * 8U;
    transport_cfg.stun_refresh_ms = 0U;
    transport_cfg.hal = p2p_hal_default();
    if (p2p_transport_init(&g_mnet.transport, &transport_cfg) != P2P_OK) {
        return MNET_ERR_TRANSPORT;
    }

    memset(&security_cfg, 0, sizeof(security_cfg));
    memcpy(security_cfg.node_privkey, cfg->node_privkey, sizeof(security_cfg.node_privkey));
    security_cfg.store_keys = false;
    security_cfg.now_ms = g_mnet.transport.hal->now_ms;
    if (p2p_security_init(&g_mnet.security, &security_cfg) != P2P_SEC_OK) {
        p2p_transport_deinit(&g_mnet.transport);
        return MNET_ERR_CRYPTO;
    }

    memset(&network_cfg, 0, sizeof(network_cfg));
    network_cfg.gossip_interval_ms = 30000U;
    network_cfg.sync_interval_ms = 60000U;
    network_cfg.offline_timeout_ms = cfg->offline_timeout_ms != 0U ? cfg->offline_timeout_ms : 15000U;
    network_cfg.max_nodes = cfg->max_nodes != 0U ? cfg->max_nodes : 16U;
    network_cfg.max_groups = P2P_MAX_GROUPS;
    network_cfg.now_ms = g_mnet.transport.hal->now_ms;
    if (p2p_network_init(&g_mnet.network, &network_cfg, g_mnet.security.node_pubkey) != P2P_NET_OK) {
        p2p_security_deinit(&g_mnet.security);
        p2p_transport_deinit(&g_mnet.transport);
        return MNET_ERR_INTERNAL;
    }
    g_mnet.network.event_publish = mnet_network_event_adapter;

    for (i = 0U; i < cfg->group_count && i < P2P_MAX_GROUPS; ++i) {
        uint8_t derived[16];
        if (p2p_network_group_derive_hash(cfg->groups[i].group_key, derived) != P2P_NET_OK ||
            memcmp(derived, cfg->groups[i].group_hash, 16U) != 0) {
            p2p_network_deinit(&g_mnet.network);
            p2p_security_deinit(&g_mnet.security);
            p2p_transport_deinit(&g_mnet.transport);
            return MNET_ERR_INVALID_ARG;
        }
        if (p2p_security_group_add(&g_mnet.security, cfg->groups[i].group_hash, cfg->groups[i].group_key) != P2P_SEC_OK ||
            p2p_network_group_join(&g_mnet.network, cfg->groups[i].group_hash, cfg->groups[i].group_key) != P2P_NET_OK) {
            uint8_t j;
            for (j = 0U; j < i; ++j) {
                (void)p2p_network_group_leave(&g_mnet.network, cfg->groups[j].group_hash);
                (void)p2p_security_group_remove(&g_mnet.security, cfg->groups[j].group_hash);
            }
            p2p_network_deinit(&g_mnet.network);
            p2p_security_deinit(&g_mnet.security);
            p2p_transport_deinit(&g_mnet.transport);
            return MNET_ERR_INTERNAL;
        }
    }

    memset(&data_cfg, 0, sizeof(data_cfg));
    data_cfg.max_vars = cfg->max_vars != 0U ? cfg->max_vars : 16U;
    data_cfg.max_subs = 8U;
    data_cfg.notify_min_interval_ms = 1000U;
    data_cfg.compress_data = true;
    data_cfg.spool_size = 16U;
    data_cfg.now_ms = g_mnet.transport.hal->now_ms;
    if (p2p_data_init(&g_mnet.data, &data_cfg) != P2P_DATA_OK) {
        p2p_network_deinit(&g_mnet.network);
        p2p_security_deinit(&g_mnet.security);
        p2p_transport_deinit(&g_mnet.transport);
        return MNET_ERR_INTERNAL;
    }

    memset(&protocol_cfg, 0, sizeof(protocol_cfg));
    protocol_cfg.max_pending = cfg->max_pending != 0U ? cfg->max_pending : 8U;
    protocol_cfg.retry_interval_ms = cfg->retry_interval_ms != 0U ? cfg->retry_interval_ms : 2000U;
    protocol_cfg.retry_count = cfg->retry_count != 0U ? cfg->retry_count : 3U;
    protocol_cfg.log_level = cfg->log_level;
    protocol_cfg.custom_handler = mnet_protocol_custom_adapter;
    protocol_cfg.data_response_handler = NULL;
    protocol_cfg.now_ms = g_mnet.transport.hal->now_ms;
    if (p2p_protocol_init(&g_mnet.protocol,
                          &protocol_cfg,
                          &g_mnet.transport,
                          &g_mnet.security,
                          &g_mnet.network,
                          &g_mnet.data) != P2P_PROTO_OK) {
        p2p_data_deinit(&g_mnet.data);
        p2p_network_deinit(&g_mnet.network);
        p2p_security_deinit(&g_mnet.security);
        p2p_transport_deinit(&g_mnet.transport);
        return MNET_ERR_INTERNAL;
    }

    g_mnet.initialized = true;
    if (g_mnet.cfg.on_node_online != NULL) {
        g_mnet.cfg.on_node_online(g_mnet.security.node_pubkey);
    }
    return MNET_OK;
}

mnet_err_t mnet_tick(void)
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }

    {
        p2p_proto_err_t perr = p2p_protocol_tick(&g_mnet.protocol);
        if (perr == P2P_PROTO_ERR_RETRY) {
            return MNET_OK;
        }
        if (perr != P2P_PROTO_OK) {
            mnet_err_t err = mnet_map_proto_err(perr);
            if (err != MNET_OK) {
                return err;
            }
        }
    }

    return MNET_OK;
}

mnet_err_t mnet_get_node_id(uint8_t out_node_id[32])
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (out_node_id == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    memcpy(out_node_id, g_mnet.security.node_pubkey, 32U);
    return MNET_OK;
}

void mnet_deinit(void)
{
    uint8_t self_node_id[32];
    void (*on_node_offline)(const uint8_t node_id[32]);

    if (!g_mnet.initialized) {
        return;
    }

    memcpy(self_node_id, g_mnet.security.node_pubkey, sizeof(self_node_id));
    on_node_offline = g_mnet.cfg.on_node_offline;

    p2p_protocol_deinit(&g_mnet.protocol);
    p2p_data_deinit(&g_mnet.data);
    p2p_network_deinit(&g_mnet.network);
    p2p_security_deinit(&g_mnet.security);
    p2p_transport_deinit(&g_mnet.transport);
    memset(&g_mnet, 0, sizeof(g_mnet));

    if (on_node_offline != NULL) {
        on_node_offline(self_node_id);
    }
}

bool mnet_node_is_online(const uint8_t node_id[32])
{
    p2p_node_t node;

    if (mnet_require_init() != MNET_OK || node_id == NULL) {
        return false;
    }

    return p2p_network_find_node(&g_mnet.network, node_id, &node) == P2P_NET_OK && node.is_online;
}

mnet_err_t mnet_node_list_online(uint8_t out[][32], uint8_t capacity, uint8_t *count)
{
    uint8_t i;
    uint8_t written = 0U;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (out == NULL || count == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    for (i = 0U; i < g_mnet.network.node_count; ++i) {
        if (g_mnet.network.nodes[i].is_online) {
            if (written >= capacity) {
                return MNET_ERR_FULL;
            }
            memcpy(out[written++], g_mnet.network.nodes[i].node_id, 32U);
        }
    }
    *count = written;
    return MNET_OK;
}

mnet_err_t mnet_node_list_all(uint8_t out[][32], uint8_t capacity, uint8_t *count)
{
    uint8_t i;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (out == NULL || count == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    for (i = 0U; i < g_mnet.network.node_count; ++i) {
        if (i >= capacity) {
            return MNET_ERR_FULL;
        }
        memcpy(out[i], g_mnet.network.nodes[i].node_id, 32U);
    }
    *count = g_mnet.network.node_count;
    return MNET_OK;
}

mnet_err_t mnet_node_invited_by(const uint8_t node_id[32], uint8_t out_inviter[32])
{
    p2p_node_t node;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (node_id == NULL || out_inviter == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    if (p2p_network_find_node(&g_mnet.network, node_id, &node) != P2P_NET_OK) {
        return MNET_ERR_NOT_FOUND;
    }

    memcpy(out_inviter, node.invited_by, 32U);
    return MNET_OK;
}

mnet_err_t mnet_peer_add_ip(const uint8_t node_id[32], const uint8_t ip[4], uint16_t port)
{
    p2p_node_t node;
    p2p_node_t *peer;
    p2p_endpoint_t *ep;
    bool already_authenticated;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (node_id == NULL || ip == NULL || port == 0U) {
        return MNET_ERR_INVALID_ARG;
    }
    if (memcmp(node_id, g_mnet.security.node_pubkey, 32U) == 0) {
        return MNET_ERR_INVALID_ARG;
    }

    ep = mnet_find_endpoint_mut(node_id);
    already_authenticated = ep != NULL && ep->valid && ep->state == P2P_ENDPOINT_AUTHENTICATED;
    peer = mnet_find_peer_mut(node_id);
    if (peer == NULL) {
        memset(&node, 0, sizeof(node));
        memcpy(node.node_id, node_id, 32U);
        memcpy(node.external_ip, ip, 4U);
        node.external_port = port;
        node.first_seen = mnet_now_ms();
        node.last_seen = node.first_seen;
        node.is_online = false;
        node.is_authorized = false;
        if (p2p_network_add_node(&g_mnet.network, &node) != P2P_NET_OK) {
            return MNET_ERR_FULL;
        }
        peer = mnet_find_peer_mut(node_id);
    } else {
        if (!already_authenticated) {
            memcpy(peer->external_ip, ip, 4U);
            peer->external_port = port;
            peer->last_seen = mnet_now_ms();
            peer->is_online = false;
        }
    }

    if (peer == NULL) {
        return MNET_ERR_INTERNAL;
    }

    if (!already_authenticated) {
        mnet_update_peer_endpoint(peer);
        ep = mnet_find_endpoint_mut(node_id);
        if (ep != NULL && ep->valid && ep->state != P2P_ENDPOINT_AUTHENTICATED) {
            ep->state = P2P_ENDPOINT_PENDING;
            memcpy(ep->local_ip, ip, 4U);
            ep->local_port = port;
            memcpy(ep->public_ip, ip, 4U);
            ep->public_port = port;
        }
    }
    return MNET_OK;
}

mnet_err_t mnet_peer_authorize(const uint8_t node_id[32], bool authorized)
{
    p2p_node_t *peer;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (node_id == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    peer = mnet_find_peer_mut(node_id);
    if (peer == NULL) {
        return MNET_ERR_NOT_FOUND;
    }

    peer->is_authorized = authorized;
    if (!authorized) {
        p2p_endpoint_t *ep = mnet_find_endpoint_mut(node_id);
        if (ep != NULL && ep->valid && ep->state != P2P_ENDPOINT_AUTHENTICATED) {
            ep->state = P2P_ENDPOINT_PENDING;
        }
    }

    return MNET_OK;
}

mnet_err_t mnet_peer_connect(const uint8_t node_id[32])
{
    p2p_node_t *peer;
    p2p_endpoint_t *ep;
    p2p_message_t msg;
    uint8_t mac[P2P_HMAC_SIZE];

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (node_id == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    peer = mnet_find_peer_mut(node_id);
    if (peer == NULL) {
        return MNET_ERR_NOT_FOUND;
    }
    if (!peer->is_authorized) {
        return MNET_ERR_ACCESS;
    }

    ep = mnet_find_endpoint_mut(node_id);
    if (ep == NULL || !ep->valid) {
        return MNET_ERR_OFFLINE;
    }

    if (p2p_security_build_hello_mac(&g_mnet.security, node_id, mac) != P2P_SEC_OK) {
        return MNET_ERR_CRYPTO;
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_HELLO;
    memcpy(msg.dst, node_id, 32U);
    memcpy(msg.payload, mac, P2P_HMAC_SIZE);
    msg.payload_len = P2P_HMAC_SIZE;
    return mnet_map_proto_err(p2p_protocol_send(&g_mnet.protocol, &msg));
}

mnet_err_t mnet_peer_remove(const uint8_t node_id[32])
{
    p2p_node_t *peer;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (node_id == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (memcmp(node_id, g_mnet.security.node_pubkey, 32U) == 0) {
        return MNET_ERR_INVALID_ARG;
    }

    peer = mnet_find_peer_mut(node_id);
    if (peer == NULL) {
        return MNET_ERR_NOT_FOUND;
    }

    while (peer->group_count > 0U) {
        (void)p2p_network_peer_leave_group(&g_mnet.network, node_id, peer->group_hashes[0]);
    }

    (void)p2p_network_remove_node(&g_mnet.network, node_id);
    mnet_remove_endpoint(node_id);
    return MNET_OK;
}

mnet_err_t mnet_peer_list(mnet_peer_info_t *out_peers, uint8_t capacity, uint8_t *out_count)
{
    uint8_t i;
    uint8_t written = 0U;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (out_peers == NULL || out_count == NULL || capacity == 0U) {
        return MNET_ERR_INVALID_ARG;
    }

    for (i = 0U; i < g_mnet.network.node_count && written < capacity; ++i) {
        mnet_peer_info_from_node(&g_mnet.network.nodes[i], &out_peers[written++]);
    }

    *out_count = written;
    return MNET_OK;
}

mnet_err_t mnet_peer_join_group(const uint8_t node_id[32], const uint8_t group_hash[16])
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (node_id == NULL || group_hash == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    return mnet_map_network_err(p2p_network_peer_join_group(&g_mnet.network, node_id, group_hash));
}

mnet_err_t mnet_peer_leave_group(const uint8_t node_id[32], const uint8_t group_hash[16])
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (node_id == NULL || group_hash == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    return mnet_map_network_err(p2p_network_peer_leave_group(&g_mnet.network, node_id, group_hash));
}

mnet_err_t mnet_group_create(uint8_t out_group_hash[16], uint8_t out_group_key[16])
{
    uint8_t i;
    mnet_err_t err;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (out_group_hash == NULL || out_group_key == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    err = mnet_map_network_err(p2p_network_group_create(&g_mnet.network, out_group_hash));
    if (err != MNET_OK) {
        return err;
    }

    err = MNET_ERR_INTERNAL;
    for (i = 0U; i < g_mnet.network.group_count; ++i) {
        if (memcmp(g_mnet.network.groups[i].group_hash, out_group_hash, 16U) == 0) {
            memcpy(out_group_key, g_mnet.network.groups[i].group_key, 16U);
            err = p2p_security_group_add(&g_mnet.security, out_group_hash, out_group_key) == P2P_SEC_OK
                      ? MNET_OK
                      : (g_mnet.security.group_count >= P2P_MAX_GROUPS ? MNET_ERR_FULL : MNET_ERR_INTERNAL);
            if (err != MNET_OK) {
                (void)p2p_network_group_leave(&g_mnet.network, out_group_hash);
                (void)p2p_network_group_remove(&g_mnet.network, out_group_hash);
            }
            return err;
        }
    }

    return MNET_ERR_INTERNAL;
}

mnet_err_t mnet_group_invite(const uint8_t node_id[32], const uint8_t group_hash[16])
{
    uint8_t group_key[16];
    p2p_message_t msg;
    uint8_t payload[1U + 16U + 16U + 32U];
    size_t payload_len;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (node_id == NULL || group_hash == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (!mnet_group_is_member(g_mnet.security.node_pubkey, group_hash)) {
        return MNET_ERR_ACCESS;
    }
    if (mnet_peer_is_sendable(node_id) == 0) {
        return mnet_find_peer_mut(node_id) != NULL ? MNET_ERR_OFFLINE : MNET_ERR_NOT_FOUND;
    }
    if (p2p_security_group_find_key(&g_mnet.security, group_hash, group_key) != P2P_SEC_OK) {
        return MNET_ERR_NOT_FOUND;
    }
    payload_len = mnet_group_pack_invite(payload, sizeof(payload), group_hash, group_key, g_mnet.security.node_pubkey);
    if (payload_len == 0U) {
        return MNET_ERR_INTERNAL;
    }
    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_GROUP_INVITE;
    memcpy(msg.dst, node_id, 32U);
    memcpy(msg.group_hash, group_hash, 16U);
    memcpy(msg.payload, payload, payload_len);
    msg.payload_len = payload_len;
    return mnet_map_proto_err(p2p_protocol_send(&g_mnet.protocol, &msg));
}

mnet_err_t mnet_group_join(const uint8_t group_hash[16], const uint8_t group_key[16])
{
    mnet_err_t err;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (group_hash == NULL || group_key == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    err = mnet_group_sync_local_join(group_hash, group_key);
    return err;
}

mnet_err_t mnet_group_accept_invite(const uint8_t group_hash[16])
{
    p2p_group_invite_t invite;
    mnet_err_t err;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (group_hash == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (p2p_network_invite_find(&g_mnet.network, group_hash, &invite) != P2P_NET_OK) {
        if (mnet_group_is_member(g_mnet.security.node_pubkey, group_hash)) {
            return MNET_OK;
        }
        return MNET_ERR_NOT_FOUND;
    }
    if (mnet_find_peer_mut(invite.inviter) == NULL || !mnet_peer_is_sendable(invite.inviter)) {
        return MNET_ERR_OFFLINE;
    }

    err = mnet_group_sync_local_join(invite.group_hash, invite.group_key);
    if (err != MNET_OK && err != MNET_ERR_ACCESS) {
        return err;
    }
    if (err == MNET_ERR_ACCESS) {
        return err;
    }

    if (mnet_group_send_member_update(P2P_MSG_GROUP_JOIN, invite.group_hash, g_mnet.security.node_pubkey, false) != MNET_OK) {
        (void)mnet_group_sync_local_leave(invite.group_hash);
        return MNET_ERR_OFFLINE;
    }
    (void)p2p_network_invite_remove(&g_mnet.network, group_hash);
    return MNET_OK;
}

mnet_err_t mnet_group_reject_invite(const uint8_t group_hash[16])
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (group_hash == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (p2p_network_invite_remove(&g_mnet.network, group_hash) == P2P_NET_OK) {
        return MNET_OK;
    }
    return mnet_group_is_member(g_mnet.security.node_pubkey, group_hash) ? MNET_OK : MNET_ERR_NOT_FOUND;
}

mnet_err_t mnet_group_leave(const uint8_t group_hash[16])
{
    uint8_t members[P2P_MAX_MEMBERS][32];
    uint8_t count = 0U;
    uint8_t i;
    mnet_err_t err;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (group_hash == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (p2p_network_group_members(&g_mnet.network, group_hash, members, P2P_MAX_MEMBERS, &count) != P2P_NET_OK) {
        return MNET_ERR_NOT_FOUND;
    }
    err = mnet_group_sync_local_leave(group_hash);
    if (err != MNET_OK) {
        return err;
    }

    for (i = 0U; i < count; ++i) {
        p2p_message_t msg;
        uint8_t payload[1U + 16U + 32U];
        size_t payload_len;

        if (memcmp(members[i], g_mnet.security.node_pubkey, 32U) == 0) {
            continue;
        }
        payload_len = mnet_group_pack_member(payload, sizeof(payload), group_hash, g_mnet.security.node_pubkey);
        if (payload_len == 0U) {
            continue;
        }
        memset(&msg, 0, sizeof(msg));
        msg.type = P2P_MSG_GROUP_LEAVE;
        memcpy(msg.dst, members[i], 32U);
        memcpy(msg.group_hash, group_hash, 16U);
        memcpy(msg.payload, payload, payload_len);
        msg.payload_len = payload_len;
        if (mnet_peer_is_sendable(members[i])) {
            (void)p2p_protocol_send(&g_mnet.protocol, &msg);
        }
    }
    (void)p2p_network_invite_remove(&g_mnet.network, group_hash);
    return MNET_OK;
}

mnet_err_t mnet_group_members(const uint8_t group_hash[16], uint8_t out[][32], uint8_t capacity, uint8_t *count)
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (group_hash == NULL || out == NULL || count == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    return mnet_map_network_err(p2p_network_group_members(&g_mnet.network, group_hash, out, capacity, count));
}

bool mnet_group_is_member(const uint8_t node_id[32], const uint8_t group_hash[16])
{
    uint8_t members[P2P_MAX_MEMBERS][32];
    uint8_t count = 0U;
    uint8_t i;

    if (mnet_require_init() != MNET_OK || node_id == NULL || group_hash == NULL) {
        return false;
    }

    if (p2p_network_group_members(&g_mnet.network, group_hash, members, P2P_MAX_MEMBERS, &count) != P2P_NET_OK) {
        return false;
    }

    for (i = 0U; i < count; ++i) {
        if (memcmp(members[i], node_id, 32U) == 0) {
            return true;
        }
    }
    return false;
}

mnet_err_t mnet_publish(const char *key, const void *value, size_t len)
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    return mnet_map_data_err(p2p_data_publish(&g_mnet.data, key, P2P_DATA_VAR, value, len));
}

mnet_err_t mnet_update(const char *key, const void *value, size_t len)
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    return mnet_map_data_err(p2p_data_update(&g_mnet.data, key, value, len));
}

mnet_err_t mnet_request(const uint8_t node_id[32], const char *key,
                        void (*cb)(mnet_err_t, const void *, size_t))
{
    mnet_err_t err;
    p2p_message_t msg;
    size_t key_len;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (node_id == NULL || key == NULL || cb == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (g_mnet.request_cb != NULL) {
        return MNET_ERR_FULL;
    }
    key_len = strlen(key);
    if (key_len == 0U || key_len > MNET_MAX_PUBLIC_PAYLOAD || key_len >= sizeof(g_mnet.request_key)) {
        return MNET_ERR_INVALID_ARG;
    }

    if (memcmp(node_id, g_mnet.security.node_pubkey, 32U) == 0) {
        g_mnet.request_cb = cb;
        err = mnet_map_data_err(p2p_data_request(&g_mnet.data, node_id, key, mnet_request_adapter));
        if (err != MNET_OK && g_mnet.request_cb == cb) {
            g_mnet.request_cb = NULL;
            g_mnet.request_inflight = false;
        }
        return err;
    }

    {
        p2p_node_t *peer = mnet_find_peer_mut(node_id);
        if (peer == NULL) {
            return MNET_ERR_OFFLINE;
        }
        if (!peer->is_authorized) {
            return MNET_ERR_ACCESS;
        }
        if (!mnet_peer_is_sendable(node_id)) {
            return MNET_ERR_OFFLINE;
        }
    }

    g_mnet.request_cb = cb;
    g_mnet.request_inflight = true;
    g_mnet.request_id++;
    if (g_mnet.request_id == 0U) {
        g_mnet.request_id = 1U;
    }
    memcpy(g_mnet.request_node_id, node_id, 32U);
    memset(g_mnet.request_key, 0, sizeof(g_mnet.request_key));
    memcpy(g_mnet.request_key, key, key_len);
    g_mnet.request_started_ms = mnet_now_ms();
    g_mnet.request_timeout_ms = 5000U;

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_DATA_REQUEST;
    msg.msg_id = g_mnet.request_id;
    memcpy(msg.dst, node_id, 32U);
    memcpy(msg.payload, key, key_len);
    msg.payload_len = key_len;

    err = mnet_map_proto_err(p2p_protocol_send_transaction(&g_mnet.protocol,
                                                            &msg,
                                                            mnet_protocol_request_completion,
                                                            &g_mnet));
    if (err != MNET_OK) {
        g_mnet.request_cb = NULL;
        g_mnet.request_inflight = false;
    }
    return err;
}

mnet_err_t mnet_subscribe(const uint8_t node_id[32], const char *key,
                          void (*cb)(const char *, const void *, size_t))
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    return mnet_map_data_err(p2p_data_subscribe(&g_mnet.data, node_id, key, cb));
}

mnet_err_t mnet_unsubscribe(const uint8_t node_id[32], const char *key)
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    return mnet_map_data_err(p2p_data_unsubscribe(&g_mnet.data, node_id, key));
}

mnet_err_t mnet_list_vars(const uint8_t node_id[32],
                          void (*cb)(mnet_err_t, const char **, uint8_t))
{
    mnet_err_t err;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (cb == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (g_mnet.list_vars_cb != NULL) {
        return MNET_ERR_FULL;
    }
    {
        p2p_node_t *peer = mnet_find_peer_mut(node_id);
        if (peer == NULL) {
            return MNET_ERR_OFFLINE;
        }
        if (!peer->is_authorized) {
            return MNET_ERR_ACCESS;
        }
        if (!mnet_peer_is_sendable(node_id)) {
            return MNET_ERR_OFFLINE;
        }
    }

    g_mnet.list_vars_cb = cb;
    err = mnet_map_data_err(p2p_data_list_vars(&g_mnet.data, node_id, mnet_list_vars_adapter));
    if (err != MNET_OK && g_mnet.list_vars_cb == cb) {
        g_mnet.list_vars_cb = NULL;
    }
    return err;
}

mnet_err_t mnet_query(const uint8_t node_id[32], const char *table, const char *filter,
                      void (*cb)(mnet_err_t, const mnet_row_t *, uint8_t))
{
    mnet_err_t err;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (cb == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (g_mnet.query_cb != NULL) {
        return MNET_ERR_FULL;
    }
    {
        p2p_node_t *peer = mnet_find_peer_mut(node_id);
        if (peer == NULL) {
            return MNET_ERR_OFFLINE;
        }
        if (!peer->is_authorized) {
            return MNET_ERR_ACCESS;
        }
        if (!mnet_peer_is_sendable(node_id)) {
            return MNET_ERR_OFFLINE;
        }
    }

    g_mnet.query_cb = cb;
    err = mnet_map_data_err(p2p_data_query(&g_mnet.data, node_id, table, filter, mnet_query_adapter));
    if (err != MNET_OK && g_mnet.query_cb == cb) {
        g_mnet.query_cb = NULL;
    }
    return err;
}

mnet_err_t mnet_get_metrics(const uint8_t node_id[32],
                            void (*cb)(mnet_err_t, const mnet_metrics_t *))
{
    mnet_err_t err;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (cb == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (g_mnet.metrics_cb != NULL) {
        return MNET_ERR_FULL;
    }
    {
        p2p_node_t *peer = mnet_find_peer_mut(node_id);
        if (peer == NULL) {
            return MNET_ERR_OFFLINE;
        }
        if (!peer->is_authorized) {
            return MNET_ERR_ACCESS;
        }
        if (!mnet_peer_is_sendable(node_id)) {
            return MNET_ERR_OFFLINE;
        }
    }

    g_mnet.metrics_cb = cb;
    err = mnet_map_data_err(p2p_data_get_metrics(&g_mnet.data, node_id, mnet_metrics_adapter));
    if (err != MNET_OK && g_mnet.metrics_cb == cb) {
        g_mnet.metrics_cb = NULL;
    }
    return err;
}

mnet_err_t mnet_send_custom(const uint8_t node_id[32],
                            uint8_t msg_type,
                            const uint8_t *payload, size_t len)
{
    p2p_message_t msg;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (node_id == NULL || payload == NULL || len > MNET_MAX_PUBLIC_PAYLOAD || msg_type < P2P_MSG_CUSTOM) {
        return MNET_ERR_INVALID_ARG;
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = msg_type;
    memcpy(msg.dst, node_id, 32U);
    memcpy(msg.payload, payload, len);
    msg.payload_len = len;
    if (memcmp(node_id, g_mnet.security.node_pubkey, 32U) == 0) {
        mnet_deliver_custom_local(msg_type, g_mnet.security.node_pubkey, payload, len, NULL);
        return MNET_OK;
    }
    {
        p2p_node_t *peer = mnet_find_peer_mut(node_id);
        if (peer == NULL) {
            return MNET_ERR_OFFLINE;
        }
        if (!peer->is_authorized) {
            return MNET_ERR_ACCESS;
        }
        if (!mnet_peer_is_sendable(node_id)) {
            return MNET_ERR_OFFLINE;
        }
    }
    return mnet_map_proto_err(p2p_protocol_send(&g_mnet.protocol, &msg));
}

mnet_err_t mnet_send_group_custom(const uint8_t group_hash[16],
                                  uint8_t msg_type,
                                  const uint8_t *payload,
                                  size_t len,
                                  uint8_t *out_sent_count)
{
    p2p_message_t msg;
    uint8_t i;
    uint8_t sent_count = 0U;
    bool group_exists = false;
    bool broadcast_all = (group_hash == NULL);

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (payload == NULL || len > MNET_MAX_PUBLIC_PAYLOAD || msg_type < P2P_MSG_CUSTOM) {
        return MNET_ERR_INVALID_ARG;
    }

    if (!broadcast_all) {
        if (p2p_network_group_exists(&g_mnet.network, group_hash, &group_exists) != P2P_NET_OK || !group_exists) {
            return MNET_ERR_NOT_FOUND;
        }
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = msg_type;
    memcpy(msg.payload, payload, len);
    msg.payload_len = len;
    if (!broadcast_all) {
        memcpy(msg.group_hash, group_hash, 16U);
    }

    if (!broadcast_all && mnet_group_is_member(g_mnet.security.node_pubkey, group_hash)) {
        mnet_deliver_custom_local(msg_type, g_mnet.security.node_pubkey, payload, len, group_hash);
        sent_count++;
    }

    for (i = 0U; i < g_mnet.network.node_count; ++i) {
        p2p_node_t *peer = &g_mnet.network.nodes[i];
        p2p_endpoint_t *ep;

        if (!peer->is_online) {
            continue;
        }
        if (!peer->is_authorized) {
            continue;
        }
        if (!broadcast_all && !mnet_peer_matches_group(peer, group_hash)) {
            continue;
        }

        ep = mnet_find_endpoint_mut(peer->node_id);
        if (ep == NULL || !ep->valid || ep->state != P2P_ENDPOINT_AUTHENTICATED) {
            continue;
        }

        memcpy(msg.dst, peer->node_id, 32U);
        if (mnet_map_proto_err(p2p_protocol_send(&g_mnet.protocol, &msg)) == MNET_OK) {
            sent_count++;
        }
    }

    if (out_sent_count != NULL) {
        *out_sent_count = sent_count;
    }
    return MNET_OK;
}

mnet_err_t mnet_broadcast_custom(const uint8_t group_hash[16],
                                 uint8_t msg_type,
                                 const uint8_t *payload, size_t len)
{
    return mnet_send_group_custom(group_hash, msg_type, payload, len, NULL);
}

mnet_err_t mnet_discover_lan(void)
{
    uint8_t payload[MNET_MAX_PUBLIC_PAYLOAD];
    uint8_t wire[MNET_PROTOCOL_SERIALIZED_HEADER_SIZE + MNET_MAX_PUBLIC_PAYLOAD];
    p2p_message_t msg;
    size_t payload_len;
    size_t wire_len = sizeof(wire);
    const uint8_t broadcast_ip[4] = {255U, 255U, 255U, 255U};
    p2p_err_t tx_err;
    mnet_err_t err;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }

    payload_len = mnet_build_gossip_payload(payload, sizeof(payload));
    if (payload_len == 0U) {
        return MNET_ERR_INTERNAL;
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_GOSSIP;
    memcpy(msg.src, g_mnet.security.node_pubkey, 32U);
    memcpy(msg.payload, payload, payload_len);
    msg.payload_len = payload_len;

    if (p2p_protocol_serialize(&msg, wire, &wire_len) != P2P_PROTO_OK) {
        return MNET_ERR_INTERNAL;
    }

    tx_err = p2p_transport_send(&g_mnet.transport,
                                broadcast_ip,
                                g_mnet.cfg.local_port,
                                wire,
                                wire_len);
    err = (tx_err == P2P_OK) ? MNET_OK : MNET_ERR_TRANSPORT;
    return err;
}

mnet_err_t mnet_register_handler(uint8_t msg_type,
                                 void (*handler)(const uint8_t src[32],
                                                 const uint8_t *payload,
                                                 size_t len))
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (msg_type < P2P_MSG_CUSTOM || handler == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    g_mnet.custom_handlers[msg_type - P2P_MSG_CUSTOM] = handler;
    return mnet_map_proto_err(p2p_protocol_register_handler(&g_mnet.protocol, msg_type, mnet_protocol_custom_adapter));
}

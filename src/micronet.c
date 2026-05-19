#include "../include/micronet.h"

#include "micronet_internal.h"

#include <string.h>

mnet_context_t g_mnet;

mnet_context_t *mnet_internal_context(void)
{
    return &g_mnet;
}

static mnet_err_t mnet_map_transport_err(p2p_err_t err)
{
    switch (err) {
        case P2P_OK:
            return MNET_OK;
        case P2P_ERR_INVALID_ARG:
            return MNET_ERR_INVALID_ARG;
        case P2P_ERR_TIMEOUT:
            return MNET_ERR_TIMEOUT;
        case P2P_ERR_BUF_FULL:
            return MNET_ERR_FULL;
        default:
            return MNET_ERR_TRANSPORT;
    }
}

static mnet_err_t mnet_map_security_err(p2p_sec_err_t err)
{
    switch (err) {
        case P2P_SEC_OK:
            return MNET_OK;
        case P2P_SEC_ERR_NO_SESSION:
            return MNET_ERR_OFFLINE;
        case P2P_SEC_ERR_BUF:
            return MNET_ERR_FULL;
        default:
            return MNET_ERR_CRYPTO;
    }
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
    if (cb != NULL) {
        cb(mnet_map_data_err((p2p_data_err_t)err), value, len);
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

    memset(&transport_cfg, 0, sizeof(transport_cfg));
    transport_cfg.stun_host = cfg->stun_host != NULL ? cfg->stun_host : "stun.l.google.com";
    transport_cfg.stun_port = cfg->stun_port != 0U ? cfg->stun_port : 19302U;
    transport_cfg.local_port = cfg->local_port;
    transport_cfg.heartbeat_ms = cfg->heartbeat_ms != 0U ? cfg->heartbeat_ms : 5000U;
    transport_cfg.timeout_ms = cfg->offline_timeout_ms != 0U ? cfg->offline_timeout_ms : 15000U;
    transport_cfg.retry_count = cfg->retry_count != 0U ? cfg->retry_count : 3U;
    transport_cfg.retry_delay_ms = cfg->retry_interval_ms != 0U ? cfg->retry_interval_ms : 2000U;
    transport_cfg.rx_buf_size = sizeof(p2p_packet_t) * 8U;
    transport_cfg.tx_buf_size = sizeof(p2p_transport_retry_entry_t) * 8U;
    if (p2p_transport_init(&g_mnet.transport, &transport_cfg) != P2P_OK) {
        return MNET_ERR_TRANSPORT;
    }

    memset(&security_cfg, 0, sizeof(security_cfg));
    memcpy(security_cfg.node_privkey, cfg->node_privkey, sizeof(security_cfg.node_privkey));
    security_cfg.group_count = cfg->group_count;
    for (i = 0U; i < cfg->group_count && i < P2P_MAX_GROUPS; ++i) {
        memcpy(security_cfg.group_keys[i], cfg->groups[i].group_key, 16U);
    }
    security_cfg.store_keys = false;
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
    if (p2p_network_init(&g_mnet.network, &network_cfg, g_mnet.security.node_pubkey) != P2P_NET_OK) {
        p2p_security_deinit(&g_mnet.security);
        p2p_transport_deinit(&g_mnet.transport);
        return MNET_ERR_INTERNAL;
    }
    g_mnet.network.event_publish = mnet_network_event_adapter;

    for (i = 0U; i < cfg->group_count && i < P2P_MAX_GROUPS; ++i) {
        (void)p2p_network_group_join(&g_mnet.network, cfg->groups[i].group_hash, cfg->groups[i].group_key);
    }

    memset(&data_cfg, 0, sizeof(data_cfg));
    data_cfg.max_vars = cfg->max_vars != 0U ? cfg->max_vars : 16U;
    data_cfg.max_subs = 8U;
    data_cfg.notify_min_interval_ms = 1000U;
    data_cfg.compress_data = true;
    data_cfg.spool_size = 16U;
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

    return mnet_map_proto_err(p2p_protocol_tick(&g_mnet.protocol));
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

mnet_err_t mnet_node_list_online(uint8_t out[][32], uint8_t *count)
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
            memcpy(out[written++], g_mnet.network.nodes[i].node_id, 32U);
        }
    }
    *count = written;
    return MNET_OK;
}

mnet_err_t mnet_node_list_all(uint8_t out[][32], uint8_t *count)
{
    uint8_t i;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (out == NULL || count == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    for (i = 0U; i < g_mnet.network.node_count; ++i) {
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

mnet_err_t mnet_group_create(uint8_t out_group_hash[16], uint8_t out_group_key[16])
{
    uint8_t i;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (out_group_hash == NULL || out_group_key == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    if (mnet_map_network_err(p2p_network_group_create(&g_mnet.network, out_group_hash)) != MNET_OK) {
        return MNET_ERR_INTERNAL;
    }

    for (i = 0U; i < g_mnet.network.group_count; ++i) {
        if (memcmp(g_mnet.network.groups[i].group_hash, out_group_hash, 16U) == 0) {
            memcpy(out_group_key, g_mnet.network.groups[i].group_key, 16U);
            return MNET_OK;
        }
    }

    return MNET_ERR_INTERNAL;
}

mnet_err_t mnet_group_invite(const uint8_t node_id[32], const uint8_t group_hash[16])
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (node_id == NULL || group_hash == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    return mnet_map_network_err(p2p_network_group_invite(&g_mnet.network, node_id, group_hash));
}

mnet_err_t mnet_group_join(const uint8_t group_hash[16], const uint8_t group_key[16])
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (group_hash == NULL || group_key == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    return mnet_map_network_err(p2p_network_group_join(&g_mnet.network, group_hash, group_key));
}

mnet_err_t mnet_group_leave(const uint8_t group_hash[16])
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (group_hash == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    return mnet_map_network_err(p2p_network_group_leave(&g_mnet.network, group_hash));
}

mnet_err_t mnet_group_members(const uint8_t group_hash[16], uint8_t out[][32], uint8_t *count)
{
    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (group_hash == NULL || out == NULL || count == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    return mnet_map_network_err(p2p_network_group_members(&g_mnet.network, group_hash, out, count));
}

bool mnet_group_is_member(const uint8_t node_id[32], const uint8_t group_hash[16])
{
    uint8_t members[P2P_MAX_MEMBERS][32];
    uint8_t count = 0U;
    uint8_t i;

    if (mnet_require_init() != MNET_OK || node_id == NULL || group_hash == NULL) {
        return false;
    }

    if (p2p_network_group_members(&g_mnet.network, group_hash, members, &count) != P2P_NET_OK) {
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

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (cb == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (g_mnet.request_cb != NULL) {
        return MNET_ERR_FULL;
    }

    g_mnet.request_cb = cb;
    err = mnet_map_data_err(p2p_data_request(&g_mnet.data, node_id, key, mnet_request_adapter));
    if (err != MNET_OK && g_mnet.request_cb == cb) {
        g_mnet.request_cb = NULL;
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
    if (node_id == NULL || payload == NULL || len > sizeof(msg.payload) || msg_type < P2P_MSG_CUSTOM) {
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
    return mnet_map_proto_err(p2p_protocol_send(&g_mnet.protocol, &msg));
}

mnet_err_t mnet_broadcast_custom(const uint8_t group_hash[16],
                                 uint8_t msg_type,
                                 const uint8_t *payload, size_t len)
{
    p2p_message_t msg;

    if (mnet_require_init() != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }
    if (group_hash == NULL || payload == NULL || len > sizeof(msg.payload) || msg_type < P2P_MSG_CUSTOM) {
        return MNET_ERR_INVALID_ARG;
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = msg_type;
    memcpy(msg.payload, payload, len);
    msg.payload_len = len;
    memcpy(msg.group_hash, group_hash, 16U);
    if (mnet_group_is_member(g_mnet.security.node_pubkey, group_hash)) {
        mnet_deliver_custom_local(msg_type, g_mnet.security.node_pubkey, payload, len, group_hash);
    }
    if (!g_mnet.transport.last_peer_valid) {
        return MNET_OK;
    }
    return mnet_map_proto_err(p2p_protocol_broadcast(&g_mnet.protocol, group_hash, &msg));
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

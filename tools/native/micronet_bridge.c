#include "micronet_bridge.h"

#include "../../include/micronet.h"
#include "../../include/micronet_debug.h"

#include <string.h>
#include <time.h>

typedef struct {
    uint8_t node_id[32];
    uint32_t sent;
    uint32_t recv;
    uint8_t health_score;
    uint32_t free_heap;
    bool in_use;
} mnviz_node_stats_t;

typedef struct {
    bool initialized;
    mnviz_message_t messages[MNVIZ_MAX_MESSAGES];
    uint8_t message_count;
    mnviz_node_stats_t node_stats[MNVIZ_MAX_NODES + 1U];
} mnviz_bridge_state_t;

static mnviz_bridge_state_t g_bridge;

static uint32_t mnviz_now_s(void)
{
    return (uint32_t)time(NULL);
}

static mnviz_node_stats_t *mnviz_get_or_create_node_stats(const uint8_t node_id[32])
{
    uint8_t i;
    mnviz_node_stats_t *free_slot = NULL;

    if (node_id == NULL) {
        return NULL;
    }

    for (i = 0U; i < (uint8_t)(sizeof(g_bridge.node_stats) / sizeof(g_bridge.node_stats[0])); ++i) {
        if (!g_bridge.node_stats[i].in_use && free_slot == NULL) {
            free_slot = &g_bridge.node_stats[i];
            continue;
        }

        if (g_bridge.node_stats[i].in_use && memcmp(g_bridge.node_stats[i].node_id, node_id, 32U) == 0) {
            return &g_bridge.node_stats[i];
        }
    }

    if (free_slot != NULL) {
        memset(free_slot, 0, sizeof(*free_slot));
        memcpy(free_slot->node_id, node_id, 32U);
        free_slot->in_use = true;
        free_slot->health_score = 100U;
        free_slot->free_heap = 65536U;
        return free_slot;
    }

    return NULL;
}

static void mnviz_bump_sent(const uint8_t node_id[32])
{
    mnviz_node_stats_t *stats = mnviz_get_or_create_node_stats(node_id);
    if (stats != NULL) {
        stats->sent++;
    }
}

static void mnviz_bump_recv(const uint8_t node_id[32])
{
    mnviz_node_stats_t *stats = mnviz_get_or_create_node_stats(node_id);
    if (stats != NULL) {
        stats->recv++;
    }
}

static void mnviz_push_message(uint8_t direction,
                               uint8_t message_type,
                               const uint8_t src[32],
                               const uint8_t dst[32],
                               const uint8_t *payload,
                               size_t payload_len)
{
    mnviz_message_t message;
    uint8_t i;

    memset(&message, 0, sizeof(message));
    message.timestamp = mnviz_now_s();
    message.direction = direction;
    message.message_type = message_type;
    if (src != NULL) {
        memcpy(message.src, src, 32U);
    }
    if (dst != NULL) {
        memcpy(message.dst, dst, 32U);
    }
    if (payload != NULL && payload_len > 0U) {
        if (payload_len > sizeof(message.payload)) {
            payload_len = sizeof(message.payload);
        }
        memcpy(message.payload, payload, payload_len);
        message.payload_len = (uint32_t)payload_len;
    }

    for (i = g_bridge.message_count; i > 0U; --i) {
        if (i < MNVIZ_MAX_MESSAGES) {
            g_bridge.messages[i] = g_bridge.messages[i - 1U];
        }
    }
    g_bridge.messages[0] = message;
    if (g_bridge.message_count < MNVIZ_MAX_MESSAGES) {
        g_bridge.message_count++;
    }
}

static void mnviz_on_node_online(const uint8_t node_id[32])
{
    mnviz_push_message(2U, 1U, node_id, NULL, (const uint8_t *)"online", 6U);
}

static void mnviz_on_node_offline(const uint8_t node_id[32])
{
    mnviz_push_message(2U, 2U, node_id, NULL, (const uint8_t *)"offline", 7U);
}

static void mnviz_on_custom_message(const mnet_message_t *msg)
{
    if (msg == NULL) {
        return;
    }

    mnviz_bump_recv(msg->src);
    mnviz_push_message(0U, msg->type, msg->src, msg->dst, msg->payload, msg->payload_len);
}

static void mnviz_fill_mnet_config(const mnviz_init_config_t *cfg, mnet_config_t *out_cfg)
{
    memset(out_cfg, 0, sizeof(*out_cfg));
    memcpy(out_cfg->node_privkey, cfg->node_privkey, sizeof(out_cfg->node_privkey));
    out_cfg->node_name = cfg->node_name[0] != '\0' ? cfg->node_name : "MicronetViz";
    out_cfg->network_mode = MNET_MODE_LAN_ONLY;
    out_cfg->stun_enabled = false;
    out_cfg->stun_host = cfg->stun_host[0] != '\0' ? cfg->stun_host : NULL;
    out_cfg->stun_port = cfg->stun_port;
    out_cfg->local_port = cfg->local_port;
    out_cfg->heartbeat_ms = cfg->heartbeat_ms != 0U ? cfg->heartbeat_ms : 5000U;
    out_cfg->offline_timeout_ms = cfg->offline_timeout_ms != 0U ? cfg->offline_timeout_ms : 15000U;
    out_cfg->retry_interval_ms = cfg->retry_interval_ms != 0U ? cfg->retry_interval_ms : 2000U;
    out_cfg->retry_count = cfg->retry_count != 0U ? cfg->retry_count : 3U;
    out_cfg->max_nodes = cfg->max_nodes != 0U ? cfg->max_nodes : 16U;
    out_cfg->max_vars = cfg->max_vars != 0U ? cfg->max_vars : 16U;
    out_cfg->max_pending = cfg->max_pending != 0U ? cfg->max_pending : 8U;
    out_cfg->log_level = cfg->log_level;
    out_cfg->on_node_online = mnviz_on_node_online;
    out_cfg->on_node_offline = mnviz_on_node_offline;
    out_cfg->on_custom_msg = mnviz_on_custom_message;
}

int mnviz_init(const mnviz_init_config_t *cfg)
{
    mnet_config_t mnet_cfg;
    int err;
    uint8_t self_node_id[32];
    mnviz_node_stats_t *self_stats;

    if (cfg == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    if (g_bridge.initialized) {
        mnviz_shutdown();
    }

    memset(&g_bridge, 0, sizeof(g_bridge));
    mnviz_fill_mnet_config(cfg, &mnet_cfg);
    err = (int)mnet_init(&mnet_cfg);
    if (err == MNET_OK) {
        g_bridge.initialized = true;
        if (mnet_get_node_id(self_node_id) == MNET_OK) {
            self_stats = mnviz_get_or_create_node_stats(self_node_id);
            if (self_stats != NULL) {
                self_stats->health_score = 100U;
                self_stats->free_heap = 65536U;
            }
        }
    }
    return err;
}

void mnviz_shutdown(void)
{
    if (!g_bridge.initialized) {
        memset(&g_bridge, 0, sizeof(g_bridge));
        return;
    }

    mnet_deinit();
    memset(&g_bridge, 0, sizeof(g_bridge));
}

int mnviz_tick(void)
{
    int err;

    if (!g_bridge.initialized) {
        return MNET_ERR_NOT_INIT;
    }

    err = (int)mnet_tick();
    if (err != MNET_OK) {
        return err;
    }

    return MNET_OK;
}

bool mnviz_is_initialized(void)
{
    return g_bridge.initialized;
}

int mnviz_publish_text(const char *key, const char *value)
{
    int err;
    uint8_t self_node_id[32];

    if (!g_bridge.initialized || key == NULL || value == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    err = (int)mnet_publish(key, value, strlen(value) + 1U);
    if (err == MNET_OK) {
        if (mnet_get_node_id(self_node_id) == MNET_OK) {
            mnviz_bump_sent(self_node_id);
            mnviz_push_message(1U, 100U, self_node_id, self_node_id, (const uint8_t *)value, strlen(value));
        }
    }
    return err;
}

int mnviz_update_text(const char *key, const char *value)
{
    int err;
    uint8_t self_node_id[32];

    if (!g_bridge.initialized || key == NULL || value == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    err = (int)mnet_update(key, value, strlen(value) + 1U);
    if (err == MNET_OK) {
        if (mnet_get_node_id(self_node_id) == MNET_OK) {
            mnviz_bump_sent(self_node_id);
            mnviz_push_message(1U, 101U, self_node_id, self_node_id, (const uint8_t *)value, strlen(value));
        }
    }
    return err;
}

int mnviz_send_custom(const uint8_t node_id[32], uint8_t msg_type, const uint8_t *payload, uint32_t payload_len)
{
    int err;
    uint8_t self_node_id[32];

    if (!g_bridge.initialized || node_id == NULL || payload == NULL || payload_len == 0U) {
        return MNET_ERR_INVALID_ARG;
    }

    err = (int)mnet_send_custom(node_id, msg_type, payload, payload_len);
    if (err == MNET_OK) {
        if (mnet_get_node_id(self_node_id) == MNET_OK) {
            mnviz_bump_sent(self_node_id);
            mnviz_push_message(1U, msg_type, self_node_id, node_id, payload, payload_len);
        }
    }
    return err;
}

int mnviz_broadcast_custom(const uint8_t group_hash[16], uint8_t msg_type, const uint8_t *payload, uint32_t payload_len)
{
    int err;
    uint8_t self_node_id[32];

    if (!g_bridge.initialized || group_hash == NULL || payload == NULL || payload_len == 0U) {
        return MNET_ERR_INVALID_ARG;
    }

    err = (int)mnet_broadcast_custom(group_hash, msg_type, payload, payload_len);
    if (err == MNET_OK) {
        if (mnet_get_node_id(self_node_id) == MNET_OK) {
            mnviz_bump_sent(self_node_id);
            mnviz_push_message(1U, msg_type, self_node_id, NULL, payload, payload_len);
        }
    }
    return err;
}

int mnviz_group_create(uint8_t out_group_hash[16], uint8_t out_group_key[16])
{
    if (!g_bridge.initialized || out_group_hash == NULL || out_group_key == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    return (int)mnet_group_create(out_group_hash, out_group_key);
}

int mnviz_group_join(const uint8_t group_hash[16], const uint8_t group_key[16])
{
    if (!g_bridge.initialized || group_hash == NULL || group_key == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    return (int)mnet_group_join(group_hash, group_key);
}

int mnviz_group_leave(const uint8_t group_hash[16])
{
    if (!g_bridge.initialized || group_hash == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    return (int)mnet_group_leave(group_hash);
}

int mnviz_snapshot(mnviz_snapshot_t *out_snapshot)
{
    mnet_debug_stats_t stats;
    uint8_t var_count = 0U;
    mnviz_var_t vars[MNVIZ_MAX_VARS];

    if (out_snapshot == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (!g_bridge.initialized) {
        return MNET_ERR_NOT_INIT;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (mnet_debug_get_stats(&stats) != MNET_OK) {
        return MNET_ERR_INTERNAL;
    }
    (void)mnet_debug_copy_vars((mnet_debug_var_t *)vars, MNVIZ_MAX_VARS, &var_count);

    out_snapshot->node_count = stats.node_count;
    out_snapshot->online_count = stats.online_count;
    out_snapshot->group_count = stats.group_count;
    out_snapshot->var_count = var_count;
    out_snapshot->message_count = g_bridge.message_count;
    memcpy(&out_snapshot->local_metrics, &stats.local_metrics, sizeof(out_snapshot->local_metrics));
    return MNET_OK;
}

int mnviz_copy_nodes(mnviz_node_t *out_nodes, uint8_t capacity, uint8_t *out_count)
{
    mnet_debug_node_t debug_nodes[MNVIZ_MAX_NODES + 1U];
    uint8_t count = 0U;
    uint8_t i;

    if (out_nodes == NULL || out_count == NULL || capacity == 0U) {
        return MNET_ERR_INVALID_ARG;
    }
    if (!g_bridge.initialized) {
        return MNET_ERR_NOT_INIT;
    }

    if (mnet_debug_copy_nodes(debug_nodes, (uint8_t)(MNVIZ_MAX_NODES + 1U), &count) != MNET_OK) {
        return MNET_ERR_INTERNAL;
    }

    if (count > capacity) {
        count = capacity;
    }

    for (i = 0U; i < count; ++i) {
        mnviz_node_stats_t *stats;
        memset(&out_nodes[i], 0, sizeof(out_nodes[i]));
        memcpy(out_nodes[i].node_id, debug_nodes[i].node_id, 32U);
        memcpy(out_nodes[i].invited_by, debug_nodes[i].invited_by, 32U);
        memcpy(out_nodes[i].external_ip, debug_nodes[i].external_ip, 4U);
        out_nodes[i].external_port = debug_nodes[i].external_port;
        out_nodes[i].first_seen = debug_nodes[i].first_seen;
        out_nodes[i].last_seen = debug_nodes[i].last_seen;
        out_nodes[i].db_version = debug_nodes[i].db_version;
        out_nodes[i].group_count = debug_nodes[i].group_count;
        memcpy(out_nodes[i].group_hashes, debug_nodes[i].group_hashes, sizeof(out_nodes[i].group_hashes));
        out_nodes[i].is_online = debug_nodes[i].is_online;
        out_nodes[i].is_authorized = debug_nodes[i].is_authorized;
        out_nodes[i].is_self = debug_nodes[i].is_self;

        stats = mnviz_get_or_create_node_stats(debug_nodes[i].node_id);
        if (stats != NULL) {
            out_nodes[i].packets_sent = stats->sent;
            out_nodes[i].packets_recv = stats->recv;
            out_nodes[i].health_score = stats->health_score;
            out_nodes[i].free_heap = stats->free_heap;
        } else {
            out_nodes[i].packets_sent = 0U;
            out_nodes[i].packets_recv = 0U;
            out_nodes[i].health_score = 100U;
            out_nodes[i].free_heap = 65536U;
        }
    }

    *out_count = count;
    return MNET_OK;
}

int mnviz_copy_vars(mnviz_var_t *out_vars, uint8_t capacity, uint8_t *out_count)
{
    return (int)mnet_debug_copy_vars((mnet_debug_var_t *)out_vars, capacity, out_count);
}

int mnviz_copy_messages(mnviz_message_t *out_messages, uint8_t capacity, uint8_t *out_count)
{
    uint8_t i;
    uint8_t written;

    if (out_messages == NULL || out_count == NULL || capacity == 0U) {
        return MNET_ERR_INVALID_ARG;
    }
    if (!g_bridge.initialized) {
        return MNET_ERR_NOT_INIT;
    }

    written = g_bridge.message_count < capacity ? g_bridge.message_count : capacity;
    for (i = 0U; i < written; ++i) {
        out_messages[i] = g_bridge.messages[i];
    }
    *out_count = written;
    return MNET_OK;
}

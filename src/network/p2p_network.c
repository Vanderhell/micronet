#include "p2p_network.h"

#include "../security/p2p_security.h"

#include <string.h>

enum {
    P2P_NET_STATE_IDLE = 0,
    P2P_NET_STATE_JOINING = 1,
    P2P_NET_STATE_ACTIVE = 2,
    P2P_NET_STATE_GOSSIPING = 3,
    P2P_NET_STATE_SYNCING = 4,
    P2P_NET_STATE_ISOLATED = 5
};

static void p2p_network_publish(p2p_network_t *ctx,
                                p2p_network_event_id_t event_id,
                                const void *payload,
                                size_t payload_len)
{
    microbus_event_t event;

    if (ctx == NULL || ctx->event_publish == NULL) {
        return;
    }

    event.event_id = (uint8_t)event_id;
    event.payload = payload;
    event.payload_len = payload_len;
    ctx->event_publish(&event, ctx->event_user);
}

static int p2p_network_same_id(const uint8_t a[32], const uint8_t b[32])
{
    return memcmp(a, b, 32U) == 0;
}

static p2p_node_t *p2p_network_find_node_mut(p2p_network_t *ctx, const uint8_t node_id[32])
{
    uint8_t i;

    if (ctx == NULL || node_id == NULL) {
        return NULL;
    }

    if (p2p_network_same_id(ctx->self.node_id, node_id)) {
        return &ctx->self;
    }

    for (i = 0U; i < ctx->node_count; ++i) {
        if (p2p_network_same_id(ctx->nodes[i].node_id, node_id)) {
            return &ctx->nodes[i];
        }
    }

    return NULL;
}

static uint32_t p2p_network_next_db_version(p2p_network_t *ctx)
{
    ctx->last_db_version++;
    if (ctx->last_db_version == 0U) {
        ctx->last_db_version = 1U;
    }
    return ctx->last_db_version;
}

p2p_net_err_t p2p_network_init(p2p_network_t *ctx,
                               const p2p_network_config_t *cfg,
                               const uint8_t self_node_id[32])
{
    if (ctx == NULL || cfg == NULL || self_node_id == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->config = *cfg;
    ctx->now_ms = cfg->now_ms;
    if (ctx->now_ms == NULL) {
        return P2P_NET_ERR_SYNC;
    }
    ctx->fsm.state = P2P_NET_STATE_JOINING;
    ctx->gossip_timer.interval_ms = cfg->gossip_interval_ms;
    ctx->sync_timer.interval_ms = cfg->sync_interval_ms;
    ctx->gossip_timer.armed = cfg->gossip_interval_ms > 0U;
    ctx->sync_timer.armed = cfg->sync_interval_ms > 0U;
    ctx->gossip_timer.last_ms = ctx->now_ms();
    ctx->sync_timer.last_ms = ctx->gossip_timer.last_ms;
    ctx->last_sync_ms = ctx->gossip_timer.last_ms;
    ctx->last_sync_version = 0U;
    ctx->last_db_version = 1U;

    memcpy(ctx->self.node_id, self_node_id, 32U);
    ctx->self.first_seen = ctx->gossip_timer.last_ms;
    ctx->self.last_seen = ctx->gossip_timer.last_ms;
    ctx->self.is_online = true;
    ctx->self.db_version = ctx->last_db_version;

    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_add_node(p2p_network_t *ctx, const p2p_node_t *node)
{
    p2p_node_t *slot;

    if (ctx == NULL || node == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    if (p2p_network_find_node_mut(ctx, node->node_id) != NULL) {
        return P2P_NET_ERR_NODE_EXISTS;
    }

    if (ctx->node_count >= ctx->config.max_nodes || ctx->node_count >= P2P_MAX_NODES) {
        return P2P_NET_ERR_NODE_FULL;
    }

    slot = &ctx->nodes[ctx->node_count++];
    *slot = *node;
    if (slot->first_seen == 0U) {
        slot->first_seen = ctx->now_ms();
    }
    if (slot->last_seen == 0U) {
        slot->last_seen = slot->first_seen;
    }
    if (slot->db_version == 0U) {
        slot->db_version = p2p_network_next_db_version(ctx);
    }
    if (slot->group_count > 0U) {
        slot->is_authorized = true;
    }

    p2p_network_publish(ctx, P2P_EVENT_NODE_NEW, slot, sizeof(*slot));
    ctx->fsm.state = P2P_NET_STATE_ACTIVE;
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_remove_node(p2p_network_t *ctx, const uint8_t node_id[32])
{
    uint8_t i;

    if (ctx == NULL || node_id == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    for (i = 0U; i < ctx->node_count; ++i) {
        if (p2p_network_same_id(ctx->nodes[i].node_id, node_id)) {
            if ((i + 1U) < ctx->node_count) {
                memmove(&ctx->nodes[i],
                        &ctx->nodes[i + 1U],
                        (size_t)(ctx->node_count - i - 1U) * sizeof(ctx->nodes[0]));
            }
            memset(&ctx->nodes[ctx->node_count - 1U], 0, sizeof(ctx->nodes[0]));
            ctx->node_count--;
            return P2P_NET_OK;
        }
    }

    return P2P_NET_ERR_NOT_FOUND;
}

p2p_net_err_t p2p_network_find_node(p2p_network_t *ctx,
                                    const uint8_t node_id[32],
                                    p2p_node_t *out)
{
    p2p_node_t *node = p2p_network_find_node_mut(ctx, node_id);

    if (node == NULL || out == NULL) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    *out = *node;
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_set_online(p2p_network_t *ctx,
                                     const uint8_t node_id[32],
                                     bool online)
{
    p2p_node_t *node = p2p_network_find_node_mut(ctx, node_id);

    if (node == NULL) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    if (node->is_online != online) {
        node->is_online = online;
        node->last_seen = ctx->now_ms();
        node->db_version = p2p_network_next_db_version(ctx);
        p2p_network_publish(ctx,
                            online ? P2P_EVENT_NODE_ONLINE : P2P_EVENT_NODE_OFFLINE,
                            node,
                            sizeof(*node));
    }

    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_peer_join_group(p2p_network_t *ctx,
                                          const uint8_t node_id[32],
                                          const uint8_t group_hash[16])
{
    p2p_node_t *node;
    p2p_group_t *group;
    uint8_t i;

    if (ctx == NULL || node_id == NULL || group_hash == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    node = p2p_network_find_node_mut(ctx, node_id);
    if (node == NULL) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    group = NULL;
    for (i = 0U; i < ctx->group_count; ++i) {
        if (memcmp(ctx->groups[i].group_hash, group_hash, 16U) == 0) {
            group = &ctx->groups[i];
            break;
        }
    }
    if (group == NULL) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    for (i = 0U; i < group->member_count; ++i) {
        if (memcmp(group->members[i], node_id, 32U) == 0) {
            goto sync_node_groups;
        }
    }

    if (group->member_count >= P2P_MAX_MEMBERS) {
        return P2P_NET_ERR_GROUP_FULL;
    }

    memcpy(group->members[group->member_count++], node_id, 32U);
    group->db_version = ++ctx->last_db_version;

sync_node_groups:
    for (i = 0U; i < node->group_count; ++i) {
        if (memcmp(node->group_hashes[i], group_hash, 16U) == 0) {
            node->is_authorized = true;
            return P2P_NET_OK;
        }
    }

    if (node->group_count >= P2P_MAX_GROUPS) {
        return P2P_NET_ERR_GROUP_FULL;
    }

    memcpy(node->group_hashes[node->group_count++], group_hash, 16U);
    node->is_authorized = true;
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_peer_leave_group(p2p_network_t *ctx,
                                           const uint8_t node_id[32],
                                           const uint8_t group_hash[16])
{
    p2p_node_t *node;
    p2p_group_t *group;
    uint8_t i;

    if (ctx == NULL || node_id == NULL || group_hash == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    node = p2p_network_find_node_mut(ctx, node_id);
    if (node == NULL) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    group = NULL;
    for (i = 0U; i < ctx->group_count; ++i) {
        if (memcmp(ctx->groups[i].group_hash, group_hash, 16U) == 0) {
            group = &ctx->groups[i];
            break;
        }
    }
    if (group == NULL) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    for (i = 0U; i < group->member_count; ++i) {
        if (memcmp(group->members[i], node_id, 32U) == 0) {
            if ((i + 1U) < group->member_count) {
                memmove(group->members[i],
                        group->members[i + 1U],
                        (size_t)(group->member_count - i - 1U) * sizeof(group->members[0]));
            }
            memset(group->members[group->member_count - 1U], 0, 32U);
            group->member_count--;
            break;
        }
    }

    for (i = 0U; i < node->group_count; ++i) {
        if (memcmp(node->group_hashes[i], group_hash, 16U) == 0) {
            if ((i + 1U) < node->group_count) {
                memmove(node->group_hashes[i],
                        node->group_hashes[i + 1U],
                        (size_t)(node->group_count - i - 1U) * sizeof(node->group_hashes[0]));
            }
            memset(node->group_hashes[node->group_count - 1U], 0, 16U);
            node->group_count--;
            break;
        }
    }

    node->is_authorized = node->group_count > 0U;
    group->db_version = ++ctx->last_db_version;
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_group_exists(p2p_network_t *ctx,
                                       const uint8_t group_hash[16],
                                       bool *out_exists)
{
    uint8_t i;

    if (ctx == NULL || group_hash == NULL || out_exists == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    for (i = 0U; i < ctx->group_count; ++i) {
        if (memcmp(ctx->groups[i].group_hash, group_hash, 16U) == 0) {
            *out_exists = true;
            return P2P_NET_OK;
        }
    }

    *out_exists = false;
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_tick(p2p_network_t *ctx)
{
    uint32_t now_ms;
    uint8_t i;
    uint8_t online_count = 0U;

    if (ctx == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    now_ms = ctx->now_ms();
    for (i = 0U; i < ctx->node_count; ++i) {
        p2p_node_t *node = &ctx->nodes[i];
        if (node->is_online && ctx->config.offline_timeout_ms > 0U &&
            (now_ms - node->last_seen) >= ctx->config.offline_timeout_ms) {
            node->is_online = false;
            node->db_version = p2p_network_next_db_version(ctx);
            p2p_network_publish(ctx, P2P_EVENT_NODE_OFFLINE, node, sizeof(*node));
        }
        if (node->is_online) {
            online_count++;
        }
    }

    if (ctx->gossip_timer.armed && (now_ms - ctx->gossip_timer.last_ms) >= ctx->gossip_timer.interval_ms) {
        ctx->gossip_timer.last_ms = now_ms;
        ctx->fsm.state = P2P_NET_STATE_GOSSIPING;
    } else if (ctx->sync_timer.armed && (now_ms - ctx->sync_timer.last_ms) >= ctx->sync_timer.interval_ms) {
        ctx->sync_timer.last_ms = now_ms;
        ctx->last_sync_ms = now_ms;
        ctx->last_sync_version = ctx->last_db_version;
        ctx->fsm.state = P2P_NET_STATE_SYNCING;
        p2p_network_publish(ctx, P2P_EVENT_DB_SYNCED, &ctx->last_sync_ms, sizeof(ctx->last_sync_ms));
    } else {
        ctx->fsm.state = online_count > 0U ? P2P_NET_STATE_ACTIVE : P2P_NET_STATE_ISOLATED;
    }

    return P2P_NET_OK;
}

void p2p_network_deinit(p2p_network_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
}

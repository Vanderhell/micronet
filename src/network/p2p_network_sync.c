#include "p2p_network.h"

#include <string.h>

static const uint8_t p2p_network_zero32[32] = {0};

static p2p_node_t *p2p_network_sync_find_slot(p2p_network_t *ctx, const uint8_t node_id[32])
{
    uint8_t i;

    if (ctx == NULL || node_id == NULL) {
        return NULL;
    }

    if (memcmp(ctx->self.node_id, node_id, 32U) == 0) {
        return &ctx->self;
    }

    for (i = 0U; i < ctx->node_count; ++i) {
        if (memcmp(ctx->nodes[i].node_id, node_id, 32U) == 0) {
            return &ctx->nodes[i];
        }
    }

    if (ctx->node_count >= ctx->config.max_nodes || ctx->node_count >= P2P_MAX_NODES) {
        return NULL;
    }

    return &ctx->nodes[ctx->node_count++];
}

p2p_net_err_t p2p_network_sync_apply(p2p_network_t *ctx, const p2p_node_t *node)
{
    p2p_node_t *slot;
    bool was_empty;

    if (ctx == NULL || node == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    slot = p2p_network_sync_find_slot(ctx, node->node_id);
    if (slot == NULL) {
        return P2P_NET_ERR_NODE_FULL;
    }

    was_empty = memcmp(slot->node_id, p2p_network_zero32, 32U) == 0;
    if (!was_empty && slot->db_version > node->db_version) {
        return P2P_NET_OK;
    }

    *slot = *node;
    if (node->db_version > ctx->last_db_version) {
        ctx->last_db_version = node->db_version;
    }

    if (was_empty) {
        microbus_event_t event;
        event.event_id = P2P_EVENT_NODE_NEW;
        event.payload = slot;
        event.payload_len = sizeof(*slot);
        if (ctx->event_publish != NULL) {
            ctx->event_publish(&event, ctx->event_user);
        }
    }

    return P2P_NET_OK;
}

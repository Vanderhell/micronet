#include "../include/micronet_debug.h"

#include "micronet_internal.h"

#include <string.h>

static mnet_err_t mnet_debug_require_init(const mnet_context_t *ctx)
{
    if (ctx == NULL || !ctx->initialized) {
        return MNET_ERR_NOT_INIT;
    }

    return MNET_OK;
}

mnet_err_t mnet_debug_get_stats(mnet_debug_stats_t *out_stats)
{
    mnet_context_t *ctx = mnet_internal_context();
    uint8_t i;

    if (out_stats == NULL) {
        return MNET_ERR_INVALID_ARG;
    }
    if (mnet_debug_require_init(ctx) != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }

    memset(out_stats, 0, sizeof(*out_stats));
    out_stats->node_count = (uint8_t)(ctx->network.node_count + 1U);
    out_stats->group_count = ctx->network.group_count;
    out_stats->online_count = 1U;

    p2p_data_refresh_metrics(&ctx->data);
    out_stats->local_metrics = *(const mnet_metrics_t *)&ctx->data.metrics;

    for (i = 0U; i < ctx->network.node_count; ++i) {
        if (ctx->network.nodes[i].is_online) {
            out_stats->online_count++;
        }
    }

    return MNET_OK;
}

mnet_err_t mnet_debug_copy_nodes(mnet_debug_node_t *out_nodes, uint8_t capacity, uint8_t *out_count)
{
    mnet_context_t *ctx = mnet_internal_context();
    uint8_t written = 0U;
    uint8_t i;

    if (out_nodes == NULL || out_count == NULL || capacity == 0U) {
        return MNET_ERR_INVALID_ARG;
    }
    if (mnet_debug_require_init(ctx) != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }

    memset(out_nodes, 0, sizeof(out_nodes[0]) * capacity);

    memcpy(out_nodes[written].node_id, ctx->network.self.node_id, 32U);
    memcpy(out_nodes[written].invited_by, ctx->network.self.invited_by, 32U);
    memcpy(out_nodes[written].external_ip, ctx->network.self.external_ip, 4U);
    out_nodes[written].external_port = ctx->network.self.external_port;
    out_nodes[written].first_seen = ctx->network.self.first_seen;
    out_nodes[written].last_seen = ctx->network.self.last_seen;
    out_nodes[written].db_version = ctx->network.self.db_version;
    out_nodes[written].group_count = ctx->network.self.group_count;
    memcpy(out_nodes[written].group_hashes, ctx->network.self.group_hashes, sizeof(out_nodes[written].group_hashes));
    out_nodes[written].is_online = ctx->network.self.is_online;
    out_nodes[written].is_authorized = ctx->network.self.is_authorized;
    out_nodes[written].is_self = true;
    written++;

    for (i = 0U; i < ctx->network.node_count && written < capacity; ++i) {
        const p2p_node_t *node = &ctx->network.nodes[i];

        memcpy(out_nodes[written].node_id, node->node_id, 32U);
        memcpy(out_nodes[written].invited_by, node->invited_by, 32U);
        memcpy(out_nodes[written].external_ip, node->external_ip, 4U);
        out_nodes[written].external_port = node->external_port;
        out_nodes[written].first_seen = node->first_seen;
        out_nodes[written].last_seen = node->last_seen;
        out_nodes[written].db_version = node->db_version;
        out_nodes[written].group_count = node->group_count;
        memcpy(out_nodes[written].group_hashes, node->group_hashes, sizeof(out_nodes[written].group_hashes));
        out_nodes[written].is_online = node->is_online;
        out_nodes[written].is_authorized = node->is_authorized;
        out_nodes[written].is_self = false;
        written++;
    }

    *out_count = written;
    return MNET_OK;
}

mnet_err_t mnet_debug_copy_vars(mnet_debug_var_t *out_vars, uint8_t capacity, uint8_t *out_count)
{
    mnet_context_t *ctx = mnet_internal_context();
    uint8_t i;
    uint8_t written = 0U;

    if (out_vars == NULL || out_count == NULL || capacity == 0U) {
        return MNET_ERR_INVALID_ARG;
    }
    if (mnet_debug_require_init(ctx) != MNET_OK) {
        return MNET_ERR_NOT_INIT;
    }

    memset(out_vars, 0, sizeof(out_vars[0]) * capacity);

    for (i = 0U; i < ctx->data.var_count && written < capacity; ++i) {
        const p2p_var_t *var = &ctx->data.vars[i];

        memcpy(out_vars[written].key, var->key, sizeof(out_vars[written].key));
        out_vars[written].type = var->type;
        out_vars[written].access = var->access;
        memcpy(out_vars[written].data, var->data, var->data_len);
        out_vars[written].data_len = var->data_len;
        out_vars[written].updated_at = var->updated_at;
        out_vars[written].is_public = var->is_public;
        written++;
    }

    *out_count = written;
    return MNET_OK;
}

#include "p2p_data.h"

#include <string.h>

void p2p_data_collect_metrics(p2p_data_t *ctx, p2p_metrics_t *out)
{
    uint32_t now_ms;

    if (ctx == NULL || out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->version = 1U;
    out->size = (uint16_t)sizeof(*out);

    now_ms = ctx->now_ms();
    out->uptime_s = (now_ms - ctx->started_ms) / 1000U;
    out->free_heap_available = true;
    out->free_heap = ctx->metrics.free_heap != 0U ? ctx->metrics.free_heap : 65536U;
    out->group_count = 0U;
    out->variables = ctx->var_count;
    out->variables_max = ctx->config.max_vars > 0U ? ctx->config.max_vars : P2P_MAX_VARS;
    out->subscriptions = (uint32_t)ctx->sub_count + (uint32_t)ctx->remote_sub_count;
    out->subscriptions_max = (uint32_t)P2P_MAX_SUBS * 2U;
    out->pending_transactions = 0U;
    out->pending_transactions_max = 0U;
    out->health_score = 100U;
}

void p2p_data_refresh_metrics(p2p_data_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    p2p_data_collect_metrics(ctx, &ctx->metrics);
}

p2p_data_err_t p2p_data_get_metrics(p2p_data_t *ctx, const uint8_t node_id[32],
                                    void (*cb)(int, const p2p_metrics_t *))
{
    (void)node_id;
    if (ctx == NULL || cb == NULL) {
        return P2P_DATA_ERR_TYPE;
    }

    p2p_data_refresh_metrics(ctx);
    cb(P2P_DATA_OK, &ctx->metrics);
    return P2P_DATA_OK;
}

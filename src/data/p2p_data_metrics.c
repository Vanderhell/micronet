#include "p2p_data.h"

#include <string.h>

void p2p_data_refresh_metrics(p2p_data_t *ctx)
{
    uint32_t now_ms;

    if (ctx == NULL) {
        return;
    }

    now_ms = ctx->now_ms();
    ctx->metrics.uptime_s = (now_ms - ctx->started_ms) / 1000U;
    ctx->metrics.connected_nodes = ctx->sub_count;
    ctx->metrics.group_count = 0U;
    ctx->metrics.free_heap = 65536U - ((uint32_t)ctx->var_count * 256U);
    ctx->health.sample_count++;
    ctx->health.health_score = (uint8_t)(100U - (ctx->metrics.errors > 100U ? 100U : ctx->metrics.errors));
    ctx->metrics.health_score = ctx->health.health_score;
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

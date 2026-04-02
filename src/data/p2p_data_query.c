#include "p2p_data.h"

#include <string.h>

p2p_data_err_t p2p_data_request(p2p_data_t *ctx, const uint8_t node_id[32],
                                const char *key,
                                void (*cb)(int, const void *, size_t))
{
    int idx;

    (void)node_id;
    if (ctx == NULL || key == NULL || cb == NULL) {
        return P2P_DATA_ERR_TYPE;
    }

    idx = p2p_data_find_var_index(ctx, key);
    if (idx < 0) {
        cb(P2P_DATA_ERR_NOT_FOUND, NULL, 0U);
        return P2P_DATA_ERR_NOT_FOUND;
    }

    if (!ctx->vars[idx].is_public) {
        cb(P2P_DATA_ERR_ACCESS, NULL, 0U);
        return P2P_DATA_ERR_ACCESS;
    }

    cb(P2P_DATA_OK, ctx->vars[idx].data, ctx->vars[idx].data_len);
    return P2P_DATA_OK;
}

p2p_data_err_t p2p_data_query(p2p_data_t *ctx, const uint8_t node_id[32],
                              const char *table, const char *filter,
                              void (*cb)(int, const p2p_row_t *, uint8_t))
{
    p2p_row_t rows[1];
    int idx;

    (void)node_id;
    (void)filter;
    if (ctx == NULL || table == NULL || cb == NULL) {
        return P2P_DATA_ERR_TYPE;
    }

    idx = p2p_data_find_var_index(ctx, table);
    if (idx < 0 || ctx->vars[idx].type != P2P_DATA_TABLE) {
        cb(P2P_DATA_ERR_NOT_FOUND, NULL, 0U);
        return P2P_DATA_ERR_NOT_FOUND;
    }

    memset(rows, 0, sizeof(rows));
    memcpy(rows[0].bytes, ctx->vars[idx].data, ctx->vars[idx].data_len);
    rows[0].len = ctx->vars[idx].data_len;
    cb(P2P_DATA_OK, rows, 1U);
    return P2P_DATA_OK;
}

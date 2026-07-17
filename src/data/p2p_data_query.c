#include "p2p_data.h"

#include <string.h>

static int p2p_data_validate_key(const char *key)
{
    size_t len;

    if (key == NULL) {
        return 0;
    }

    len = 0U;
    while (len < P2P_MAX_KEY_LEN) {
        if (key[len] == '\0') {
            break;
        }
        len++;
    }
    if (len >= P2P_MAX_KEY_LEN) {
        return 0;
    }

    return 1;
}

p2p_data_err_t p2p_data_request(p2p_data_t *ctx, const uint8_t node_id[32],
                                const char *key,
                                void (*cb)(int, const void *, size_t))
{
    int idx;

    (void)node_id;
    if (ctx == NULL || key == NULL || cb == NULL) {
        return P2P_DATA_ERR_TYPE;
    }
    if (!p2p_data_validate_key(key)) {
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
    int idx;

    (void)node_id;
    (void)filter;
    if (ctx == NULL || table == NULL || cb == NULL) {
        return P2P_DATA_ERR_TYPE;
    }
    if (!p2p_data_validate_key(table)) {
        return P2P_DATA_ERR_TYPE;
    }
    idx = p2p_data_find_var_index(ctx, table);
    if (idx < 0 || ctx->vars[idx].type != P2P_DATA_TABLE) {
        cb(P2P_DATA_ERR_NOT_FOUND, NULL, 0U);
        return P2P_DATA_ERR_NOT_FOUND;
    }

    memset(ctx->query_rows, 0, sizeof(ctx->query_rows));
    if (ctx->vars[idx].data_len > P2P_MAX_VAL_LEN) {
        cb(P2P_DATA_ERR_TYPE, NULL, 0U);
        return P2P_DATA_ERR_TYPE;
    }
    memcpy(ctx->query_rows[0].bytes, ctx->vars[idx].data, ctx->vars[idx].data_len);
    ctx->query_rows[0].len = ctx->vars[idx].data_len;
    ctx->query_row_count = 1U;
    cb(P2P_DATA_OK, ctx->query_rows, ctx->query_row_count);
    return P2P_DATA_OK;
}

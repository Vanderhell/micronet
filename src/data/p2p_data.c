#include "p2p_data.h"

#include <string.h>
#include <time.h>

static uint32_t p2p_data_now_ms_default(void)
{
    return (uint32_t)((uint64_t)time(NULL) * 1000ULL);
}

int p2p_data_find_var_index(const p2p_data_t *ctx, const char *key)
{
    uint8_t i;

    if (ctx == NULL || key == NULL) {
        return -1;
    }

    for (i = 0U; i < ctx->var_count; ++i) {
        if (strncmp(ctx->vars[i].key, key, P2P_MAX_KEY_LEN) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static int p2p_data_encode_rle(const uint8_t *src, size_t src_len, uint8_t *dst, size_t *dst_len)
{
    size_t in_pos = 0U;
    size_t out_pos = 0U;

    while (in_pos < src_len) {
        uint8_t run = 1U;
        while ((in_pos + run) < src_len && run < 255U && src[in_pos] == src[in_pos + run]) {
            run++;
        }
        if ((out_pos + 2U) > P2P_MAX_VAL_LEN) {
            return 0;
        }
        dst[out_pos++] = run;
        dst[out_pos++] = src[in_pos];
        in_pos += run;
    }

    *dst_len = out_pos;
    return 1;
}

static int p2p_data_decode_rle(const uint8_t *src, size_t src_len,
                               uint8_t *dst, size_t *dst_len,
                               size_t expected_out_len)
{
    size_t in_pos = 0U;
    size_t out_pos = 0U;

    if (src == NULL || dst == NULL || dst_len == NULL) {
        return 0;
    }

    while (in_pos < src_len) {
        uint8_t run;
        uint8_t val;
        size_t j;
        if ((in_pos + 2U) > src_len) {
            return 0;
        }
        run = src[in_pos++];
        val = src[in_pos++];
        if (run == 0U) {
            return 0;
        }
        if ((out_pos + run) > expected_out_len || (out_pos + run) > P2P_MAX_VAL_LEN) {
            return 0;
        }
        for (j = 0U; j < (size_t)run; ++j) {
            dst[out_pos++] = val;
        }
    }

    if (out_pos != expected_out_len) {
        return 0;
    }

    *dst_len = out_pos;
    return 1;
}

int p2p_data_decode_value(const p2p_var_t *var, uint8_t out[P2P_MAX_VAL_LEN], size_t *out_len)
{
    size_t decoded_len = 0U;

    if (var == NULL || out == NULL || out_len == NULL) {
        return 0;
    }

    if (var->encoding == 0U) {
        if (var->data_len > P2P_MAX_VAL_LEN) {
            return 0;
        }
        if (var->data_len > 0U) {
            memcpy(out, var->data, var->data_len);
        }
        *out_len = var->data_len;
        return 1;
    }

    if (var->encoding == 1U) {
        if (var->raw_len > P2P_MAX_VAL_LEN) {
            return 0;
        }
        if (!p2p_data_decode_rle(var->data, var->data_len, out, &decoded_len, (size_t)var->raw_len)) {
            return 0;
        }
        *out_len = decoded_len;
        return 1;
    }

    return 0;
}

p2p_data_err_t p2p_data_copy_value(p2p_var_t *var, const void *value, size_t len, bool compress)
{
    uint8_t compressed[P2P_MAX_VAL_LEN];
    size_t compressed_len = 0U;

    if (var == NULL || (value == NULL && len > 0U) || len > P2P_MAX_VAL_LEN) {
        return P2P_DATA_ERR_TYPE;
    }

    if (compress && len > 64U &&
        p2p_data_encode_rle((const uint8_t *)value, len, compressed, &compressed_len) &&
        compressed_len < len) {
        memcpy(var->data, compressed, compressed_len);
        var->data_len = compressed_len;
        var->raw_len = (uint16_t)len;
        var->encoding = 1U;
    } else {
        if (len > 0U) {
            memcpy(var->data, value, len);
        }
        var->data_len = len;
        var->raw_len = (uint16_t)len;
        var->encoding = 0U;
    }

    return P2P_DATA_OK;
}

p2p_data_err_t p2p_data_init(p2p_data_t *ctx, const p2p_data_config_t *cfg)
{
    if (ctx == NULL || cfg == NULL) {
        return P2P_DATA_ERR_TYPE;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->config = *cfg;
    ctx->now_ms = p2p_data_now_ms_default;
    ctx->started_ms = ctx->now_ms();
    ctx->health.health_score = 100U;
    ctx->metrics.health_score = 100U;
    ctx->metrics.free_heap = 65536U;
    return P2P_DATA_OK;
}

p2p_data_err_t p2p_data_tick(p2p_data_t *ctx)
{
    uint8_t i;

    if (ctx == NULL) {
        return P2P_DATA_ERR_TYPE;
    }

    p2p_data_refresh_metrics(ctx);
    if (ctx->spool.in_use) {
        for (i = 0U; i < ctx->sub_count; ++i) {
            if (ctx->subs[i].cb != NULL &&
                strncmp(ctx->subs[i].key, ctx->spool.key, P2P_MAX_KEY_LEN) == 0) {
                ctx->subs[i].cb(ctx->spool.key, ctx->spool.payload, ctx->spool.payload_len);
                ctx->subs[i].last_notified = ctx->now_ms();
                ctx->spool.in_use = false;
                break;
            }
        }
    }
    return P2P_DATA_OK;
}

void p2p_data_deinit(p2p_data_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
}

#include "p2p_data.h"

#include <string.h>

static size_t p2p_strnlen_c99(const char *s, size_t max_len)
{
    size_t i;

    if (s == NULL) {
        return 0U;
    }

    for (i = 0U; i < max_len; ++i) {
        if (s[i] == '\0') {
            break;
        }
    }

    return i;
}

static int p2p_data_copy_key(char dst[P2P_MAX_KEY_LEN], const char *src)
{
    size_t len;

    if (dst == NULL || src == NULL) {
        return 0;
    }

    len = p2p_strnlen_c99(src, P2P_MAX_KEY_LEN);
    if (len >= P2P_MAX_KEY_LEN) {
        return 0;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
    return 1;
}

static int p2p_data_key_valid(const char *key)
{
    size_t len;

    if (key == NULL) {
        return 0;
    }

    len = p2p_strnlen_c99(key, P2P_MAX_KEY_LEN);
    return len < P2P_MAX_KEY_LEN;
}

static void p2p_data_store_spool(p2p_data_t *ctx, const p2p_var_t *var)
{
    if (ctx == NULL || var == NULL) {
        return;
    }
    if (var->data_len > sizeof(ctx->spool.payload)) {
        return;
    }

    memcpy(ctx->spool.key, var->key, sizeof(ctx->spool.key));
    if (var->data_len > 0U) {
        memcpy(ctx->spool.payload, var->data, var->data_len);
    }
    ctx->spool.payload_len = var->data_len;
    ctx->spool.in_use = true;
}

static void p2p_data_notify_subscribers(p2p_data_t *ctx, const p2p_var_t *var)
{
    uint8_t i;
    uint32_t now_ms;

    if (ctx == NULL || var == NULL) {
        return;
    }

    now_ms = ctx->now_ms != NULL ? ctx->now_ms() : 0U;
    for (i = 0U; i < ctx->sub_count; ++i) {
        p2p_subscription_t *sub = &ctx->subs[i];
        if (strncmp(sub->key, var->key, P2P_MAX_KEY_LEN) != 0) {
            continue;
        }
        if (sub->cb == NULL) {
            p2p_data_store_spool(ctx, var);
            continue;
        }
        if (ctx->config.notify_min_interval_ms > 0U &&
            sub->last_notified != 0U &&
            (now_ms - sub->last_notified) < ctx->config.notify_min_interval_ms) {
            continue;
        }
        sub->cb(sub->key, var->data, var->data_len);
        sub->last_notified = now_ms;
    }
}

p2p_data_err_t p2p_data_publish(p2p_data_t *ctx, const char *key,
                                p2p_data_type_t type,
                                const void *value, size_t len)
{
    p2p_var_t *var;
    int idx;

    if (ctx == NULL || key == NULL) {
        return P2P_DATA_ERR_TYPE;
    }
    if (!p2p_data_key_valid(key)) {
        return P2P_DATA_ERR_TYPE;
    }

    idx = p2p_data_find_var_index(ctx, key);
    if (idx >= 0) {
        return p2p_data_update(ctx, key, value, len);
    }

    if (ctx->var_count >= ctx->config.max_vars || ctx->var_count >= P2P_MAX_VARS) {
        return P2P_DATA_ERR_FULL;
    }

    var = &ctx->vars[ctx->var_count++];
    memset(var, 0, sizeof(*var));
    if (!p2p_data_copy_key(var->key, key)) {
        ctx->var_count--;
        return P2P_DATA_ERR_TYPE;
    }
    var->type = (uint8_t)type;
    var->is_public = true;
    var->access = P2P_ACCESS_PUBLIC;
    var->updated_at = ctx->now_ms();
    if (p2p_data_copy_value(var, value, len, ctx->config.compress_data) != P2P_DATA_OK) {
        ctx->var_count--;
        return P2P_DATA_ERR_TYPE;
    }

    p2p_data_notify_subscribers(ctx, var);
    return P2P_DATA_OK;
}

p2p_data_err_t p2p_data_update(p2p_data_t *ctx, const char *key,
                               const void *value, size_t len)
{
    int idx;
    p2p_var_t *var;

    if (ctx == NULL || key == NULL) {
        return P2P_DATA_ERR_TYPE;
    }
    if (!p2p_data_key_valid(key)) {
        return P2P_DATA_ERR_TYPE;
    }

    idx = p2p_data_find_var_index(ctx, key);
    if (idx < 0) {
        return P2P_DATA_ERR_NOT_FOUND;
    }

    var = &ctx->vars[idx];
    if (p2p_data_copy_value(var, value, len, ctx->config.compress_data) != P2P_DATA_OK) {
        return P2P_DATA_ERR_TYPE;
    }

    var->updated_at = ctx->now_ms();
    p2p_data_notify_subscribers(ctx, var);
    return P2P_DATA_OK;
}

p2p_data_err_t p2p_data_subscribe(p2p_data_t *ctx, const uint8_t node_id[32],
                                  const char *key,
                                  void (*cb)(const char *, const void *, size_t))
{
    p2p_subscription_t *sub;
    uint8_t i;

    if (ctx == NULL || node_id == NULL || key == NULL) {
        return P2P_DATA_ERR_TYPE;
    }
    if (!p2p_data_key_valid(key)) {
        return P2P_DATA_ERR_TYPE;
    }

    for (i = 0U; i < ctx->sub_count; ++i) {
        if (memcmp(ctx->subs[i].subscriber, node_id, 32U) == 0 &&
            strncmp(ctx->subs[i].key, key, P2P_MAX_KEY_LEN) == 0) {
            ctx->subs[i].cb = cb;
            return P2P_DATA_OK;
        }
    }

    if (ctx->sub_count >= ctx->config.max_subs || ctx->sub_count >= P2P_MAX_SUBS) {
        return P2P_DATA_ERR_FULL;
    }

    sub = &ctx->subs[ctx->sub_count++];
    memset(sub, 0, sizeof(*sub));
    memcpy(sub->subscriber, node_id, 32U);
    if (!p2p_data_copy_key(sub->key, key)) {
        ctx->sub_count--;
        return P2P_DATA_ERR_TYPE;
    }
    sub->cb = cb;
    return P2P_DATA_OK;
}

p2p_data_err_t p2p_data_unsubscribe(p2p_data_t *ctx, const uint8_t node_id[32],
                                    const char *key)
{
    uint8_t i;

    if (ctx == NULL || node_id == NULL || key == NULL) {
        return P2P_DATA_ERR_TYPE;
    }
    if (!p2p_data_key_valid(key)) {
        return P2P_DATA_ERR_TYPE;
    }

    for (i = 0U; i < ctx->sub_count; ++i) {
        if (memcmp(ctx->subs[i].subscriber, node_id, 32U) == 0 &&
            strncmp(ctx->subs[i].key, key, P2P_MAX_KEY_LEN) == 0) {
            if ((i + 1U) < ctx->sub_count) {
                memmove(&ctx->subs[i],
                        &ctx->subs[i + 1U],
                        (size_t)(ctx->sub_count - i - 1U) * sizeof(ctx->subs[0]));
            }
            ctx->sub_count--;
            memset(&ctx->subs[ctx->sub_count], 0, sizeof(ctx->subs[0]));
            return P2P_DATA_OK;
        }
    }

    return P2P_DATA_ERR_NOT_FOUND;
}

p2p_data_err_t p2p_data_remote_subscribe(p2p_data_t *ctx, const uint8_t node_id[32],
                                         const char *key)
{
    p2p_remote_subscription_t *sub;
    uint8_t i;

    if (ctx == NULL || node_id == NULL || key == NULL) {
        return P2P_DATA_ERR_TYPE;
    }
    if (!p2p_data_key_valid(key)) {
        return P2P_DATA_ERR_TYPE;
    }

    for (i = 0U; i < ctx->remote_sub_count; ++i) {
        if (ctx->remote_subs[i].valid &&
            memcmp(ctx->remote_subs[i].subscriber, node_id, 32U) == 0 &&
            strncmp(ctx->remote_subs[i].key, key, P2P_MAX_KEY_LEN) == 0) {
            ctx->remote_subs[i].last_notified = 0U;
            return P2P_DATA_OK;
        }
    }

    if (ctx->remote_sub_count >= P2P_MAX_SUBS) {
        return P2P_DATA_ERR_FULL;
    }

    sub = &ctx->remote_subs[ctx->remote_sub_count++];
    memset(sub, 0, sizeof(*sub));
    memcpy(sub->subscriber, node_id, 32U);
    if (!p2p_data_copy_key(sub->key, key)) {
        ctx->remote_sub_count--;
        return P2P_DATA_ERR_TYPE;
    }
    sub->valid = true;
    return P2P_DATA_OK;
}

p2p_data_err_t p2p_data_remote_unsubscribe(p2p_data_t *ctx, const uint8_t node_id[32],
                                           const char *key)
{
    uint8_t i;

    if (ctx == NULL || node_id == NULL || key == NULL) {
        return P2P_DATA_ERR_TYPE;
    }
    if (!p2p_data_key_valid(key)) {
        return P2P_DATA_ERR_TYPE;
    }

    for (i = 0U; i < ctx->remote_sub_count; ++i) {
        if (!ctx->remote_subs[i].valid) {
            continue;
        }
        if (memcmp(ctx->remote_subs[i].subscriber, node_id, 32U) == 0 &&
            strncmp(ctx->remote_subs[i].key, key, P2P_MAX_KEY_LEN) == 0) {
            if ((i + 1U) < ctx->remote_sub_count) {
                memmove(&ctx->remote_subs[i],
                        &ctx->remote_subs[i + 1U],
                        (size_t)(ctx->remote_sub_count - i - 1U) * sizeof(ctx->remote_subs[0]));
            }
            ctx->remote_sub_count--;
            memset(&ctx->remote_subs[ctx->remote_sub_count], 0, sizeof(ctx->remote_subs[0]));
            return P2P_DATA_OK;
        }
    }

    return P2P_DATA_ERR_NOT_FOUND;
}

p2p_data_err_t p2p_data_find_subscription(p2p_data_t *ctx, const uint8_t node_id[32],
                                          const char *key, void (**cb)(const char *, const void *, size_t))
{
    uint8_t i;

    if (ctx == NULL || node_id == NULL || key == NULL || cb == NULL) {
        return P2P_DATA_ERR_TYPE;
    }

    for (i = 0U; i < ctx->sub_count; ++i) {
        if (memcmp(ctx->subs[i].subscriber, node_id, 32U) == 0 &&
            strncmp(ctx->subs[i].key, key, P2P_MAX_KEY_LEN) == 0) {
            *cb = ctx->subs[i].cb;
            return P2P_DATA_OK;
        }
    }

    *cb = NULL;
    return P2P_DATA_ERR_NOT_FOUND;
}

p2p_data_err_t p2p_data_list_vars(p2p_data_t *ctx, const uint8_t node_id[32],
                                  void (*cb)(int, const char **, uint8_t))
{
    uint8_t i;
    uint8_t count = 0U;

    (void)node_id;
    if (ctx == NULL || cb == NULL) {
        return P2P_DATA_ERR_TYPE;
    }

    memset(ctx->list_names, 0, sizeof(ctx->list_names));
    for (i = 0U; i < ctx->var_count; ++i) {
        if (ctx->vars[i].is_public && count < P2P_MAX_VARS) {
            ctx->list_names[count++] = ctx->vars[i].key;
        }
    }

    cb(P2P_DATA_OK, ctx->list_names, count);
    return P2P_DATA_OK;
}

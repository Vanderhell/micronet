#include "p2p_security.h"

#include <string.h>

static const uint8_t p2p_security_zero_node[P2P_NODE_KEY_SIZE] = {0};
static const uint8_t p2p_security_zero_group_hash[16] = {0};

static void p2p_security_note_auth_failure(p2p_security_t *ctx)
{
    if (ctx != NULL && ctx->auth_failures < UINT32_MAX) {
        ctx->auth_failures++;
    }
}

static void p2p_security_note_crypto_failure(p2p_security_t *ctx)
{
    if (ctx != NULL && ctx->crypto_failures < UINT32_MAX) {
        ctx->crypto_failures++;
    }
}

uint32_t p2p_security_count_authenticated(const p2p_security_t *ctx)
{
    uint32_t count = 0U;
    uint8_t i;

    if (ctx == NULL) {
        return 0U;
    }

    for (i = 0U; i < P2P_MAX_SESSIONS; ++i) {
        if (ctx->sessions[i].authenticated) {
            count++;
        }
    }
    return count;
}

static p2p_session_t *p2p_security_session_for(p2p_security_t *ctx,
                                               const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE])
{
    uint8_t i;

    if (ctx == NULL || remote_pubkey == NULL) {
        return NULL;
    }

    for (i = 0U; i < P2P_MAX_SESSIONS; ++i) {
        if (memcmp(ctx->sessions[i].remote_pubkey, remote_pubkey, P2P_NODE_KEY_SIZE) == 0) {
            return &ctx->sessions[i];
        }
    }

    return NULL;
}

static p2p_group_secret_t *p2p_security_group_slot_for(p2p_security_t *ctx,
                                                       const uint8_t group_hash[16])
{
    uint8_t i;

    if (ctx == NULL || group_hash == NULL) {
        return NULL;
    }

    for (i = 0U; i < P2P_MAX_GROUPS; ++i) {
        if (ctx->groups[i].valid && memcmp(ctx->groups[i].group_hash, group_hash, 16U) == 0) {
            return &ctx->groups[i];
        }
    }

    for (i = 0U; i < P2P_MAX_GROUPS; ++i) {
        if (!ctx->groups[i].valid) {
            return &ctx->groups[i];
        }
    }

    return NULL;
}

static size_t p2p_security_padded_size(size_t plain_len)
{
    size_t rem = plain_len % P2P_IV_SIZE;
    return rem == 0U ? plain_len + P2P_IV_SIZE : plain_len + (P2P_IV_SIZE - rem);
}

static p2p_sec_err_t p2p_security_encrypt_with_key(const uint8_t key[P2P_SESSION_KEY_SIZE],
                                                   const uint8_t *plain,
                                                   size_t plain_len,
                                                   uint8_t *out,
                                                   size_t *out_len)
{
    mcrypt_aes128_t aes;
    uint8_t iv[P2P_IV_SIZE];
    uint8_t iv_work[P2P_IV_SIZE];
    uint8_t padded[512U];
    size_t padded_len;

    if (key == NULL || (plain == NULL && plain_len > 0U) || out == NULL || out_len == NULL) {
        return P2P_SEC_ERR_BUF;
    }

    padded_len = p2p_security_padded_size(plain_len);
    if (padded_len > sizeof(padded) || *out_len < (P2P_HMAC_SIZE + P2P_IV_SIZE + padded_len)) {
        return P2P_SEC_ERR_BUF;
    }

    if (p2p_security_random_fill(iv, sizeof(iv)) != P2P_SEC_OK) {
        return P2P_SEC_ERR_KEYGEN;
    }
    memcpy(iv_work, iv, sizeof(iv));

    if (plain_len > 0U) {
        memcpy(padded, plain, plain_len);
    }
    memset(padded + plain_len, (uint8_t)(padded_len - plain_len), padded_len - plain_len);

    mcrypt_aes128_init(&aes, key);
    mcrypt_aes128_cbc_encrypt(&aes, iv_work, padded, out + P2P_HMAC_SIZE + P2P_IV_SIZE, (uint32_t)padded_len);
    memcpy(out + P2P_HMAC_SIZE, iv, P2P_IV_SIZE);
    mcrypt_hmac_sha256(key,
                       P2P_SESSION_KEY_SIZE,
                       out + P2P_HMAC_SIZE,
                       (uint32_t)(P2P_IV_SIZE + padded_len),
                       out);
    *out_len = P2P_HMAC_SIZE + P2P_IV_SIZE + padded_len;
    return P2P_SEC_OK;
}

static p2p_sec_err_t p2p_security_decrypt_with_key(const uint8_t key[P2P_SESSION_KEY_SIZE],
                                                   const uint8_t *cipher,
                                                   size_t cipher_len,
                                                   uint8_t *out,
                                                   size_t *out_len)
{
    mcrypt_aes128_t aes;
    uint8_t expected_hmac[P2P_HMAC_SIZE];
    uint8_t iv[P2P_IV_SIZE];
    uint8_t plain[512U];
    size_t enc_len;
    size_t plain_len;
    size_t i;
    uint8_t diff = 0U;
    uint8_t pad;

    if (key == NULL || cipher == NULL || out == NULL || out_len == NULL ||
        cipher_len < (P2P_HMAC_SIZE + P2P_IV_SIZE)) {
        return P2P_SEC_ERR_DECRYPT;
    }

    enc_len = cipher_len - P2P_HMAC_SIZE - P2P_IV_SIZE;
    if ((enc_len % P2P_IV_SIZE) != 0U || enc_len > sizeof(plain)) {
        return P2P_SEC_ERR_DECRYPT;
    }

    mcrypt_hmac_sha256(key,
                       P2P_SESSION_KEY_SIZE,
                       cipher + P2P_HMAC_SIZE,
                       (uint32_t)(cipher_len - P2P_HMAC_SIZE),
                       expected_hmac);
    for (i = 0U; i < P2P_HMAC_SIZE; ++i) {
        diff |= (uint8_t)(expected_hmac[i] ^ cipher[i]);
    }
    if (diff != 0U) {
        return P2P_SEC_ERR_HMAC;
    }

    memcpy(iv, cipher + P2P_HMAC_SIZE, P2P_IV_SIZE);
    mcrypt_aes128_init(&aes, key);
    mcrypt_aes128_cbc_decrypt(&aes,
                              iv,
                              cipher + P2P_HMAC_SIZE + P2P_IV_SIZE,
                              plain,
                              (uint32_t)enc_len);

    plain_len = enc_len;
    pad = plain[plain_len - 1U];
    if (pad == 0U || pad > P2P_IV_SIZE || pad > plain_len) {
        return P2P_SEC_ERR_DECRYPT;
    }
    for (i = 0U; i < pad; ++i) {
        if (plain[plain_len - 1U - i] != pad) {
            return P2P_SEC_ERR_DECRYPT;
        }
    }
    plain_len -= pad;

    if (*out_len < plain_len) {
        return P2P_SEC_ERR_BUF;
    }

    memcpy(out, plain, plain_len);
    *out_len = plain_len;
    return P2P_SEC_OK;
}

p2p_sec_err_t p2p_security_init(p2p_security_t *ctx, const p2p_security_config_t *cfg)
{
    bool loaded = false;

    if (ctx == NULL || cfg == NULL) {
        return P2P_SEC_ERR_KEYGEN;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->store_keys = cfg->store_keys;
    ctx->now_ms = cfg->now_ms;
    ctx->fsm.state = 0U;
    if (ctx->now_ms == NULL) {
        return P2P_SEC_ERR_KEYGEN;
    }

    if (ctx->store_keys) {
        p2p_sec_err_t load_err = p2p_security_load_keys(ctx, &loaded);
        if (load_err != P2P_SEC_OK) {
            return load_err;
        }
    }

    if (!loaded) {
        if (memcmp(cfg->node_privkey, p2p_security_zero_node, P2P_NODE_KEY_SIZE) == 0) {
            if (p2p_security_generate_keypair(ctx->node_privkey, ctx->node_pubkey) != P2P_SEC_OK) {
                return P2P_SEC_ERR_KEYGEN;
            }
        } else if (memcmp(cfg->node_pubkey, p2p_security_zero_node, P2P_NODE_KEY_SIZE) == 0) {
            if (p2p_security_derive_pubkey_from_privkey(cfg->node_privkey, ctx->node_privkey, ctx->node_pubkey) !=
                P2P_SEC_OK) {
                return P2P_SEC_ERR_KEYGEN;
            }
        } else {
            memcpy(ctx->node_privkey, cfg->node_privkey, P2P_NODE_KEY_SIZE);
            memcpy(ctx->node_pubkey, cfg->node_pubkey, P2P_NODE_KEY_SIZE);
        }

        if (ctx->store_keys && p2p_security_store_keys(ctx) != P2P_SEC_OK) {
            return P2P_SEC_ERR_KEYGEN;
        }
    }

    return P2P_SEC_OK;
}

p2p_sec_err_t p2p_security_get_pubkey(p2p_security_t *ctx, uint8_t pubkey[P2P_NODE_KEY_SIZE])
{
    if (ctx == NULL || pubkey == NULL) {
        return P2P_SEC_ERR_KEYGEN;
    }

    memcpy(pubkey, ctx->node_pubkey, P2P_NODE_KEY_SIZE);
    return P2P_SEC_OK;
}

p2p_sec_err_t p2p_security_encrypt(p2p_security_t *ctx, const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE],
                                   const uint8_t *plain, size_t plain_len,
                                   uint8_t *out, size_t *out_len)
{
    p2p_session_t *session = p2p_security_session_for(ctx, remote_pubkey);
    p2p_sec_err_t err;

    if (session == NULL || !session->authenticated) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_NO_SESSION;
    }

    err = p2p_security_encrypt_with_key(session->session_key, plain, plain_len, out, out_len);
    if (err != P2P_SEC_OK) {
        p2p_security_note_crypto_failure(ctx);
        return err;
    }
    return P2P_SEC_OK;
}

p2p_sec_err_t p2p_security_decrypt(p2p_security_t *ctx, const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE],
                                   const uint8_t *cipher, size_t cipher_len,
                                   uint8_t *out, size_t *out_len)
{
    p2p_session_t *session = p2p_security_session_for(ctx, remote_pubkey);
    p2p_sec_err_t err;

    if (session == NULL || !session->authenticated) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_NO_SESSION;
    }

    err = p2p_security_decrypt_with_key(session->session_key, cipher, cipher_len, out, out_len);
    if (err != P2P_SEC_OK) {
        p2p_security_note_crypto_failure(ctx);
    }
    return err;
}

p2p_sec_err_t p2p_security_encrypt_group(p2p_security_t *ctx, uint8_t group_idx,
                                         const uint8_t *plain, size_t plain_len,
                                         uint8_t *out, size_t *out_len)
{
    uint8_t i = 0U;
    p2p_sec_err_t err;

    if (ctx == NULL || out == NULL || out_len == NULL) {
        p2p_security_note_crypto_failure(ctx);
        return P2P_SEC_ERR_NO_GROUP;
    }

    for (i = 0U; i < P2P_MAX_GROUPS; ++i) {
        if (ctx->groups[i].valid) {
            if (group_idx == 0U) {
                err = p2p_security_encrypt_with_key(ctx->groups[i].group_key, plain, plain_len, out, out_len);
                if (err != P2P_SEC_OK) {
                    p2p_security_note_crypto_failure(ctx);
                }
                return err;
            }
            group_idx--;
        }
    }

    p2p_security_note_crypto_failure(ctx);
    return P2P_SEC_ERR_NO_GROUP;
}

p2p_sec_err_t p2p_security_decrypt_group(p2p_security_t *ctx, uint8_t group_idx,
                                         const uint8_t *cipher, size_t cipher_len,
                                         uint8_t *out, size_t *out_len)
{
    uint8_t i = 0U;
    p2p_sec_err_t err;

    if (ctx == NULL || out == NULL || out_len == NULL) {
        p2p_security_note_crypto_failure(ctx);
        return P2P_SEC_ERR_NO_GROUP;
    }

    for (i = 0U; i < P2P_MAX_GROUPS; ++i) {
        if (ctx->groups[i].valid) {
            if (group_idx == 0U) {
                err = p2p_security_decrypt_with_key(ctx->groups[i].group_key, cipher, cipher_len, out, out_len);
                if (err != P2P_SEC_OK) {
                    p2p_security_note_crypto_failure(ctx);
                }
                return err;
            }
            group_idx--;
        }
    }

    p2p_security_note_crypto_failure(ctx);
    return P2P_SEC_ERR_NO_GROUP;
}

p2p_sec_err_t p2p_security_add_group_key(p2p_security_t *ctx,
                                         const uint8_t group_key[P2P_SESSION_KEY_SIZE])
{
    return p2p_security_group_add(ctx, p2p_security_zero_group_hash, group_key);
}

p2p_sec_err_t p2p_security_group_add(p2p_security_t *ctx,
                                    const uint8_t group_hash[16],
                                    const uint8_t group_key[P2P_SESSION_KEY_SIZE])
{
    p2p_group_secret_t *slot;

    if (ctx == NULL || group_hash == NULL || group_key == NULL) {
        return P2P_SEC_ERR_NO_GROUP;
    }

    slot = p2p_security_group_slot_for(ctx, group_hash);
    if (slot == NULL) {
        return P2P_SEC_ERR_NO_GROUP;
    }

    if (slot->valid && memcmp(slot->group_key, group_key, P2P_SESSION_KEY_SIZE) == 0) {
        return P2P_SEC_OK;
    }
    if (slot->valid) {
        return P2P_SEC_ERR_NO_GROUP;
    }

    memcpy(slot->group_hash, group_hash, 16U);
    memcpy(slot->group_key, group_key, P2P_SESSION_KEY_SIZE);
    slot->valid = true;
    ctx->group_count++;

    if (ctx->store_keys) {
        return p2p_security_store_keys(ctx);
    }

    return P2P_SEC_OK;
}

p2p_sec_err_t p2p_security_group_find_key(p2p_security_t *ctx,
                                         const uint8_t group_hash[16],
                                         uint8_t out_group_key[P2P_SESSION_KEY_SIZE])
{
    uint8_t i;

    if (ctx == NULL || group_hash == NULL || out_group_key == NULL) {
        return P2P_SEC_ERR_NO_GROUP;
    }

    for (i = 0U; i < P2P_MAX_GROUPS; ++i) {
        if (ctx->groups[i].valid && memcmp(ctx->groups[i].group_hash, group_hash, 16U) == 0) {
            memcpy(out_group_key, ctx->groups[i].group_key, P2P_SESSION_KEY_SIZE);
            return P2P_SEC_OK;
        }
    }

    return P2P_SEC_ERR_NO_GROUP;
}

p2p_sec_err_t p2p_security_group_remove(p2p_security_t *ctx, const uint8_t group_hash[16])
{
    uint8_t i;

    if (ctx == NULL || group_hash == NULL) {
        return P2P_SEC_ERR_NO_GROUP;
    }

    for (i = 0U; i < P2P_MAX_GROUPS; ++i) {
        if (ctx->groups[i].valid && memcmp(ctx->groups[i].group_hash, group_hash, 16U) == 0) {
            memset(&ctx->groups[i], 0, sizeof(ctx->groups[i]));
            if (ctx->group_count > 0U) {
                ctx->group_count--;
            }
            if (ctx->store_keys) {
                return p2p_security_store_keys(ctx);
            }
            return P2P_SEC_OK;
        }
    }

    return P2P_SEC_ERR_NO_GROUP;
}

void p2p_security_deinit(p2p_security_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->store_keys) {
        (void)p2p_security_store_keys(ctx);
    }

    memset(ctx, 0, sizeof(*ctx));
}

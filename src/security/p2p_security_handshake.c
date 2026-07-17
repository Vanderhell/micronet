#include "p2p_security.h"

#include <string.h>

enum {
    P2P_SEC_STATE_IDLE = 0,
    P2P_SEC_STATE_HANDSHAKE_HELLO = 1,
    P2P_SEC_STATE_HANDSHAKE_VERIFY = 2,
    P2P_SEC_STATE_AUTHENTICATED = 3,
    P2P_SEC_STATE_FAILED = 4
};

static const uint8_t p2p_security_zero_node[P2P_NODE_KEY_SIZE] = {0};
static const uint8_t p2p_security_zero_session[P2P_SESSION_KEY_SIZE] = {0};

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

static int p2p_security_secure_eq(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0U;
    size_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }

    for (i = 0U; i < len; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }

    return diff == 0U;
}

static p2p_session_t *p2p_security_find_session(p2p_security_t *ctx,
                                                const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE])
{
    uint8_t i;

    if (ctx == NULL || remote_pubkey == NULL) {
        return NULL;
    }

    for (i = 0; i < P2P_MAX_SESSIONS; ++i) {
        if (memcmp(ctx->sessions[i].remote_pubkey, remote_pubkey, P2P_NODE_KEY_SIZE) == 0) {
            return &ctx->sessions[i];
        }
    }

    for (i = 0; i < P2P_MAX_SESSIONS; ++i) {
        if (!ctx->sessions[i].authenticated &&
            memcmp(ctx->sessions[i].remote_pubkey,
                   p2p_security_zero_node,
                   P2P_NODE_KEY_SIZE) == 0 &&
            memcmp(ctx->sessions[i].session_key,
                   p2p_security_zero_session,
                   P2P_SESSION_KEY_SIZE) == 0) {
            return &ctx->sessions[i];
        }
    }

    return NULL;
}

static void p2p_security_session_update_authenticated(p2p_security_t *ctx, p2p_session_t *session)
{
    if (ctx == NULL || session == NULL) {
        return;
    }
    if (session->authenticated) {
        return;
    }
    if (session->inbound_verified && session->outbound_verified) {
        session->authenticated = true;
        session->established_at = ctx->now_ms != NULL ? ctx->now_ms() : 0U;
        ctx->fsm.state = P2P_SEC_STATE_AUTHENTICATED;
    }
}

p2p_sec_err_t p2p_security_derive_session_key(const uint8_t local_privkey[P2P_NODE_KEY_SIZE],
                                              const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE],
                                              uint8_t session_key[P2P_SESSION_KEY_SIZE])
{
    static const uint8_t label[] = "micronet_v1";
    uint8_t shared[P2P_NODE_KEY_SIZE];
    uint8_t mac[MCRYPT_HMAC_SHA256_SIZE];

    if (local_privkey == NULL || remote_pubkey == NULL || session_key == NULL) {
        p2p_security_note_auth_failure(NULL);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    if (mdh_shared_secret(local_privkey, remote_pubkey, shared) != 0) {
        p2p_security_note_crypto_failure(NULL);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    mcrypt_hmac_sha256(shared, 32U, label, 11U, mac);
    memcpy(session_key, mac, P2P_SESSION_KEY_SIZE);
    return P2P_SEC_OK;
}

p2p_sec_err_t p2p_security_handshake(p2p_security_t *ctx,
                                     const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE])
{
    p2p_session_t *session;

    if (ctx == NULL || remote_pubkey == NULL) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    if (ctx->now_ms == NULL) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    ctx->fsm.state = P2P_SEC_STATE_HANDSHAKE_HELLO;
    session = p2p_security_find_session(ctx, remote_pubkey);
    if (session == NULL) {
        p2p_security_note_auth_failure(ctx);
        ctx->fsm.state = P2P_SEC_STATE_FAILED;
        return P2P_SEC_ERR_HANDSHAKE;
    }

    if (memcmp(session->remote_pubkey, remote_pubkey, P2P_NODE_KEY_SIZE) != 0) {
        memset(session, 0, sizeof(*session));
        memcpy(session->remote_pubkey, remote_pubkey, P2P_NODE_KEY_SIZE);
    }

    if (p2p_security_derive_session_key(ctx->node_privkey,
                                        remote_pubkey,
                                        session->session_key) != P2P_SEC_OK) {
        p2p_security_note_crypto_failure(ctx);
        ctx->fsm.state = P2P_SEC_STATE_FAILED;
        return P2P_SEC_ERR_HANDSHAKE;
    }

    /* keep existing authenticated state if session already established */
    ctx->fsm.state = P2P_SEC_STATE_HANDSHAKE_VERIFY;
    return P2P_SEC_OK;
}

bool p2p_security_is_authenticated(p2p_security_t *ctx,
                                   const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE])
{
    p2p_session_t *session = p2p_security_find_session(ctx, remote_pubkey);
    return session != NULL && session->authenticated;
}

p2p_sec_err_t p2p_security_build_hello_mac(p2p_security_t *ctx,
                                          const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE],
                                          uint8_t out_mac[P2P_HMAC_SIZE])
{
    p2p_session_t *session;

    if (ctx == NULL || remote_pubkey == NULL || out_mac == NULL) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    if (p2p_security_handshake(ctx, remote_pubkey) != P2P_SEC_OK) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    session = p2p_security_find_session(ctx, remote_pubkey);
    if (session == NULL) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    mcrypt_hmac_sha256(session->session_key,
                       P2P_SESSION_KEY_SIZE,
                       ctx->node_pubkey,
                       P2P_NODE_KEY_SIZE,
                       out_mac);
    return P2P_SEC_OK;
}

p2p_sec_err_t p2p_security_verify_hello_mac(p2p_security_t *ctx,
                                            const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE],
                                            const uint8_t mac[P2P_HMAC_SIZE])
{
    p2p_session_t *session;
    uint8_t expected[P2P_HMAC_SIZE];

    if (ctx == NULL || remote_pubkey == NULL || mac == NULL) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    if (p2p_security_handshake(ctx, remote_pubkey) != P2P_SEC_OK) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    session = p2p_security_find_session(ctx, remote_pubkey);
    if (session == NULL) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    mcrypt_hmac_sha256(session->session_key,
                       P2P_SESSION_KEY_SIZE,
                       remote_pubkey,
                       P2P_NODE_KEY_SIZE,
                       expected);
    if (!p2p_security_secure_eq(expected, mac, P2P_HMAC_SIZE)) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    session->inbound_verified = true;
    p2p_security_session_update_authenticated(ctx, session);
    return P2P_SEC_OK;
}

p2p_sec_err_t p2p_security_mark_outbound_verified(p2p_security_t *ctx,
                                                 const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE])
{
    p2p_session_t *session;

    if (ctx == NULL || remote_pubkey == NULL) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    if (p2p_security_handshake(ctx, remote_pubkey) != P2P_SEC_OK) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    session = p2p_security_find_session(ctx, remote_pubkey);
    if (session == NULL) {
        p2p_security_note_auth_failure(ctx);
        return P2P_SEC_ERR_HANDSHAKE;
    }

    session->outbound_verified = true;
    p2p_security_session_update_authenticated(ctx, session);
    return P2P_SEC_OK;
}

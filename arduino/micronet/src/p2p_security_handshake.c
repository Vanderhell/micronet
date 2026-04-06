#include "p2p_security.h"

#include <string.h>
#include <time.h>

enum {
    P2P_SEC_STATE_IDLE = 0,
    P2P_SEC_STATE_HANDSHAKE_HELLO = 1,
    P2P_SEC_STATE_HANDSHAKE_VERIFY = 2,
    P2P_SEC_STATE_AUTHENTICATED = 3,
    P2P_SEC_STATE_FAILED = 4
};

static const uint8_t p2p_security_zero_node[P2P_NODE_KEY_SIZE] = {0};
static const uint8_t p2p_security_zero_session[P2P_SESSION_KEY_SIZE] = {0};

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

p2p_sec_err_t p2p_security_derive_session_key(const uint8_t local_privkey[P2P_NODE_KEY_SIZE],
                                              const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE],
                                              uint8_t session_key[P2P_SESSION_KEY_SIZE])
{
    static const uint8_t label[] = "micronet_v1";
    uint8_t shared[P2P_NODE_KEY_SIZE];
    uint8_t mac[MCRYPT_HMAC_SHA256_SIZE];

    if (local_privkey == NULL || remote_pubkey == NULL || session_key == NULL) {
        return P2P_SEC_ERR_HANDSHAKE;
    }

    if (mdh_shared_secret(local_privkey, remote_pubkey, shared) != 0) {
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
    uint8_t verify_mac[MCRYPT_HMAC_SHA256_SIZE];

    if (ctx == NULL || remote_pubkey == NULL) {
        return P2P_SEC_ERR_HANDSHAKE;
    }

    ctx->fsm.state = P2P_SEC_STATE_HANDSHAKE_HELLO;
    session = p2p_security_find_session(ctx, remote_pubkey);
    if (session == NULL) {
        ctx->fsm.state = P2P_SEC_STATE_FAILED;
        return P2P_SEC_ERR_HANDSHAKE;
    }

    memset(session, 0, sizeof(*session));
    memcpy(session->remote_pubkey, remote_pubkey, P2P_NODE_KEY_SIZE);

    if (p2p_security_derive_session_key(ctx->node_privkey,
                                        remote_pubkey,
                                        session->session_key) != P2P_SEC_OK) {
        ctx->fsm.state = P2P_SEC_STATE_FAILED;
        return P2P_SEC_ERR_HANDSHAKE;
    }

    ctx->fsm.state = P2P_SEC_STATE_HANDSHAKE_VERIFY;
    mcrypt_hmac_sha256(session->session_key,
                       P2P_SESSION_KEY_SIZE,
                       ctx->node_pubkey,
                       P2P_NODE_KEY_SIZE,
                       verify_mac);
    if (memcmp(verify_mac, p2p_security_zero_node, 1U) == 0) {
        ctx->fsm.state = P2P_SEC_STATE_FAILED;
        return P2P_SEC_ERR_HANDSHAKE;
    }

    session->authenticated = true;
    session->established_at = (uint32_t)time(NULL);
    ctx->fsm.state = P2P_SEC_STATE_AUTHENTICATED;
    return P2P_SEC_OK;
}

bool p2p_security_is_authenticated(p2p_security_t *ctx,
                                   const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE])
{
    p2p_session_t *session = p2p_security_find_session(ctx, remote_pubkey);
    return session != NULL && session->authenticated;
}

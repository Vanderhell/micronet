#include "mtest.h"

#include "micronet.h"
#include "micronet_internal.h"

#include <string.h>

static int api_online_count;
static int api_offline_count;
static int api_custom_count;
static mnet_err_t api_request_err;
static int api_request_value;
static int api_subscribe_count;
static int api_custom_handler_count;
static int api_broadcast_handler_count;
static int api_nested_request_stage;
static int api_nested_request_second_value;
static int api_list_err;
static uint8_t api_list_count;
static const char *api_list_names[MNET_MAX_NODES];
static int api_query_err;
static uint8_t api_query_count;
static uint8_t api_query_first_row[256];
static size_t api_query_first_row_len;
static int api_metrics_err;
static mnet_metrics_t api_metrics_value;
static int api_chain_stage;
static uint8_t api_self_id[32];
static uint8_t api_nested_node_id[32];

static void api_auth_mutual(p2p_security_t *local, p2p_security_t *remote)
{
    uint8_t hello_local[P2P_HMAC_SIZE];
    uint8_t hello_remote[P2P_HMAC_SIZE];
    uint8_t ack_local[P2P_HMAC_SIZE];
    uint8_t ack_remote[P2P_HMAC_SIZE];

    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(local, remote->node_pubkey, hello_local));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_verify_hello_mac(remote, local->node_pubkey, hello_local));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(remote, local->node_pubkey, ack_remote));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_verify_hello_mac(local, remote->node_pubkey, ack_remote));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_mark_outbound_verified(local, remote->node_pubkey));

    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(remote, local->node_pubkey, hello_remote));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_verify_hello_mac(local, remote->node_pubkey, hello_remote));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(local, remote->node_pubkey, ack_local));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_verify_hello_mac(remote, local->node_pubkey, ack_local));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_mark_outbound_verified(remote, local->node_pubkey));
}

static void api_on_online(const uint8_t node_id[32])
{
    api_online_count++;
    memcpy(api_self_id, node_id, 32U);
}

static void api_on_offline(const uint8_t node_id[32])
{
    api_offline_count++;
    memcpy(api_self_id, node_id, 32U);
}

static void api_on_custom_msg(const mnet_message_t *msg)
{
    if (msg != NULL) {
        api_custom_count++;
    }
}

static void api_request_cb(mnet_err_t err, const void *value, size_t len)
{
    api_request_err = err;
    if (value != NULL && len == sizeof(api_request_value)) {
        memcpy(&api_request_value, value, len);
    }
}

static void api_subscribe_cb(const char *key, const void *value, size_t len)
{
    (void)key;
    if (value != NULL && len > 0U) {
        api_subscribe_count++;
    }
}

static void api_custom_handler(const uint8_t src[32], const uint8_t *payload, size_t len)
{
    (void)src;
    if (payload != NULL && len > 0U) {
        api_custom_handler_count++;
    }
}

static void api_broadcast_handler(const uint8_t src[32], const uint8_t *payload, size_t len)
{
    (void)src;
    if (payload != NULL && len > 0U) {
        api_broadcast_handler_count++;
    }
}

static void api_nested_request_cb(mnet_err_t err, const void *value, size_t len)
{
    int next_value;

    MTEST_ASSERT_EQ(MNET_OK, err);
    MTEST_ASSERT_EQ((int)sizeof(int), (int)len);
    memcpy(&next_value, value, sizeof(next_value));

    if (api_nested_request_stage == 0) {
        api_nested_request_stage = 1;
        MTEST_ASSERT_EQ(MNET_OK, mnet_request(api_nested_node_id, "second", api_nested_request_cb));
        return;
    }

    api_nested_request_stage = 2;
    api_nested_request_second_value = next_value;
}

static void api_list_vars_cb(mnet_err_t err, const char **names, uint8_t count)
{
    uint8_t i;

    api_list_err = err;
    api_list_count = count;
    for (i = 0U; i < count; ++i) {
        api_list_names[i] = names[i];
    }
}

static void api_query_cb(mnet_err_t err, const mnet_row_t *rows, uint8_t count)
{
    api_query_err = err;
    api_query_count = count;
    api_query_first_row_len = 0U;
    if (rows != NULL && count > 0U) {
        api_query_first_row_len = rows[0].len;
        memcpy(api_query_first_row, rows[0].bytes, rows[0].len);
    }
}

static void api_metrics_cb(mnet_err_t err, const mnet_metrics_t *metrics)
{
    api_metrics_err = err;
    if (metrics != NULL) {
        api_metrics_value = *metrics;
    }
}

static void api_fill_peer_id(uint8_t out[32], uint8_t seed)
{
    uint8_t i;

    for (i = 0U; i < 32U; ++i) {
        out[i] = (uint8_t)(seed + i);
    }
}

static void api_prime_authenticated_peer(mnet_context_t *ctx,
                                         const uint8_t peer_id[32],
                                         const uint8_t ip[4],
                                         uint16_t port)
{
    p2p_endpoint_t *ep;
    p2p_session_t *session = NULL;
    uint8_t i;

    MTEST_ASSERT_NOT_NULL(ctx);
    MTEST_ASSERT_NOT_NULL(peer_id);
    MTEST_ASSERT_NOT_NULL(ip);

    for (i = 0U; i < P2P_MAX_SESSIONS; ++i) {
        if (memcmp(ctx->security.sessions[i].remote_pubkey, peer_id, 32U) == 0 ||
            memcmp(ctx->security.sessions[i].remote_pubkey, (uint8_t[32]){0}, 32U) == 0) {
            session = &ctx->security.sessions[i];
            break;
        }
    }
    MTEST_ASSERT_NOT_NULL(session);
    memset(session, 0, sizeof(*session));
    memcpy(session->remote_pubkey, peer_id, 32U);
    MTEST_ASSERT_EQ(P2P_SEC_OK,
                    p2p_security_derive_session_key(ctx->security.node_privkey,
                                                     peer_id,
                                                     session->session_key));
    session->inbound_verified = true;
    session->outbound_verified = true;
    session->authenticated = true;

    ep = NULL;
    for (i = 0U; i < ctx->protocol.endpoint_count; ++i) {
        if (ctx->protocol.endpoints[i].valid &&
            memcmp(ctx->protocol.endpoints[i].node_id, peer_id, 32U) == 0) {
            ep = &ctx->protocol.endpoints[i];
            break;
        }
    }
    if (ep == NULL) {
        for (i = 0U; i < ctx->protocol.endpoint_count; ++i) {
            if (!ctx->protocol.endpoints[i].valid) {
                ep = &ctx->protocol.endpoints[i];
                break;
            }
        }
    }
    if (ep == NULL && ctx->protocol.endpoint_count < (uint8_t)(sizeof(ctx->protocol.endpoints) /
                                                               sizeof(ctx->protocol.endpoints[0]))) {
        ep = &ctx->protocol.endpoints[ctx->protocol.endpoint_count++];
    }
    MTEST_ASSERT_NOT_NULL(ep);
    memset(ep, 0, sizeof(*ep));
    memcpy(ep->node_id, peer_id, 32U);
    ep->valid = true;
    ep->state = P2P_ENDPOINT_AUTHENTICATED;
    memcpy(ep->local_ip, ip, 4U);
    memcpy(ep->public_ip, ip, 4U);
    ep->local_port = port;
    ep->public_port = port;
}

static void api_chain_metrics_cb(mnet_err_t err, const mnet_metrics_t *metrics)
{
    MTEST_ASSERT_EQ(MNET_OK, err);
    MTEST_ASSERT_NOT_NULL(metrics);
    MTEST_ASSERT_GT(metrics->free_heap, 0);
    api_chain_stage = 3;
}

static void api_chain_query_cb(mnet_err_t err, const mnet_row_t *rows, uint8_t count)
{
    (void)rows;
    (void)count;

    MTEST_ASSERT_EQ(MNET_ERR_NOT_FOUND, err);
    api_chain_stage = 2;
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_metrics(api_nested_node_id, api_chain_metrics_cb));
}

static void api_chain_list_cb(mnet_err_t err, const char **names, uint8_t count)
{
    (void)names;

    MTEST_ASSERT_EQ(MNET_OK, err);
    MTEST_ASSERT_EQ(2, (int)count);
    api_chain_stage = 1;
    MTEST_ASSERT_EQ(MNET_ERR_NOT_FOUND,
                    mnet_query(api_nested_node_id, "table_missing", NULL, api_chain_query_cb));
}

static mnet_config_t api_config(void)
{
    mnet_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.network_mode = MNET_MODE_LAN_ONLY;
    cfg.stun_enabled = false;
    cfg.heartbeat_ms = 5000U;
    cfg.offline_timeout_ms = 15000U;
    cfg.retry_interval_ms = 2000U;
    cfg.retry_count = 3U;
    cfg.max_nodes = 16U;
    cfg.max_vars = 16U;
    cfg.max_pending = 8U;
    cfg.on_node_online = api_on_online;
    cfg.on_node_offline = api_on_offline;
    cfg.on_custom_msg = api_on_custom_msg;
    return cfg;
}

MTEST(test_api_init_deinit)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];
    static const uint8_t zero32[32] = {0};

    api_online_count = 0;
    api_offline_count = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));
    MTEST_ASSERT_TRUE(memcmp(node_id, zero32, sizeof(node_id)) != 0);
    MTEST_ASSERT_EQ(1, api_online_count);
    mnet_deinit();
}

MTEST(test_api_publish_request_end_to_end)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];
    int value = 1234;

    api_request_err = MNET_ERR_INTERNAL;
    api_request_value = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));
    MTEST_ASSERT_EQ(MNET_OK, mnet_publish("val", &value, sizeof(value)));
    MTEST_ASSERT_EQ(MNET_OK, mnet_request(node_id, "val", api_request_cb));
    MTEST_ASSERT_EQ(MNET_OK, api_request_err);
    MTEST_ASSERT_EQ(value, api_request_value);
    mnet_deinit();
}

MTEST(test_api_remote_request_no_auth_fails)
{
    mnet_config_t cfg = api_config();
    uint8_t remote_id[32];

    memset(remote_id, 0xAA, sizeof(remote_id));
    api_request_err = MNET_ERR_INTERNAL;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_ERR_OFFLINE, mnet_request(remote_id, "val", api_request_cb));
    MTEST_ASSERT_EQ(MNET_ERR_INTERNAL, api_request_err);
    mnet_deinit();
}

MTEST(test_api_remote_request_pending_endpoint_fails)
{
    mnet_config_t cfg = api_config();
    mnet_context_t *ctx;
    p2p_security_t remote_sec;
    p2p_security_config_t remote_cfg;
    uint8_t remote_id[32];
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};

    api_request_err = MNET_ERR_INTERNAL;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    ctx = mnet_internal_context();
    MTEST_ASSERT_NOT_NULL(ctx);

    memset(&remote_cfg, 0, sizeof(remote_cfg));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    memcpy(remote_id, remote_sec.node_pubkey, sizeof(remote_id));
    api_auth_mutual(&ctx->security, &remote_sec);
    MTEST_ASSERT_TRUE(p2p_security_is_authenticated(&ctx->security, remote_id));

    ctx->protocol.endpoints[0].valid = true;
    ctx->protocol.endpoints[0].state = P2P_ENDPOINT_PENDING;
    memcpy(ctx->protocol.endpoints[0].node_id, remote_id, 32U);
    memcpy(ctx->protocol.endpoints[0].local_ip, ip, 4U);
    ctx->protocol.endpoints[0].local_port = 45500U;
    ctx->protocol.endpoint_count = 1U;

    MTEST_ASSERT_EQ(MNET_ERR_OFFLINE, mnet_request(remote_id, "val", api_request_cb));
    MTEST_ASSERT_EQ(MNET_ERR_INTERNAL, api_request_err);

    p2p_security_deinit(&remote_sec);
    mnet_deinit();
}

MTEST(test_api_remote_request_and_response_flow)
{
    mnet_config_t cfg = api_config();
    mnet_context_t *ctx;
    p2p_security_t remote_sec;
    p2p_security_config_t remote_cfg;
    uint8_t remote_id[32];
    int value = 2026;
    p2p_message_t resp;
    uint8_t plain[1024];
    size_t plain_len = sizeof(plain);
    uint8_t cipher[1024];
    size_t cipher_len = sizeof(cipher);
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};

    api_request_err = MNET_ERR_INTERNAL;
    api_request_value = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    ctx = mnet_internal_context();
    MTEST_ASSERT_NOT_NULL(ctx);

    memset(&remote_cfg, 0, sizeof(remote_cfg));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    memcpy(remote_id, remote_sec.node_pubkey, sizeof(remote_id));
    api_auth_mutual(&ctx->security, &remote_sec);

    ctx->protocol.endpoints[0].valid = true;
    ctx->protocol.endpoints[0].state = P2P_ENDPOINT_AUTHENTICATED;
    memcpy(ctx->protocol.endpoints[0].node_id, remote_id, 32U);
    memcpy(ctx->protocol.endpoints[0].local_ip, ip, 4U);
    ctx->protocol.endpoints[0].local_port = 45501U;
    ctx->protocol.endpoint_count = 1U;

    /* Remote request must not perform local lookup. */
    MTEST_ASSERT_EQ(MNET_OK, mnet_publish("val", &value, sizeof(value)));
    MTEST_ASSERT_EQ(MNET_OK, mnet_request(remote_id, "val", api_request_cb));
    MTEST_ASSERT_EQ(MNET_ERR_INTERNAL, api_request_err);

    /* Malformed payload with matching request_id/src must fail fast and clear slot. */
    memset(&resp, 0, sizeof(resp));
    resp.type = P2P_MSG_DATA_RESPONSE;
    resp.msg_id = ctx->request_id;
    memcpy(resp.src, remote_id, 32U);
    memcpy(resp.dst, ctx->security.node_pubkey, 32U);
    resp.payload[0] = 0U;
    resp.payload[1] = 0U;
    resp.payload[2] = 4U;
    resp.payload_len = 3U; /* claims 4 bytes but provides none */
    plain_len = sizeof(plain);
    cipher_len = sizeof(cipher);
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&resp, plain, &plain_len));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt(&remote_sec, ctx->security.node_pubkey, plain, plain_len, cipher, &cipher_len));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&ctx->protocol, cipher, cipher_len, ip, 45501U, P2P_PACKET_FLAG_ENCRYPTED));
    MTEST_ASSERT_EQ(MNET_ERR_PROTOCOL, api_request_err);
    MTEST_ASSERT_TRUE(!ctx->request_inflight);

    /* Late valid response after protocol-error must not invoke callback again. */
    api_request_err = MNET_ERR_INTERNAL;
    api_request_value = 0;
    memset(&resp, 0, sizeof(resp));
    resp.type = P2P_MSG_DATA_RESPONSE;
    resp.msg_id = ctx->request_id;
    memcpy(resp.src, remote_id, 32U);
    memcpy(resp.dst, ctx->security.node_pubkey, 32U);
    resp.payload[0] = 0U;
    resp.payload[1] = 0U;
    resp.payload[2] = (uint8_t)sizeof(value);
    memcpy(resp.payload + 3U, &value, sizeof(value));
    resp.payload_len = 3U + sizeof(value);
    plain_len = sizeof(plain);
    cipher_len = sizeof(cipher);
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&resp, plain, &plain_len));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt(&remote_sec, ctx->security.node_pubkey, plain, plain_len, cipher, &cipher_len));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&ctx->protocol, cipher, cipher_len, ip, 45501U, P2P_PACKET_FLAG_ENCRYPTED));
    MTEST_ASSERT_EQ(MNET_ERR_INTERNAL, api_request_err);

    /* Start a new remote request after protocol-error cleared the slot. */
    api_request_err = MNET_ERR_INTERNAL;
    api_request_value = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_request(remote_id, "val", api_request_cb));
    MTEST_ASSERT_TRUE(ctx->request_inflight);
    MTEST_ASSERT_EQ(MNET_ERR_INTERNAL, api_request_err);

    /* If source is no longer authenticated, dispatch must reject and callback must not fire. */
    {
        p2p_session_t *s = NULL;
        uint8_t i;
        for (i = 0U; i < P2P_MAX_SESSIONS; ++i) {
            if (memcmp(ctx->security.sessions[i].remote_pubkey, remote_id, 32U) == 0) {
                s = &ctx->security.sessions[i];
                break;
            }
        }
        MTEST_ASSERT_NOT_NULL(s);
        s->authenticated = false;
        s->inbound_verified = false;
        s->outbound_verified = false;
    }
    memset(&resp, 0, sizeof(resp));
    resp.type = P2P_MSG_DATA_RESPONSE;
    resp.msg_id = ctx->request_id;
    memcpy(resp.src, remote_id, 32U);
    memcpy(resp.dst, ctx->security.node_pubkey, 32U);
    resp.payload[0] = 0U;
    resp.payload[1] = 0U;
    resp.payload[2] = (uint8_t)sizeof(value);
    memcpy(resp.payload + 3U, &value, sizeof(value));
    resp.payload_len = 3U + sizeof(value);
    plain_len = sizeof(plain);
    cipher_len = sizeof(cipher);
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&resp, plain, &plain_len));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt(&remote_sec, ctx->security.node_pubkey, plain, plain_len, cipher, &cipher_len));
    MTEST_ASSERT_EQ(P2P_PROTO_ERR_PARSE, p2p_protocol_on_packet(&ctx->protocol, cipher, cipher_len, ip, 45501U, P2P_PACKET_FLAG_ENCRYPTED));
    MTEST_ASSERT_EQ(MNET_ERR_INTERNAL, api_request_err);

    /* Restore auth for remaining cases. */
    api_auth_mutual(&ctx->security, &remote_sec);

    /* Unknown request_id must be ignored (request remains in-flight). */
    memset(&resp, 0, sizeof(resp));
    resp.type = P2P_MSG_DATA_RESPONSE;
    resp.msg_id = (uint16_t)(ctx->request_id + 1U);
    memcpy(resp.src, remote_id, 32U);
    memcpy(resp.dst, ctx->security.node_pubkey, 32U);
    resp.payload[0] = 0U;
    resp.payload[1] = 0U;
    resp.payload[2] = (uint8_t)sizeof(value);
    memcpy(resp.payload + 3U, &value, sizeof(value));
    resp.payload_len = 3U + sizeof(value);
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&resp, plain, &plain_len));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt(&remote_sec, ctx->security.node_pubkey, plain, plain_len, cipher, &cipher_len));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&ctx->protocol, cipher, cipher_len, ip, 45501U, P2P_PACKET_FLAG_ENCRYPTED));
    MTEST_ASSERT_EQ(MNET_ERR_INTERNAL, api_request_err);

    /* Wrong-node response must be ignored. */
    memset(&resp, 0, sizeof(resp));
    resp.type = P2P_MSG_DATA_RESPONSE;
    resp.msg_id = ctx->request_id;
    memset(resp.src, 0xEE, 32U);
    memcpy(resp.dst, ctx->security.node_pubkey, 32U);
    resp.payload[0] = 0U;
    resp.payload[1] = 0U;
    resp.payload[2] = (uint8_t)sizeof(value);
    memcpy(resp.payload + 3U, &value, sizeof(value));
    resp.payload_len = 3U + sizeof(value);
    plain_len = sizeof(plain);
    cipher_len = sizeof(cipher);
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&resp, plain, &plain_len));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt(&remote_sec, ctx->security.node_pubkey, plain, plain_len, cipher, &cipher_len));
    MTEST_ASSERT_EQ(P2P_PROTO_ERR_PARSE, p2p_protocol_on_packet(&ctx->protocol, cipher, cipher_len, ip, 45501U, P2P_PACKET_FLAG_ENCRYPTED));
    MTEST_ASSERT_EQ(MNET_ERR_INTERNAL, api_request_err);

    /* Valid response invokes callback exactly once. */
    memset(&resp, 0, sizeof(resp));
    resp.type = P2P_MSG_DATA_RESPONSE;
    resp.msg_id = ctx->request_id;
    memcpy(resp.src, remote_id, 32U);
    memcpy(resp.dst, ctx->security.node_pubkey, 32U);
    resp.payload[0] = 0U;
    resp.payload[1] = 0U;
    resp.payload[2] = (uint8_t)sizeof(value);
    memcpy(resp.payload + 3U, &value, sizeof(value));
    resp.payload_len = 3U + sizeof(value);
    plain_len = sizeof(plain);
    cipher_len = sizeof(cipher);
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&resp, plain, &plain_len));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt(&remote_sec, ctx->security.node_pubkey, plain, plain_len, cipher, &cipher_len));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&ctx->protocol, cipher, cipher_len, ip, 45501U, P2P_PACKET_FLAG_ENCRYPTED));
    MTEST_ASSERT_EQ(MNET_OK, api_request_err);
    MTEST_ASSERT_EQ(value, api_request_value);

    /* Replay must not invoke again. */
    api_request_err = MNET_ERR_INTERNAL;
    api_request_value = 0;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&ctx->protocol, cipher, cipher_len, ip, 45501U, P2P_PACKET_FLAG_ENCRYPTED));
    MTEST_ASSERT_EQ(MNET_ERR_INTERNAL, api_request_err);

    p2p_security_deinit(&remote_sec);
    mnet_deinit();
}

MTEST(test_api_remote_request_timeout_frees_slot)
{
    mnet_config_t cfg = api_config();
    mnet_context_t *ctx;
    p2p_security_t remote_sec;
    p2p_security_config_t remote_cfg;
    uint8_t remote_id[32];
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};

    api_request_err = MNET_ERR_INTERNAL;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    ctx = mnet_internal_context();
    MTEST_ASSERT_NOT_NULL(ctx);

    memset(&remote_cfg, 0, sizeof(remote_cfg));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    memcpy(remote_id, remote_sec.node_pubkey, sizeof(remote_id));
    api_auth_mutual(&ctx->security, &remote_sec);

    ctx->protocol.endpoints[0].valid = true;
    ctx->protocol.endpoints[0].state = P2P_ENDPOINT_AUTHENTICATED;
    memcpy(ctx->protocol.endpoints[0].node_id, remote_id, 32U);
    memcpy(ctx->protocol.endpoints[0].local_ip, ip, 4U);
    ctx->protocol.endpoints[0].local_port = 45502U;
    ctx->protocol.endpoint_count = 1U;

    MTEST_ASSERT_EQ(MNET_OK, mnet_request(remote_id, "val", api_request_cb));
    MTEST_ASSERT_TRUE(ctx->request_inflight);

    /* Second in-flight request must fail deterministically. */
    MTEST_ASSERT_EQ(MNET_ERR_FULL, mnet_request(remote_id, "val", api_request_cb));

    ctx->request_deadline_ms = 0U;
    MTEST_ASSERT_EQ(MNET_OK, mnet_tick());
    MTEST_ASSERT_TRUE(!ctx->request_inflight);
    MTEST_ASSERT_EQ(MNET_ERR_TIMEOUT, api_request_err);

    /* Late response after timeout must not invoke callback. */
    {
        int value = 999;
        p2p_message_t resp;
        uint8_t plain[1024];
        size_t plain_len = sizeof(plain);
        uint8_t cipher[1024];
        size_t cipher_len = sizeof(cipher);
        api_request_err = MNET_ERR_INTERNAL;
        memset(&resp, 0, sizeof(resp));
        resp.type = P2P_MSG_DATA_RESPONSE;
        resp.msg_id = ctx->request_id;
        memcpy(resp.src, remote_id, 32U);
        memcpy(resp.dst, ctx->security.node_pubkey, 32U);
        resp.payload[0] = 0U;
        resp.payload[1] = 0U;
        resp.payload[2] = (uint8_t)sizeof(value);
        memcpy(resp.payload + 3U, &value, sizeof(value));
        resp.payload_len = 3U + sizeof(value);
        MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&resp, plain, &plain_len));
        MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt(&remote_sec, ctx->security.node_pubkey, plain, plain_len, cipher, &cipher_len));
        MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&ctx->protocol, cipher, cipher_len, ip, 45502U, P2P_PACKET_FLAG_ENCRYPTED));
        MTEST_ASSERT_EQ(MNET_ERR_INTERNAL, api_request_err);
    }

    /* New request can start after timeout frees the slot. */
    api_request_err = MNET_ERR_INTERNAL;
    MTEST_ASSERT_EQ(MNET_OK, mnet_request(remote_id, "val", api_request_cb));
    MTEST_ASSERT_TRUE(ctx->request_inflight);

    p2p_security_deinit(&remote_sec);
    mnet_deinit();
}

MTEST(test_api_subscribe_end_to_end)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];
    int value = 1;

    api_subscribe_count = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));
    MTEST_ASSERT_EQ(MNET_OK, mnet_publish("val", &value, sizeof(value)));
    MTEST_ASSERT_EQ(MNET_OK, mnet_subscribe(node_id, "val", api_subscribe_cb));
    value = 2;
    MTEST_ASSERT_EQ(MNET_OK, mnet_update("val", &value, sizeof(value)));
    MTEST_ASSERT_EQ(1, api_subscribe_count);
    mnet_deinit();
}

MTEST(test_api_group_end_to_end)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];
    uint8_t group_hash[16];
    uint8_t group_key[16];

    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));
    MTEST_ASSERT_EQ(MNET_OK, mnet_group_create(group_hash, group_key));
    MTEST_ASSERT_TRUE(mnet_group_is_member(node_id, group_hash));
    mnet_deinit();
}

MTEST(test_api_lan_defaults_and_peer_table)
{
    mnet_config_t cfg = api_config();
    uint8_t peer1[32];
    uint8_t peer2[32];
    uint8_t ip1[4] = {192U, 168U, 1U, 10U};
    uint8_t ip2[4] = {192U, 168U, 1U, 11U};
    mnet_peer_info_t peers[4];
    uint8_t count = 0U;

    api_fill_peer_id(peer1, 0x11U);
    api_fill_peer_id(peer2, 0x22U);

    MTEST_ASSERT_EQ(MNET_MODE_LAN_ONLY, cfg.network_mode);
    MTEST_ASSERT_TRUE(!cfg.stun_enabled);
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_add_ip(peer1, ip1, 33333U));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_add_ip(peer2, ip2, 33334U));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_list(peers, (uint8_t)(sizeof(peers) / sizeof(peers[0])), &count));
    MTEST_ASSERT_EQ(2, (int)count);
    MTEST_ASSERT_MEM_EQ(peer1, peers[0].node_id, 32U);
    MTEST_ASSERT_MEM_EQ(ip1, peers[0].ip, 4U);
    MTEST_ASSERT_EQ(33333, (int)peers[0].port);
    MTEST_ASSERT_MEM_EQ(peer2, peers[1].node_id, 32U);
    MTEST_ASSERT_MEM_EQ(ip2, peers[1].ip, 4U);
    MTEST_ASSERT_EQ(33334, (int)peers[1].port);
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_remove(peer1));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_list(peers, (uint8_t)(sizeof(peers) / sizeof(peers[0])), &count));
    MTEST_ASSERT_EQ(1, (int)count);
    MTEST_ASSERT_MEM_EQ(peer2, peers[0].node_id, 32U);
    mnet_deinit();
}

MTEST(test_api_placeholder_peer_id_is_deterministic)
{
    mnet_config_t cfg = api_config();
    uint8_t ip[4] = {127U, 0U, 0U, 1U};
    mnet_peer_info_t peers[2];
    uint8_t count = 0U;
    uint8_t first_id[32];
    uint8_t second_id[32];

    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_add_ip(NULL, ip, 33337U));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_list(peers, (uint8_t)(sizeof(peers) / sizeof(peers[0])), &count));
    MTEST_ASSERT_EQ(1, (int)count);
    memcpy(first_id, peers[0].node_id, sizeof(first_id));
    MTEST_ASSERT_TRUE(memcmp(first_id, (uint8_t[32]){0}, 32U) != 0);
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_remove(first_id));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_add_ip(NULL, ip, 33337U));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_list(peers, (uint8_t)(sizeof(peers) / sizeof(peers[0])), &count));
    MTEST_ASSERT_EQ(1, (int)count);
    memcpy(second_id, peers[0].node_id, sizeof(second_id));
    MTEST_ASSERT_MEM_EQ(first_id, second_id, 32U);
    mnet_deinit();
}

MTEST(test_api_group_send_filters_peers)
{
    mnet_config_t cfg = api_config();
    mnet_context_t *ctx;
    uint8_t peer1[32];
    uint8_t peer2[32];
    uint8_t peer1_ip[4] = {127U, 0U, 0U, 1U};
    uint8_t peer2_ip[4] = {127U, 0U, 0U, 1U};
    uint8_t group_hash[16];
    uint8_t group_key[16];
    uint8_t sent_count = 0U;
    static const uint8_t payload[] = "grp-msg";

    api_fill_peer_id(peer1, 0x31U);
    api_fill_peer_id(peer2, 0x41U);
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    ctx = mnet_internal_context();
    MTEST_ASSERT_NOT_NULL(ctx);
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(api_self_id));

    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_add_ip(peer1, peer1_ip, 33335U));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_add_ip(peer2, peer2_ip, 33336U));
    MTEST_ASSERT_EQ(MNET_OK, mnet_group_create(group_hash, group_key));
    MTEST_ASSERT_EQ(MNET_OK, mnet_group_leave(group_hash));
    MTEST_ASSERT_FALSE(mnet_group_is_member(api_self_id, group_hash));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_join_group(peer1, group_hash));
    api_prime_authenticated_peer(ctx, peer1, peer1_ip, 33335U);
    api_prime_authenticated_peer(ctx, peer2, peer2_ip, 33336U);

    MTEST_ASSERT_EQ(MNET_OK, mnet_send_group_custom(group_hash, 0x80U, payload, sizeof(payload) - 1U, &sent_count));
    MTEST_ASSERT_EQ(1, (int)sent_count);
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_leave_group(peer1, group_hash));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_remove(peer1));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_remove(peer2));
    mnet_deinit();
}

MTEST(test_api_broadcast_all_reaches_multiple_peers)
{
    mnet_config_t cfg = api_config();
    mnet_context_t *ctx;
    uint8_t peer1[32];
    uint8_t peer2[32];
    uint8_t peer1_ip[4] = {127U, 0U, 0U, 1U};
    uint8_t peer2_ip[4] = {127U, 0U, 0U, 1U};
    uint8_t sent_count = 0U;
    static const uint8_t payload[] = "all-msg";

    api_fill_peer_id(peer1, 0x51U);
    api_fill_peer_id(peer2, 0x61U);

    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    ctx = mnet_internal_context();
    MTEST_ASSERT_NOT_NULL(ctx);
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_add_ip(peer1, peer1_ip, 33338U));
    MTEST_ASSERT_EQ(MNET_OK, mnet_peer_add_ip(peer2, peer2_ip, 33339U));
    api_prime_authenticated_peer(ctx, peer1, peer1_ip, 33338U);
    api_prime_authenticated_peer(ctx, peer2, peer2_ip, 33339U);

    MTEST_ASSERT_EQ(MNET_OK, mnet_send_group_custom(NULL, 0x80U, payload, sizeof(payload) - 1U, &sent_count));
    MTEST_ASSERT_EQ(2, (int)sent_count);
    mnet_deinit();
}

MTEST(test_api_unknown_and_empty_group_send)
{
    mnet_config_t cfg = api_config();
    uint8_t group_hash[16];
    uint8_t group_key[16];
    uint8_t sent_count = 99U;
    static const uint8_t payload[] = "noop";

    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_ERR_NOT_FOUND, mnet_send_group_custom((const uint8_t[16]){0xAA,0xBB,0xCC,0xDD}, 0x80U, payload, sizeof(payload) - 1U, &sent_count));
    MTEST_ASSERT_EQ(MNET_OK, mnet_group_create(group_hash, group_key));
    MTEST_ASSERT_EQ(MNET_OK, mnet_group_leave(group_hash));
    sent_count = 99U;
    MTEST_ASSERT_EQ(MNET_OK, mnet_send_group_custom(group_hash, 0x80U, payload, sizeof(payload) - 1U, &sent_count));
    MTEST_ASSERT_EQ(0, (int)sent_count);
    mnet_deinit();
}

MTEST(test_api_discovery_packet_is_untrusted)
{
    mnet_config_t cfg = api_config();
    mnet_context_t *ctx;
    p2p_message_t gossip;
    uint8_t plain[1024];
    size_t plain_len = sizeof(plain);
    uint8_t delta[1024];
    size_t delta_len = sizeof(delta);
    p2p_network_t sender;
    p2p_network_config_t net_cfg;
    uint8_t self_id[32];
    uint8_t peer_id[32];
    p2p_node_t peer;
    static const uint8_t src_ip[4] = {192U, 168U, 1U, 80U};

    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    ctx = mnet_internal_context();
    MTEST_ASSERT_NOT_NULL(ctx);

    memset(&net_cfg, 0, sizeof(net_cfg));
    net_cfg.gossip_interval_ms = 1000U;
    net_cfg.sync_interval_ms = 1000U;
    net_cfg.offline_timeout_ms = 1000U;
    net_cfg.max_nodes = P2P_MAX_NODES;
    net_cfg.max_groups = P2P_MAX_GROUPS;
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(self_id));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_init(&sender, &net_cfg, self_id));
    sender.now_ms = ctx->network.now_ms;
    api_fill_peer_id(peer_id, 0x71U);
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.node_id, peer_id, 32U);
    memcpy(peer.external_ip, src_ip, 4U);
    peer.external_port = 44444U;
    peer.first_seen = 1U;
    peer.last_seen = 1U;
    peer.is_online = true;
    peer.is_authorized = false;
    peer.db_version = 2U;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_add_node(&sender, &peer));
    delta_len = sizeof(delta);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_gossip_build_delta(&sender, delta, &delta_len));

    memset(&gossip, 0, sizeof(gossip));
    gossip.type = P2P_MSG_GOSSIP;
    memcpy(gossip.src, peer_id, 32U);
    memcpy(gossip.payload, delta, delta_len);
    gossip.payload_len = delta_len;

    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&gossip, plain, &plain_len));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&ctx->protocol, plain, plain_len, src_ip, 44444U, 0U));
    MTEST_ASSERT_EQ(1, (int)ctx->network.node_count);
    MTEST_ASSERT_FALSE(ctx->network.nodes[0].is_authorized);
    MTEST_ASSERT_EQ(0, (int)ctx->protocol.endpoint_count);

    MTEST_ASSERT_EQ(P2P_NET_ERR_SYNC, p2p_network_on_gossip(&ctx->network, (const uint8_t *)"X", 1U));
    mnet_deinit();
    p2p_network_deinit(&sender);
}

MTEST(test_api_custom_message_end_to_end)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];
    static const uint8_t payload[] = "fire";

    api_custom_count = 0;
    api_custom_handler_count = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));
    MTEST_ASSERT_EQ(MNET_OK, mnet_register_handler(0x80U, api_custom_handler));
    MTEST_ASSERT_EQ(MNET_OK, mnet_send_custom(node_id, 0x80U, payload, sizeof(payload) - 1U));
    MTEST_ASSERT_EQ(1, api_custom_count);
    MTEST_ASSERT_EQ(1, api_custom_handler_count);
    mnet_deinit();
}

MTEST(test_api_node_online_offline_callbacks)
{
    mnet_config_t cfg = api_config();

    api_online_count = 0;
    api_offline_count = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(1, api_online_count);
    mnet_deinit();
    MTEST_ASSERT_EQ(1, api_offline_count);
}

MTEST(test_api_rejects_double_init)
{
    mnet_config_t cfg = api_config();

    api_online_count = 0;
    api_offline_count = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_ERR_INTERNAL, mnet_init(&cfg));
    MTEST_ASSERT_EQ(1, api_online_count);
    mnet_deinit();
    MTEST_ASSERT_EQ(1, api_offline_count);
}

MTEST(test_api_broadcast_custom_delivers_local_group_member)
{
    mnet_config_t cfg = api_config();
    uint8_t group_hash[16];
    uint8_t group_key[16];
    static const uint8_t payload[] = "mesh";

    api_custom_count = 0;
    api_broadcast_handler_count = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_group_create(group_hash, group_key));
    MTEST_ASSERT_EQ(MNET_OK, mnet_register_handler(0x81U, api_broadcast_handler));
    MTEST_ASSERT_EQ(MNET_OK, mnet_broadcast_custom(group_hash, 0x81U, payload, sizeof(payload) - 1U));
    MTEST_ASSERT_EQ(1, api_custom_count);
    MTEST_ASSERT_EQ(1, api_broadcast_handler_count);
    mnet_deinit();
}

MTEST(test_api_request_callback_can_chain_request)
{
    mnet_config_t cfg = api_config();
    int first = 11;
    int second = 22;

    api_nested_request_stage = 0;
    api_nested_request_second_value = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(api_nested_node_id));
    MTEST_ASSERT_EQ(MNET_OK, mnet_publish("first", &first, sizeof(first)));
    MTEST_ASSERT_EQ(MNET_OK, mnet_publish("second", &second, sizeof(second)));
    MTEST_ASSERT_EQ(MNET_OK, mnet_request(api_nested_node_id, "first", api_nested_request_cb));
    MTEST_ASSERT_EQ(2, api_nested_request_stage);
    MTEST_ASSERT_EQ(second, api_nested_request_second_value);
    mnet_deinit();
}

MTEST(test_api_list_vars_and_metrics_end_to_end)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];
    int first = 1;
    int second = 2;

    api_list_err = MNET_ERR_INTERNAL;
    api_list_count = 0U;
    memset((void *)api_list_names, 0, sizeof(api_list_names));
    api_metrics_err = MNET_ERR_INTERNAL;
    memset(&api_metrics_value, 0, sizeof(api_metrics_value));

    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));
    MTEST_ASSERT_EQ(MNET_OK, mnet_publish("alpha", &first, sizeof(first)));
    MTEST_ASSERT_EQ(MNET_OK, mnet_publish("beta", &second, sizeof(second)));
    MTEST_ASSERT_EQ(MNET_OK, mnet_list_vars(node_id, api_list_vars_cb));
    MTEST_ASSERT_EQ(MNET_OK, api_list_err);
    MTEST_ASSERT_EQ(2, (int)api_list_count);
    MTEST_ASSERT_STR_EQ("alpha", api_list_names[0]);
    MTEST_ASSERT_STR_EQ("beta", api_list_names[1]);

    MTEST_ASSERT_EQ(MNET_OK, mnet_get_metrics(node_id, api_metrics_cb));
    MTEST_ASSERT_EQ(MNET_OK, api_metrics_err);
    MTEST_ASSERT_GT(api_metrics_value.free_heap, 0);
    MTEST_ASSERT_EQ(0, (int)api_metrics_value.group_count);
    mnet_deinit();
}

MTEST(test_api_query_not_found_maps_error)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];

    api_query_err = MNET_OK;
    api_query_count = 99U;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));
    MTEST_ASSERT_EQ(MNET_ERR_NOT_FOUND, mnet_query(node_id, "missing_table", NULL, api_query_cb));
    MTEST_ASSERT_EQ(MNET_ERR_NOT_FOUND, api_query_err);
    MTEST_ASSERT_EQ(0, (int)api_query_count);
    mnet_deinit();
}

MTEST(test_api_node_and_group_listing)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];
    uint8_t online_nodes[MNET_MAX_NODES][32];
    uint8_t all_nodes[MNET_MAX_NODES][32];
    uint8_t members[MNET_MAX_NODES][32];
    uint8_t group_hash[16];
    uint8_t group_key[16];
    uint8_t count = 0U;

    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));

    MTEST_ASSERT_EQ(MNET_OK, mnet_node_list_online(online_nodes, &count));
    MTEST_ASSERT_EQ(0, (int)count);

    MTEST_ASSERT_EQ(MNET_OK, mnet_node_list_all(all_nodes, &count));
    MTEST_ASSERT_EQ(0, (int)count);

    MTEST_ASSERT_EQ(MNET_OK, mnet_group_create(group_hash, group_key));
    MTEST_ASSERT_EQ(MNET_OK, mnet_group_members(group_hash, members, &count));
    MTEST_ASSERT_EQ(1, (int)count);
    MTEST_ASSERT_MEM_EQ(node_id, members[0], 32U);
    mnet_deinit();
}

MTEST(test_api_list_query_metrics_callbacks_can_chain)
{
    mnet_config_t cfg = api_config();
    int first = 5;
    int second = 6;

    api_chain_stage = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(api_nested_node_id));
    MTEST_ASSERT_EQ(MNET_OK, mnet_publish("first", &first, sizeof(first)));
    MTEST_ASSERT_EQ(MNET_OK, mnet_publish("second", &second, sizeof(second)));
    MTEST_ASSERT_EQ(MNET_OK, mnet_list_vars(api_nested_node_id, api_chain_list_cb));
    MTEST_ASSERT_EQ(3, api_chain_stage);
    mnet_deinit();
}

MTEST_SUITE(api)
{
    MTEST_RUN(test_api_init_deinit);
    MTEST_RUN(test_api_publish_request_end_to_end);
    MTEST_RUN(test_api_remote_request_no_auth_fails);
    MTEST_RUN(test_api_remote_request_pending_endpoint_fails);
    MTEST_RUN(test_api_remote_request_and_response_flow);
    MTEST_RUN(test_api_remote_request_timeout_frees_slot);
    MTEST_RUN(test_api_subscribe_end_to_end);
    MTEST_RUN(test_api_group_end_to_end);
    MTEST_RUN(test_api_custom_message_end_to_end);
    MTEST_RUN(test_api_node_online_offline_callbacks);
    MTEST_RUN(test_api_rejects_double_init);
    MTEST_RUN(test_api_broadcast_custom_delivers_local_group_member);
    MTEST_RUN(test_api_placeholder_peer_id_is_deterministic);
    MTEST_RUN(test_api_broadcast_all_reaches_multiple_peers);
    MTEST_RUN(test_api_request_callback_can_chain_request);
    MTEST_RUN(test_api_list_vars_and_metrics_end_to_end);
    MTEST_RUN(test_api_query_not_found_maps_error);
    MTEST_RUN(test_api_node_and_group_listing);
    MTEST_RUN(test_api_list_query_metrics_callbacks_can_chain);
    MTEST_RUN(test_api_discovery_packet_is_untrusted);
    MTEST_RUN(test_api_group_send_filters_peers);
    MTEST_RUN(test_api_lan_defaults_and_peer_table);
    MTEST_RUN(test_api_unknown_and_empty_group_send);
}

void run_api_suite(void)
{
    MTEST_SUITE_RUN(api);
}

#include "mtest.h"

#include "data/p2p_data.h"
#include "network/p2p_network.h"
#include "protocol/p2p_protocol.h"
#include "security/p2p_security.h"
#include "transport/p2p_transport.h"

#include <string.h>

static int protocol_custom_called;
static uint8_t protocol_custom_payload[P2P_MAX_PAYLOAD];
static size_t protocol_custom_len;
static int protocol_data_resp_called;
static uint16_t protocol_data_resp_id;
static uint8_t protocol_data_resp_src[32];
static uint8_t protocol_data_resp_status;
static uint8_t protocol_data_resp_value[256];
static size_t protocol_data_resp_value_len;
static uint32_t protocol_fake_now_ms;

static uint32_t test_protocol_now_ms(void)
{
    return protocol_fake_now_ms;
}

static void protocol_custom_handler(const p2p_message_t *msg)
{
    protocol_custom_called++;
    protocol_custom_len = msg->payload_len;
    if (msg->payload_len > 0U) {
        memcpy(protocol_custom_payload, msg->payload, msg->payload_len);
    }
}

static void authenticate_mutual(p2p_security_t *a, p2p_security_t *b)
{
    uint8_t hello_a[P2P_HMAC_SIZE];
    uint8_t hello_b[P2P_HMAC_SIZE];
    uint8_t ack_a[P2P_HMAC_SIZE];
    uint8_t ack_b[P2P_HMAC_SIZE];

    /* A -> B: HELLO */
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(a, b->node_pubkey, hello_a));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_verify_hello_mac(b, a->node_pubkey, hello_a));
    /* B -> A: HELLO_ACK */
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(b, a->node_pubkey, ack_b));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_verify_hello_mac(a, b->node_pubkey, ack_b));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_mark_outbound_verified(a, b->node_pubkey));

    /* B -> A: HELLO */
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(b, a->node_pubkey, hello_b));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_verify_hello_mac(a, b->node_pubkey, hello_b));
    /* A -> B: HELLO_ACK */
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(a, b->node_pubkey, ack_a));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_verify_hello_mac(b, a->node_pubkey, ack_a));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_mark_outbound_verified(b, a->node_pubkey));
}

static void protocol_data_response_handler(const p2p_message_t *msg)
{
    if (msg == NULL || msg->payload_len < 3U) {
        return;
    }

    protocol_data_resp_called++;
    protocol_data_resp_id = msg->msg_id;
    memcpy(protocol_data_resp_src, msg->src, 32U);
    protocol_data_resp_status = msg->payload[0];
    protocol_data_resp_value_len = (size_t)(((uint16_t)msg->payload[1] << 8) | msg->payload[2]);
    if (protocol_data_resp_value_len > sizeof(protocol_data_resp_value)) {
        protocol_data_resp_value_len = sizeof(protocol_data_resp_value);
    }
    if ((3U + protocol_data_resp_value_len) <= msg->payload_len) {
        memcpy(protocol_data_resp_value, msg->payload + 3U, protocol_data_resp_value_len);
    }
}

static void init_protocol_fixture(p2p_protocol_t *proto,
                                  p2p_transport_t *transport,
                                  p2p_security_t *security,
                                  p2p_network_t *network,
                                  p2p_data_t *data,
                                  const uint8_t remote_pubkey[32])
{
    p2p_transport_config_t tcfg;
    p2p_security_config_t scfg;
    p2p_network_config_t ncfg;
    p2p_data_config_t dcfg;
    p2p_protocol_config_t pcfg;
    uint8_t self_id[32];

    (void)remote_pubkey;
    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.local_port = 0U;
    tcfg.retry_count = 2U;
    tcfg.retry_delay_ms = 1U;
    tcfg.rx_buf_size = sizeof(p2p_packet_t) * 4U;
    tcfg.tx_buf_size = sizeof(p2p_transport_retry_entry_t) * 4U;
    tcfg.hal = p2p_hal_default();
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(transport, &tcfg));

    memset(&scfg, 0, sizeof(scfg));
    scfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(security, &scfg));

    memcpy(self_id, security->node_pubkey, 32U);
    memset(&ncfg, 0, sizeof(ncfg));
    ncfg.gossip_interval_ms = 1000U;
    ncfg.sync_interval_ms = 1000U;
    ncfg.offline_timeout_ms = 1000U;
    ncfg.max_nodes = P2P_MAX_NODES;
    ncfg.max_groups = P2P_MAX_GROUPS;
    ncfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_init(network, &ncfg, self_id));

    memset(&dcfg, 0, sizeof(dcfg));
    dcfg.max_vars = P2P_MAX_VARS;
    dcfg.max_subs = P2P_MAX_SUBS;
    dcfg.notify_min_interval_ms = 1U;
    dcfg.compress_data = true;
    dcfg.spool_size = 1U;
    dcfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_init(data, &dcfg));

    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.max_pending = P2P_MAX_PENDING;
    pcfg.retry_interval_ms = 1U;
    pcfg.retry_count = 1U;
    pcfg.custom_handler = protocol_custom_handler;
    pcfg.data_response_handler = protocol_data_response_handler;
    pcfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_init(proto, &pcfg, transport, security, network, data));
}

static void init_protocol_fixture_on_port(p2p_protocol_t *proto,
                                          p2p_transport_t *transport,
                                          p2p_security_t *security,
                                          p2p_network_t *network,
                                          p2p_data_t *data,
                                          uint16_t local_port)
{
    p2p_transport_config_t tcfg;
    p2p_security_config_t scfg;
    p2p_network_config_t ncfg;
    p2p_data_config_t dcfg;
    p2p_protocol_config_t pcfg;
    uint8_t self_id[32];

    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.local_port = local_port;
    tcfg.retry_count = 2U;
    tcfg.retry_delay_ms = 1U;
    tcfg.rx_buf_size = sizeof(p2p_packet_t) * 4U;
    tcfg.tx_buf_size = sizeof(p2p_transport_retry_entry_t) * 4U;
    tcfg.hal = p2p_hal_default();
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(transport, &tcfg));

    memset(&scfg, 0, sizeof(scfg));
    scfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(security, &scfg));

    memcpy(self_id, security->node_pubkey, 32U);
    memset(&ncfg, 0, sizeof(ncfg));
    ncfg.gossip_interval_ms = 1000U;
    ncfg.sync_interval_ms = 1000U;
    ncfg.offline_timeout_ms = 1000U;
    ncfg.max_nodes = P2P_MAX_NODES;
    ncfg.max_groups = P2P_MAX_GROUPS;
    ncfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_init(network, &ncfg, self_id));

    memset(&dcfg, 0, sizeof(dcfg));
    dcfg.max_vars = P2P_MAX_VARS;
    dcfg.max_subs = P2P_MAX_SUBS;
    dcfg.notify_min_interval_ms = 1U;
    dcfg.compress_data = true;
    dcfg.spool_size = 1U;
    dcfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_init(data, &dcfg));

    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.max_pending = P2P_MAX_PENDING;
    pcfg.retry_interval_ms = 1U;
    pcfg.retry_count = 1U;
    pcfg.custom_handler = protocol_custom_handler;
    pcfg.data_response_handler = protocol_data_response_handler;
    pcfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_init(proto, &pcfg, transport, security, network, data));
}


static void deinit_protocol_fixture(p2p_protocol_t *proto,
                                    p2p_transport_t *transport,
                                    p2p_security_t *security,
                                    p2p_network_t *network,
                                    p2p_data_t *data)
{
    p2p_protocol_deinit(proto);
    p2p_data_deinit(data);
    p2p_network_deinit(network);
    p2p_security_deinit(security);
    p2p_transport_deinit(transport);
}

MTEST(test_protocol_serialize_parse)
{
    p2p_message_t in;
    p2p_message_t out;
    uint8_t buf[1024];
    size_t len = sizeof(buf);


    memset(&in, 0, sizeof(in));
    in.type = P2P_MSG_DATA_REQUEST;
    in.msg_id = 42U;
    in.timestamp = 12345U;
    memset(in.src, 0x11, sizeof(in.src));
    memset(in.dst, 0x22, sizeof(in.dst));
    memset(in.group_hash, 0x33, sizeof(in.group_hash));
    memcpy(in.payload, "temperature", 11U);
    in.payload_len = 11U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&in, buf, &len));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_parse(&out, buf, len));
    MTEST_ASSERT_EQ(in.type, out.type);
    MTEST_ASSERT_EQ(in.msg_id, out.msg_id);
    MTEST_ASSERT_EQ(in.timestamp, out.timestamp);
    MTEST_ASSERT_MEM_EQ(in.src, out.src, sizeof(in.src));
    MTEST_ASSERT_MEM_EQ(in.dst, out.dst, sizeof(in.dst));
    MTEST_ASSERT_MEM_EQ(in.group_hash, out.group_hash, sizeof(in.group_hash));
    MTEST_ASSERT_EQ((int)in.payload_len, (int)out.payload_len);
    MTEST_ASSERT_MEM_EQ(in.payload, out.payload, in.payload_len);
}

MTEST(test_protocol_dispatch_gossip)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    p2p_security_t remote_sec;
    p2p_security_config_t remote_cfg;
    p2p_network_t source_net;
    p2p_network_config_t source_cfg;
    p2p_node_t node_c;
    p2p_node_t out;
    p2p_message_t msg;
    uint8_t delta[1024];
    size_t delta_len = sizeof(delta);

    memset(&remote_cfg, 0, sizeof(remote_cfg));
    remote_cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    init_protocol_fixture(&proto, &transport, &security, &network, &data, remote_sec.node_pubkey);
    authenticate_mutual(&security, &remote_sec);

    memset(&source_cfg, 0, sizeof(source_cfg));
    source_cfg.gossip_interval_ms = 1000U;
    source_cfg.sync_interval_ms = 1000U;
    source_cfg.offline_timeout_ms = 1000U;
    source_cfg.max_nodes = P2P_MAX_NODES;
    source_cfg.max_groups = P2P_MAX_GROUPS;
    source_cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_init(&source_net, &source_cfg, remote_sec.node_pubkey));

    memset(&node_c, 0, sizeof(node_c));
    memset(node_c.node_id, 0xC3, sizeof(node_c.node_id));
    node_c.is_online = true;
    node_c.db_version = 2U;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_add_node(&source_net, &node_c));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_gossip_build_delta(&source_net, delta, &delta_len));

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_GOSSIP;
    memcpy(msg.src, remote_sec.node_pubkey, 32U);
    memcpy(msg.payload, delta, delta_len);
    msg.payload_len = delta_len;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_dispatch(&proto, &msg));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_find_node(&network, node_c.node_id, &out));

    p2p_network_deinit(&source_net);
    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
    p2p_security_deinit(&remote_sec);
}

MTEST(test_protocol_retry_failed_message)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    p2p_security_t remote_sec;
    p2p_security_config_t remote_cfg;
    p2p_message_t msg;
    uint8_t mac[P2P_HMAC_SIZE];
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};

    protocol_fake_now_ms = UINT32_MAX - 2U;
    memset(&remote_cfg, 0, sizeof(remote_cfg));
    remote_cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    init_protocol_fixture(&proto, &transport, &security, &network, &data, NULL);

    proto.endpoints[0].valid = true;
    memcpy(proto.endpoints[0].node_id, remote_sec.node_pubkey, 32U);
    memcpy(proto.endpoints[0].local_ip, ip, 4U);
    proto.endpoints[0].local_port = 40202U;
    proto.endpoint_count = 1U;

    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(&security, remote_sec.node_pubkey, mac));
    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_HELLO;
    memcpy(msg.dst, remote_sec.node_pubkey, sizeof(msg.dst));
    memcpy(msg.payload, mac, P2P_HMAC_SIZE);
    msg.payload_len = P2P_HMAC_SIZE;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_send(&proto, &msg));
    MTEST_ASSERT_EQ(1, (int)proto.pending_count);

    proto.pending[0].sent_at = UINT32_MAX - 2U;
    protocol_fake_now_ms = 1U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&proto));
    MTEST_ASSERT_EQ(1, (int)proto.pending[0].retry_count);
    proto.pending[0].sent_at = UINT32_MAX - 2U;
    protocol_fake_now_ms = 5U;
    MTEST_ASSERT_EQ(P2P_PROTO_ERR_RETRY, p2p_protocol_tick(&proto));
    MTEST_ASSERT_EQ(0, (int)proto.pending_count);

    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
    p2p_security_deinit(&remote_sec);
    protocol_fake_now_ms = 0U;
}

MTEST(test_protocol_custom_message)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    p2p_security_t remote_sec;
    p2p_security_config_t remote_cfg;
    p2p_message_t msg;
    uint8_t buf[1024];
    size_t len = sizeof(buf);

    protocol_custom_called = 0;
    protocol_custom_len = 0U;
    init_protocol_fixture(&proto, &transport, &security, &network, &data, NULL);
    memset(&remote_cfg, 0, sizeof(remote_cfg));
    remote_cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    authenticate_mutual(&security, &remote_sec);
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_register_handler(&proto, 0x80U, protocol_custom_handler));

    memset(&msg, 0, sizeof(msg));
    msg.type = 0x80U;
    memcpy(msg.src, remote_sec.node_pubkey, 32U);
    memcpy(msg.payload, "fire", 4U);
    msg.payload_len = 4U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&msg, buf, &len));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&proto, buf, len, NULL, 0U, 0U));
    MTEST_ASSERT_EQ(1, protocol_custom_called);
    MTEST_ASSERT_EQ(4, (int)protocol_custom_len);
    MTEST_ASSERT_MEM_EQ("fire", protocol_custom_payload, 4U);

    p2p_security_deinit(&remote_sec);
    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
}

MTEST(test_protocol_broadcast_group_message)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    p2p_security_t remote_sec;
    p2p_security_config_t remote_cfg;
    p2p_message_t msg;
    uint8_t group_hash[16];

    memset(&remote_cfg, 0, sizeof(remote_cfg));
    remote_cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    init_protocol_fixture(&proto, &transport, &security, &network, &data, remote_sec.node_pubkey);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_group_create(&network, group_hash));

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_CUSTOM;
    memcpy(msg.dst, remote_sec.node_pubkey, sizeof(msg.dst));
    memcpy(msg.payload, "grp", 3U);
    msg.payload_len = 3U;
    MTEST_ASSERT_EQ(P2P_PROTO_ERR_NO_ROUTE, p2p_protocol_broadcast(&proto, group_hash, &msg));
    /* no route -> no pending queued */
    /* no route -> no pending fields to check */
    /* no route -> no pending fields to check */

    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
    p2p_security_deinit(&remote_sec);
}

MTEST(test_protocol_lifecycle_ready)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;

    init_protocol_fixture(&proto, &transport, &security, &network, &data, NULL);
    MTEST_ASSERT_EQ(3, (int)proto.fsm.state);
    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
}


MTEST(test_protocol_send_no_endpoint_fails)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    p2p_security_t remote_sec;
    p2p_security_config_t remote_cfg;
    p2p_message_t msg;

    memset(&remote_cfg, 0, sizeof(remote_cfg));
    remote_cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    init_protocol_fixture(&proto, &transport, &security, &network, &data, NULL);

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_DATA_REQUEST;
    memcpy(msg.dst, remote_sec.node_pubkey, sizeof(msg.dst));
    memcpy(msg.payload, "k", 1U);
    msg.payload_len = 1U;
    MTEST_ASSERT_EQ(P2P_PROTO_ERR_NO_ROUTE, p2p_protocol_send(&proto, &msg));

    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
    p2p_security_deinit(&remote_sec);
}

MTEST(test_protocol_send_with_endpoint_hits_transport)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    p2p_security_t remote_sec;
    p2p_security_config_t remote_cfg;
    p2p_message_t msg;
    uint8_t mac[P2P_HMAC_SIZE];
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};

    memset(&remote_cfg, 0, sizeof(remote_cfg));
    remote_cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    init_protocol_fixture(&proto, &transport, &security, &network, &data, NULL);

    proto.endpoints[0].valid = true;
    memcpy(proto.endpoints[0].node_id, remote_sec.node_pubkey, 32U);
    memcpy(proto.endpoints[0].local_ip, ip, 4U);
    proto.endpoints[0].local_port = 40001U;
    proto.endpoint_count = 1U;

    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(&security, remote_sec.node_pubkey, mac));
    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_HELLO;
    memcpy(msg.dst, remote_sec.node_pubkey, sizeof(msg.dst));
    memcpy(msg.payload, mac, P2P_HMAC_SIZE);
    msg.payload_len = P2P_HMAC_SIZE;

    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_send(&proto, &msg));
    MTEST_ASSERT_TRUE(transport.last_peer_valid);
    MTEST_ASSERT_EQ(40001, (int)transport.last_peer_port);

    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
    p2p_security_deinit(&remote_sec);
}

MTEST(test_protocol_tick_drains_and_dispatches)
{
    p2p_protocol_t a_proto;
    p2p_transport_t a_transport;
    p2p_security_t a_security;
    p2p_network_t a_network;
    p2p_data_t a_data;
    p2p_protocol_t b_proto;
    p2p_transport_t b_transport;
    p2p_security_t b_security;
    p2p_network_t b_network;
    p2p_data_t b_data;
    p2p_message_t msg;
    uint8_t mac_a[P2P_HMAC_SIZE];
    uint8_t mac_b[P2P_HMAC_SIZE];
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};

    protocol_custom_called = 0;
    protocol_custom_len = 0U;

    /* Use fixed ports so transports can talk over loopback */
    init_protocol_fixture_on_port(&a_proto, &a_transport, &a_security, &a_network, &a_data, 40101U);
    init_protocol_fixture_on_port(&b_proto, &b_transport, &b_security, &b_network, &b_data, 40102U);

    /* Seed endpoints for HELLO routing (PENDING only) */
    a_proto.endpoints[0].valid = true;
    a_proto.endpoints[0].state = P2P_ENDPOINT_PENDING;
    memcpy(a_proto.endpoints[0].node_id, b_security.node_pubkey, 32U);
    memcpy(a_proto.endpoints[0].local_ip, ip, 4U);
    a_proto.endpoints[0].local_port = 40102U;
    a_proto.endpoint_count = 1U;

    b_proto.endpoints[0].valid = true;
    b_proto.endpoints[0].state = P2P_ENDPOINT_PENDING;
    memcpy(b_proto.endpoints[0].node_id, a_security.node_pubkey, 32U);
    memcpy(b_proto.endpoints[0].local_ip, ip, 4U);
    b_proto.endpoints[0].local_port = 40101U;
    b_proto.endpoint_count = 1U;

    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(&a_security, b_security.node_pubkey, mac_a));
    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_HELLO;
    memcpy(msg.dst, b_security.node_pubkey, 32U);
    memcpy(msg.payload, mac_a, P2P_HMAC_SIZE);
    msg.payload_len = P2P_HMAC_SIZE;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_send(&a_proto, &msg));

    /* B must drain transport->recv and verify A, but not fully authenticate yet */
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&b_proto));
    MTEST_ASSERT_TRUE(!p2p_security_is_authenticated(&b_security, a_security.node_pubkey));

    /* A receives HELLO_ACK from B and becomes authenticated */
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&a_proto));
    MTEST_ASSERT_TRUE(p2p_security_is_authenticated(&a_security, b_security.node_pubkey));

    /* B sends HELLO to A so B can later become authenticated too */
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(&b_security, a_security.node_pubkey, mac_b));
    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_HELLO;
    memcpy(msg.dst, a_security.node_pubkey, 32U);
    memcpy(msg.payload, mac_b, P2P_HMAC_SIZE);
    msg.payload_len = P2P_HMAC_SIZE;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_send(&b_proto, &msg));

    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&a_proto));
    MTEST_ASSERT_TRUE(p2p_security_is_authenticated(&a_security, b_security.node_pubkey));

    /* B receives HELLO_ACK from A and becomes authenticated */
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&b_proto));
    MTEST_ASSERT_TRUE(p2p_security_is_authenticated(&b_security, a_security.node_pubkey));

    /* Now send an encrypted custom message A -> B and verify dispatch */
    protocol_custom_called = 0;
    protocol_custom_len = 0U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_register_handler(&b_proto, 0x80U, protocol_custom_handler));
    memset(&msg, 0, sizeof(msg));
    msg.type = 0x80U;
    memcpy(msg.dst, b_security.node_pubkey, 32U);
    memcpy(msg.payload, "fire", 4U);
    msg.payload_len = 4U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_send(&a_proto, &msg));

    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&b_proto));
    MTEST_ASSERT_EQ(1, protocol_custom_called);
    MTEST_ASSERT_EQ(4, (int)protocol_custom_len);
    MTEST_ASSERT_MEM_EQ("fire", protocol_custom_payload, 4U);

    deinit_protocol_fixture(&a_proto, &a_transport, &a_security, &a_network, &a_data);
    deinit_protocol_fixture(&b_proto, &b_transport, &b_security, &b_network, &b_data);
}

MTEST(test_protocol_stun_propagation)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    static const uint8_t ip[4] = {10U, 1U, 2U, 3U};

    init_protocol_fixture(&proto, &transport, &security, &network, &data, NULL);
    transport.stun_resolved = true;
    memcpy(transport.external_ip, ip, 4U);
    transport.external_port = 34567U;

    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&proto));
    MTEST_ASSERT_MEM_EQ(ip, network.self.external_ip, 4U);
    MTEST_ASSERT_EQ(34567, (int)network.self.external_port);

    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
}


MTEST(test_protocol_encrypted_on_packet)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t a;
    p2p_security_t b;
    p2p_network_t network;
    p2p_data_t data;
    p2p_security_config_t cfg;
    p2p_message_t msg;
    uint8_t plain[1024];
    size_t plain_len = sizeof(plain);
    uint8_t cipher[1024];
    size_t cipher_len = sizeof(cipher);
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};

    /* Build a protocol with B as receiver */
    init_protocol_fixture(&proto, &transport, &b, &network, &data, NULL);
    memset(&cfg, 0, sizeof(cfg));
    cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&a, &cfg));

    /* Authenticate both directions (mutual) */
    authenticate_mutual(&a, &b);

    /* Receiver must know endpoint for encrypted receive key selection */
    proto.endpoints[0].valid = true;
    proto.endpoints[0].state = P2P_ENDPOINT_AUTHENTICATED;
    memcpy(proto.endpoints[0].node_id, a.node_pubkey, 32U);
    memcpy(proto.endpoints[0].local_ip, ip, 4U);
    proto.endpoints[0].local_port = 45000U;
    proto.endpoint_count = 1U;

    protocol_custom_called = 0;
    protocol_custom_len = 0U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_register_handler(&proto, 0x80U, protocol_custom_handler));

    memset(&msg, 0, sizeof(msg));
    msg.type = 0x80U;
    memcpy(msg.src, a.node_pubkey, 32U);
    memcpy(msg.dst, b.node_pubkey, 32U);
    memcpy(msg.payload, "ok", 2U);
    msg.payload_len = 2U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&msg, plain, &plain_len));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt(&a, b.node_pubkey, plain, plain_len, cipher, &cipher_len));

    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&proto, cipher, cipher_len, ip, 45000U, P2P_PACKET_FLAG_ENCRYPTED));
    MTEST_ASSERT_EQ(1, protocol_custom_called);

    /* Corrupted encrypted payload must be rejected */
    cipher[cipher_len - 1U] ^= 0x01U;
    MTEST_ASSERT_EQ(P2P_PROTO_ERR_PARSE, p2p_protocol_on_packet(&proto, cipher, cipher_len, ip, 45000U, P2P_PACKET_FLAG_ENCRYPTED));

    /* Plaintext still works */
    protocol_custom_called = 0;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&proto, plain, plain_len, ip, 45000U, 0U));
    MTEST_ASSERT_EQ(1, protocol_custom_called);

    p2p_security_deinit(&a);
    deinit_protocol_fixture(&proto, &transport, &b, &network, &data);
}

MTEST(test_protocol_spoofed_plaintext_cannot_authenticate_endpoint)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    p2p_security_t attacker_sec;
    p2p_security_config_t attacker_cfg;
    p2p_message_t hello;
    uint8_t mac[P2P_HMAC_SIZE];
    uint8_t buf[1024];
    size_t len = sizeof(buf);
    static const uint8_t ip[4] = {10U, 0U, 0U, 123U};

    init_protocol_fixture(&proto, &transport, &security, &network, &data, NULL);
    memset(&attacker_cfg, 0, sizeof(attacker_cfg));
    attacker_cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&attacker_sec, &attacker_cfg));

    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(&attacker_sec, security.node_pubkey, mac));
    memset(&hello, 0, sizeof(hello));
    hello.type = P2P_MSG_HELLO;
    memcpy(hello.src, attacker_sec.node_pubkey, 32U);
    memcpy(hello.dst, security.node_pubkey, 32U);
    memcpy(hello.payload, mac, P2P_HMAC_SIZE);
    hello.payload_len = P2P_HMAC_SIZE;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&hello, buf, &len));

    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&proto, buf, len, ip, 41000U, 0U));
    MTEST_ASSERT_EQ(1, (int)proto.endpoint_count);
    MTEST_ASSERT_EQ(P2P_ENDPOINT_PENDING, (int)proto.endpoints[0].state);
    MTEST_ASSERT_TRUE(!p2p_security_is_authenticated(&security, attacker_sec.node_pubkey));

    p2p_security_deinit(&attacker_sec);
    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
}

MTEST(test_protocol_spoofed_plaintext_cannot_overwrite_authenticated_endpoint)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    p2p_security_t attacker_sec;
    p2p_security_config_t attacker_cfg;
    p2p_message_t hello;
    uint8_t mac[P2P_HMAC_SIZE];
    uint8_t buf[1024];
    size_t len = sizeof(buf);
    static const uint8_t ip_auth[4] = {192U, 168U, 1U, 10U};
    static const uint8_t ip_spoof[4] = {192U, 168U, 1U, 99U};

    init_protocol_fixture(&proto, &transport, &security, &network, &data, NULL);
    memset(&attacker_cfg, 0, sizeof(attacker_cfg));
    attacker_cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&attacker_sec, &attacker_cfg));

    proto.endpoints[0].valid = true;
    proto.endpoints[0].state = P2P_ENDPOINT_AUTHENTICATED;
    memcpy(proto.endpoints[0].node_id, attacker_sec.node_pubkey, 32U);
    memcpy(proto.endpoints[0].local_ip, ip_auth, 4U);
    proto.endpoints[0].local_port = 41001U;
    proto.endpoint_count = 1U;

    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(&attacker_sec, security.node_pubkey, mac));
    memset(&hello, 0, sizeof(hello));
    hello.type = P2P_MSG_HELLO;
    memcpy(hello.src, attacker_sec.node_pubkey, 32U);
    memcpy(hello.dst, security.node_pubkey, 32U);
    memcpy(hello.payload, mac, P2P_HMAC_SIZE);
    hello.payload_len = P2P_HMAC_SIZE;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&hello, buf, &len));

    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&proto, buf, len, ip_spoof, 49999U, 0U));
    MTEST_ASSERT_EQ(1, (int)proto.endpoint_count);
    MTEST_ASSERT_EQ(P2P_ENDPOINT_AUTHENTICATED, (int)proto.endpoints[0].state);
    MTEST_ASSERT_MEM_EQ(ip_auth, proto.endpoints[0].local_ip, 4U);
    MTEST_ASSERT_EQ(41001, (int)proto.endpoints[0].local_port);

    p2p_security_deinit(&attacker_sec);
    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
}

MTEST(test_protocol_directed_send_to_pending_endpoint_fails_for_app)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    p2p_security_t remote_sec;
    p2p_security_config_t remote_cfg;
    p2p_message_t msg;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};

    memset(&remote_cfg, 0, sizeof(remote_cfg));
    remote_cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    init_protocol_fixture(&proto, &transport, &security, &network, &data, NULL);

    proto.endpoints[0].valid = true;
    proto.endpoints[0].state = P2P_ENDPOINT_PENDING;
    memcpy(proto.endpoints[0].node_id, remote_sec.node_pubkey, 32U);
    memcpy(proto.endpoints[0].local_ip, ip, 4U);
    proto.endpoints[0].local_port = 42000U;
    proto.endpoint_count = 1U;

    memset(&msg, 0, sizeof(msg));
    msg.type = 0x80U;
    memcpy(msg.dst, remote_sec.node_pubkey, 32U);
    memcpy(msg.payload, "no", 2U);
    msg.payload_len = 2U;
    MTEST_ASSERT_EQ(P2P_PROTO_ERR_NO_ROUTE, p2p_protocol_send(&proto, &msg));

    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
    p2p_security_deinit(&remote_sec);
}

MTEST(test_protocol_encrypted_src_mismatch_rejected)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t a;
    p2p_security_t b;
    p2p_network_t network;
    p2p_data_t data;
    p2p_security_config_t cfg;
    p2p_message_t msg;
    uint8_t plain[1024];
    size_t plain_len = sizeof(plain);
    uint8_t cipher[1024];
    size_t cipher_len = sizeof(cipher);
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};

    init_protocol_fixture(&proto, &transport, &b, &network, &data, NULL);
    memset(&cfg, 0, sizeof(cfg));
    cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&a, &cfg));
    authenticate_mutual(&a, &b);

    proto.endpoints[0].valid = true;
    proto.endpoints[0].state = P2P_ENDPOINT_AUTHENTICATED;
    memcpy(proto.endpoints[0].node_id, a.node_pubkey, 32U);
    memcpy(proto.endpoints[0].local_ip, ip, 4U);
    proto.endpoints[0].local_port = 45001U;
    proto.endpoint_count = 1U;

    memset(&msg, 0, sizeof(msg));
    msg.type = 0x80U;
    memset(msg.src, 0xEE, 32U); /* wrong src */
    memcpy(msg.dst, b.node_pubkey, 32U);
    memcpy(msg.payload, "x", 1U);
    msg.payload_len = 1U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&msg, plain, &plain_len));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt(&a, b.node_pubkey, plain, plain_len, cipher, &cipher_len));

    MTEST_ASSERT_EQ(P2P_PROTO_ERR_PARSE, p2p_protocol_on_packet(&proto, cipher, cipher_len, ip, 45001U, P2P_PACKET_FLAG_ENCRYPTED));

    p2p_security_deinit(&a);
    deinit_protocol_fixture(&proto, &transport, &b, &network, &data);
}

MTEST(test_protocol_encrypted_from_unknown_endpoint_rejected)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t a;
    p2p_security_t b;
    p2p_network_t network;
    p2p_data_t data;
    p2p_security_config_t cfg;
    p2p_message_t msg;
    uint8_t plain[1024];
    size_t plain_len = sizeof(plain);
    uint8_t cipher[1024];
    size_t cipher_len = sizeof(cipher);
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};

    init_protocol_fixture(&proto, &transport, &b, &network, &data, NULL);
    memset(&cfg, 0, sizeof(cfg));
    cfg.now_ms = test_protocol_now_ms;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&a, &cfg));
    authenticate_mutual(&a, &b);

    /* No authenticated endpoint mapping for this address -> reject. */
    proto.endpoint_count = 0U;

    memset(&msg, 0, sizeof(msg));
    msg.type = 0x80U;
    memcpy(msg.src, a.node_pubkey, 32U);
    memcpy(msg.dst, b.node_pubkey, 32U);
    memcpy(msg.payload, "z", 1U);
    msg.payload_len = 1U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&msg, plain, &plain_len));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt(&a, b.node_pubkey, plain, plain_len, cipher, &cipher_len));

    MTEST_ASSERT_EQ(P2P_PROTO_ERR_PARSE, p2p_protocol_on_packet(&proto, cipher, cipher_len, ip, 45002U, P2P_PACKET_FLAG_ENCRYPTED));

    p2p_security_deinit(&a);
    deinit_protocol_fixture(&proto, &transport, &b, &network, &data);
}

MTEST(test_protocol_preauth_message_rejected)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    p2p_message_t msg;

    init_protocol_fixture(&proto, &transport, &security, &network, &data, NULL);
    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_GOSSIP;
    memcpy(msg.payload, "x", 1U);
    msg.payload_len = 1U;
    MTEST_ASSERT_EQ(P2P_PROTO_ERR_NO_HANDLER, p2p_protocol_dispatch(&proto, &msg));

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_DATA_REQUEST;
    memcpy(msg.payload, "val", 3U);
    msg.payload_len = 3U;
    MTEST_ASSERT_EQ(P2P_PROTO_ERR_NO_HANDLER, p2p_protocol_dispatch(&proto, &msg));
    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
}

MTEST(test_protocol_remote_data_request_response_roundtrip)
{
    p2p_protocol_t a_proto;
    p2p_transport_t a_transport;
    p2p_security_t a_security;
    p2p_network_t a_network;
    p2p_data_t a_data;
    p2p_protocol_t b_proto;
    p2p_transport_t b_transport;
    p2p_security_t b_security;
    p2p_network_t b_network;
    p2p_data_t b_data;
    p2p_message_t msg;
    uint8_t mac_a[P2P_HMAC_SIZE];
    uint8_t mac_b[P2P_HMAC_SIZE];
    int value = 777;
    uint8_t big[100];
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};

    protocol_data_resp_called = 0;
    protocol_data_resp_status = 0xFFU;
    protocol_data_resp_value_len = 0U;

    init_protocol_fixture_on_port(&a_proto, &a_transport, &a_security, &a_network, &a_data, 40601U);
    init_protocol_fixture_on_port(&b_proto, &b_transport, &b_security, &b_network, &b_data, 40602U);

    /* Seed endpoints for HELLO routing (PENDING only) */
    a_proto.endpoints[0].valid = true;
    a_proto.endpoints[0].state = P2P_ENDPOINT_PENDING;
    memcpy(a_proto.endpoints[0].node_id, b_security.node_pubkey, 32U);
    memcpy(a_proto.endpoints[0].local_ip, ip, 4U);
    a_proto.endpoints[0].local_port = 40602U;
    a_proto.endpoint_count = 1U;

    b_proto.endpoints[0].valid = true;
    b_proto.endpoints[0].state = P2P_ENDPOINT_PENDING;
    memcpy(b_proto.endpoints[0].node_id, a_security.node_pubkey, 32U);
    memcpy(b_proto.endpoints[0].local_ip, ip, 4U);
    b_proto.endpoints[0].local_port = 40601U;
    b_proto.endpoint_count = 1U;

    /* Mutual handshake: A->B HELLO, B->A HELLO, then exchange HELLO_ACK via tick */
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(&a_security, b_security.node_pubkey, mac_a));
    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_HELLO;
    memcpy(msg.dst, b_security.node_pubkey, 32U);
    memcpy(msg.payload, mac_a, P2P_HMAC_SIZE);
    msg.payload_len = P2P_HMAC_SIZE;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_send(&a_proto, &msg));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&b_proto));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&a_proto));

    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_build_hello_mac(&b_security, a_security.node_pubkey, mac_b));
    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_HELLO;
    memcpy(msg.dst, a_security.node_pubkey, 32U);
    memcpy(msg.payload, mac_b, P2P_HMAC_SIZE);
    msg.payload_len = P2P_HMAC_SIZE;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_send(&b_proto, &msg));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&a_proto));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&b_proto));

    MTEST_ASSERT_TRUE(p2p_security_is_authenticated(&a_security, b_security.node_pubkey));
    MTEST_ASSERT_TRUE(p2p_security_is_authenticated(&b_security, a_security.node_pubkey));

    /* B hosts the data */
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_publish(&b_data, "val", P2P_DATA_VAR, &value, sizeof(value)));
    memset(big, 0xAA, sizeof(big));
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_publish(&b_data, "big", P2P_DATA_VAR, big, sizeof(big)));

    /* A -> B DATA_REQUEST over authenticated path */
    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_DATA_REQUEST;
    msg.msg_id = 123U;
    memcpy(msg.dst, b_security.node_pubkey, 32U);
    memcpy(msg.payload, "val", 3U);
    msg.payload_len = 3U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_send(&a_proto, &msg));

    /* B receives request, dispatches local lookup, sends DATA_RESPONSE */
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&b_proto));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&a_proto));

    MTEST_ASSERT_EQ(1, protocol_data_resp_called);
    MTEST_ASSERT_EQ(123, (int)protocol_data_resp_id);
    MTEST_ASSERT_MEM_EQ(b_security.node_pubkey, protocol_data_resp_src, 32U);
    MTEST_ASSERT_EQ(0, (int)protocol_data_resp_status);
    MTEST_ASSERT_EQ((int)sizeof(value), (int)protocol_data_resp_value_len);
    MTEST_ASSERT_MEM_EQ(&value, protocol_data_resp_value, sizeof(value));

    /* Compressed stored values must be returned as raw bytes (no ambiguity). */
    protocol_data_resp_called = 0;
    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_DATA_REQUEST;
    msg.msg_id = 125U;
    memcpy(msg.dst, b_security.node_pubkey, 32U);
    memcpy(msg.payload, "big", 3U);
    msg.payload_len = 3U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_send(&a_proto, &msg));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&b_proto));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&a_proto));
    MTEST_ASSERT_EQ(1, protocol_data_resp_called);
    MTEST_ASSERT_EQ(125, (int)protocol_data_resp_id);
    MTEST_ASSERT_EQ(0, (int)protocol_data_resp_status);
    MTEST_ASSERT_EQ((int)sizeof(big), (int)protocol_data_resp_value_len);
    MTEST_ASSERT_MEM_EQ(big, protocol_data_resp_value, sizeof(big));

    /* Missing key returns explicit not-found status */
    protocol_data_resp_called = 0;
    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_DATA_REQUEST;
    msg.msg_id = 124U;
    memcpy(msg.dst, b_security.node_pubkey, 32U);
    memcpy(msg.payload, "missing", 7U);
    msg.payload_len = 7U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_send(&a_proto, &msg));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&b_proto));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&a_proto));
    MTEST_ASSERT_EQ(1, protocol_data_resp_called);
    MTEST_ASSERT_EQ(124, (int)protocol_data_resp_id);
    MTEST_ASSERT_EQ(1, (int)protocol_data_resp_status);

    deinit_protocol_fixture(&a_proto, &a_transport, &a_security, &a_network, &a_data);
    deinit_protocol_fixture(&b_proto, &b_transport, &b_security, &b_network, &b_data);
}

MTEST_SUITE(protocol)
{
    MTEST_RUN(test_protocol_serialize_parse);
    MTEST_RUN(test_protocol_dispatch_gossip);
    MTEST_RUN(test_protocol_retry_failed_message);
    MTEST_RUN(test_protocol_custom_message);
    MTEST_RUN(test_protocol_broadcast_group_message);
    MTEST_RUN(test_protocol_lifecycle_ready);
    MTEST_RUN(test_protocol_send_no_endpoint_fails);
    MTEST_RUN(test_protocol_send_with_endpoint_hits_transport);
    MTEST_RUN(test_protocol_tick_drains_and_dispatches);
    MTEST_RUN(test_protocol_stun_propagation);
    MTEST_RUN(test_protocol_encrypted_on_packet);
    MTEST_RUN(test_protocol_spoofed_plaintext_cannot_authenticate_endpoint);
    MTEST_RUN(test_protocol_spoofed_plaintext_cannot_overwrite_authenticated_endpoint);
    MTEST_RUN(test_protocol_directed_send_to_pending_endpoint_fails_for_app);
    MTEST_RUN(test_protocol_encrypted_src_mismatch_rejected);
    MTEST_RUN(test_protocol_encrypted_from_unknown_endpoint_rejected);
    MTEST_RUN(test_protocol_preauth_message_rejected);
    MTEST_RUN(test_protocol_remote_data_request_response_roundtrip);
}

void run_protocol_suite(void)
{
    MTEST_SUITE_RUN(protocol);
}

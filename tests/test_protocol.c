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

static void protocol_custom_handler(const p2p_message_t *msg)
{
    protocol_custom_called++;
    protocol_custom_len = msg->payload_len;
    if (msg->payload_len > 0U) {
        memcpy(protocol_custom_payload, msg->payload, msg->payload_len);
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

    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.local_port = 0U;
    tcfg.retry_count = 2U;
    tcfg.retry_delay_ms = 1U;
    tcfg.rx_buf_size = sizeof(p2p_packet_t) * 4U;
    tcfg.tx_buf_size = sizeof(p2p_transport_retry_entry_t) * 4U;
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(transport, &tcfg));

    memset(&scfg, 0, sizeof(scfg));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(security, &scfg));
    if (remote_pubkey != NULL) {
        MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_handshake(security, remote_pubkey));
    }

    memcpy(self_id, security->node_pubkey, 32U);
    memset(&ncfg, 0, sizeof(ncfg));
    ncfg.gossip_interval_ms = 1000U;
    ncfg.sync_interval_ms = 1000U;
    ncfg.offline_timeout_ms = 1000U;
    ncfg.max_nodes = P2P_MAX_NODES;
    ncfg.max_groups = P2P_MAX_GROUPS;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_init(network, &ncfg, self_id));

    memset(&dcfg, 0, sizeof(dcfg));
    dcfg.max_vars = P2P_MAX_VARS;
    dcfg.max_subs = P2P_MAX_SUBS;
    dcfg.notify_min_interval_ms = 1U;
    dcfg.compress_data = true;
    dcfg.spool_size = 1U;
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_init(data, &dcfg));

    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.max_pending = P2P_MAX_PENDING;
    pcfg.retry_interval_ms = 1U;
    pcfg.retry_count = 1U;
    pcfg.custom_handler = protocol_custom_handler;
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
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    init_protocol_fixture(&proto, &transport, &security, &network, &data, remote_sec.node_pubkey);

    memset(&source_cfg, 0, sizeof(source_cfg));
    source_cfg.gossip_interval_ms = 1000U;
    source_cfg.sync_interval_ms = 1000U;
    source_cfg.offline_timeout_ms = 1000U;
    source_cfg.max_nodes = P2P_MAX_NODES;
    source_cfg.max_groups = P2P_MAX_GROUPS;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_init(&source_net, &source_cfg, remote_sec.node_pubkey));

    memset(&node_c, 0, sizeof(node_c));
    memset(node_c.node_id, 0xC3, sizeof(node_c.node_id));
    node_c.is_online = true;
    node_c.db_version = 2U;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_add_node(&source_net, &node_c));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_gossip_build_delta(&source_net, delta, &delta_len));

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_GOSSIP;
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

    memset(&remote_cfg, 0, sizeof(remote_cfg));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    init_protocol_fixture(&proto, &transport, &security, &network, &data, remote_sec.node_pubkey);

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_DATA_REQUEST;
    memcpy(msg.dst, remote_sec.node_pubkey, sizeof(msg.dst));
    memcpy(msg.payload, "k", 1U);
    msg.payload_len = 1U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_send(&proto, &msg));
    MTEST_ASSERT_EQ(1, (int)proto.pending_count);
    proto.pending[0].sent_at = 0U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_tick(&proto));
    MTEST_ASSERT_EQ(1, (int)proto.pending[0].retry_count);
    proto.pending[0].sent_at = 0U;
    MTEST_ASSERT_EQ(P2P_PROTO_ERR_RETRY, p2p_protocol_tick(&proto));
    MTEST_ASSERT_EQ(0, (int)proto.pending_count);

    deinit_protocol_fixture(&proto, &transport, &security, &network, &data);
    p2p_security_deinit(&remote_sec);
}

MTEST(test_protocol_custom_message)
{
    p2p_protocol_t proto;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    p2p_message_t msg;
    uint8_t buf[1024];
    size_t len = sizeof(buf);

    protocol_custom_called = 0;
    protocol_custom_len = 0U;
    init_protocol_fixture(&proto, &transport, &security, &network, &data, NULL);
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_register_handler(&proto, 0x80U, protocol_custom_handler));

    memset(&msg, 0, sizeof(msg));
    msg.type = 0x80U;
    memcpy(msg.payload, "fire", 4U);
    msg.payload_len = 4U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_serialize(&msg, buf, &len));
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_on_packet(&proto, buf, len, NULL, 0U));
    MTEST_ASSERT_EQ(1, protocol_custom_called);
    MTEST_ASSERT_EQ(4, (int)protocol_custom_len);
    MTEST_ASSERT_MEM_EQ("fire", protocol_custom_payload, 4U);

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
    static const uint8_t zero32[32] = {0};

    memset(&remote_cfg, 0, sizeof(remote_cfg));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&remote_sec, &remote_cfg));
    init_protocol_fixture(&proto, &transport, &security, &network, &data, remote_sec.node_pubkey);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_group_create(&network, group_hash));

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_CUSTOM;
    memcpy(msg.dst, remote_sec.node_pubkey, sizeof(msg.dst));
    memcpy(msg.payload, "grp", 3U);
    msg.payload_len = 3U;
    MTEST_ASSERT_EQ(P2P_PROTO_OK, p2p_protocol_broadcast(&proto, group_hash, &msg));
    MTEST_ASSERT_EQ(1, (int)proto.pending_count);
    MTEST_ASSERT_MEM_EQ(group_hash, proto.pending[0].msg.group_hash, sizeof(group_hash));
    MTEST_ASSERT_MEM_EQ(zero32, proto.pending[0].msg.dst, sizeof(zero32));

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

MTEST_SUITE(protocol)
{
    MTEST_RUN(test_protocol_serialize_parse);
    MTEST_RUN(test_protocol_dispatch_gossip);
    MTEST_RUN(test_protocol_retry_failed_message);
    MTEST_RUN(test_protocol_custom_message);
    MTEST_RUN(test_protocol_broadcast_group_message);
    MTEST_RUN(test_protocol_lifecycle_ready);
}

void run_protocol_suite(void)
{
    MTEST_SUITE_RUN(protocol);
}

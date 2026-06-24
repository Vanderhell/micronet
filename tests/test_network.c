#include "mtest.h"

#include "network/p2p_network.h"

#include <string.h>

static uint32_t net_fake_now_ms;
static int net_last_event_id;

static uint32_t test_net_now_ms(void)
{
    return net_fake_now_ms;
}

static void test_net_event_publish(const microbus_event_t *event, void *user)
{
    (void)user;
    if (event != NULL) {
        net_last_event_id = event->event_id;
    }
}

static void fill_node_id(uint8_t out[32], uint8_t seed)
{
    size_t i;
    for (i = 0U; i < 32U; ++i) {
        out[i] = (uint8_t)(seed + i);
    }
}

static void init_net(p2p_network_t *ctx, uint8_t self_seed)
{
    p2p_network_config_t cfg;
    uint8_t self_id[32];

    memset(&cfg, 0, sizeof(cfg));
    cfg.gossip_interval_ms = 1000U;
    cfg.sync_interval_ms = 1000U;
    cfg.offline_timeout_ms = 100U;
    cfg.max_nodes = P2P_MAX_NODES;
    cfg.max_groups = P2P_MAX_GROUPS;
    cfg.now_ms = test_net_now_ms;
    fill_node_id(self_id, self_seed);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_init(ctx, &cfg, self_id));
    ctx->event_publish = test_net_event_publish;
}

static p2p_node_t make_node(uint8_t seed, uint8_t invited_by_seed)
{
    p2p_node_t node;
    memset(&node, 0, sizeof(node));
    fill_node_id(node.node_id, seed);
    if (invited_by_seed != 0U) {
        fill_node_id(node.invited_by, invited_by_seed);
    }
    node.external_ip[0] = 127U;
    node.external_ip[3] = seed;
    node.external_port = (uint16_t)(4000U + seed);
    node.first_seen = net_fake_now_ms;
    node.last_seen = net_fake_now_ms;
    node.is_online = true;
    node.db_version = seed;
    return node;
}

MTEST(test_network_add_node)
{
    p2p_network_t ctx;
    p2p_node_t node;
    p2p_node_t out;

    net_fake_now_ms = 1000U;
    net_last_event_id = 0;
    init_net(&ctx, 1U);
    node = make_node(2U, 1U);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_add_node(&ctx, &node));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_find_node(&ctx, node.node_id, &out));
    MTEST_ASSERT_MEM_EQ(node.node_id, out.node_id, sizeof(node.node_id));
    p2p_network_deinit(&ctx);
}

MTEST(test_network_online_offline_detection)
{
    p2p_network_t ctx;
    p2p_node_t node;

    net_fake_now_ms = 1000U;
    net_last_event_id = 0;
    init_net(&ctx, 10U);
    node = make_node(11U, 10U);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_add_node(&ctx, &node));
    ctx.nodes[0].last_seen = 0U;
    net_fake_now_ms = 500U;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_tick(&ctx));
    MTEST_ASSERT_EQ(P2P_EVENT_NODE_OFFLINE, net_last_event_id);
    p2p_network_deinit(&ctx);
}

MTEST(test_network_group_create)
{
    p2p_network_t ctx;
    uint8_t group_hash[16];
    static const uint8_t zero16[16] = {0};

    net_fake_now_ms = 100U;
    init_net(&ctx, 20U);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_group_create(&ctx, group_hash));
    MTEST_ASSERT_TRUE(memcmp(group_hash, zero16, sizeof(group_hash)) != 0);
    p2p_network_deinit(&ctx);
}

MTEST(test_network_invite_and_join)
{
    p2p_network_t a;
    p2p_network_t b;
    p2p_node_t node_b;
    uint8_t group_hash[16];
    uint8_t members[P2P_MAX_MEMBERS][32];
    uint8_t count = 0U;

    net_fake_now_ms = 200U;
    init_net(&a, 30U);
    init_net(&b, 31U);
    node_b = make_node(31U, 30U);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_add_node(&a, &node_b));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_group_create(&a, group_hash));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_group_invite(&a, node_b.node_id, group_hash));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_group_join(&b, group_hash, a.groups[0].group_key));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_group_members(&a, group_hash, members, P2P_MAX_MEMBERS, &count));
    MTEST_ASSERT_EQ(2, (int)count);
    MTEST_ASSERT_MEM_EQ(node_b.node_id, members[1], 32U);
    p2p_network_deinit(&a);
    p2p_network_deinit(&b);
}

MTEST(test_network_gossip_spread)
{
    p2p_network_t a;
    p2p_network_t b;
    p2p_node_t node_c;
    p2p_node_t out;
    uint8_t delta[1024];
    size_t delta_len = sizeof(delta);

    net_fake_now_ms = 300U;
    init_net(&a, 40U);
    init_net(&b, 41U);
    node_c = make_node(42U, 40U);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_add_node(&a, &node_c));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_gossip_build_delta(&a, delta, &delta_len));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_on_gossip(&b, delta, delta_len));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_find_node(&b, node_c.node_id, &out));
    MTEST_ASSERT_MEM_EQ(node_c.node_id, out.node_id, 32U);
    p2p_network_deinit(&a);
    p2p_network_deinit(&b);
}

MTEST(test_network_db_sync)
{
    p2p_network_t ctx;
    p2p_node_t node;

    net_fake_now_ms = 400U;
    init_net(&ctx, 50U);
    node = make_node(52U, 50U);
    node.db_version = 1U;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_add_node(&ctx, &node));
    node.db_version = 5U;
    node.last_seen = 999U;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_sync_apply(&ctx, &node));
    MTEST_ASSERT_EQ(5, (int)ctx.nodes[0].db_version);
    MTEST_ASSERT_EQ(999, (int)ctx.nodes[0].last_seen);
    p2p_network_deinit(&ctx);
}

MTEST(test_network_web_of_trust)
{
    p2p_network_t ctx;
    p2p_node_t node_b;
    p2p_node_t node_c;
    p2p_node_t out_b;
    p2p_node_t out_c;

    net_fake_now_ms = 500U;
    init_net(&ctx, 60U);
    node_b = make_node(61U, 60U);
    node_c = make_node(62U, 61U);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_add_node(&ctx, &node_b));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_add_node(&ctx, &node_c));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_find_node(&ctx, node_b.node_id, &out_b));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_find_node(&ctx, node_c.node_id, &out_c));
    MTEST_ASSERT_MEM_EQ(ctx.self.node_id, out_b.invited_by, 32U);
    MTEST_ASSERT_MEM_EQ(node_b.node_id, out_c.invited_by, 32U);
    p2p_network_deinit(&ctx);
}

MTEST(test_network_group_leave)
{
    p2p_network_t ctx;
    uint8_t group_hash[16];
    uint8_t members[P2P_MAX_MEMBERS][32];
    uint8_t count = 0U;

    net_fake_now_ms = 600U;
    net_last_event_id = 0;
    init_net(&ctx, 70U);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_group_create(&ctx, group_hash));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_group_leave(&ctx, group_hash));
    MTEST_ASSERT_EQ(P2P_EVENT_GROUP_LEFT, net_last_event_id);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_group_members(&ctx, group_hash, members, P2P_MAX_MEMBERS, &count));
    MTEST_ASSERT_EQ(0, (int)count);
    p2p_network_deinit(&ctx);
}

MTEST(test_network_gossip_rejects_malformed_payload)
{
    p2p_network_t ctx;
    uint8_t bad_payload[4] = {2U, 1U, 0U, 0U};
    uint8_t members[P2P_MAX_MEMBERS][32];
    uint8_t count = 0U;

    net_fake_now_ms = 700U;
    init_net(&ctx, 80U);
    MTEST_ASSERT_EQ(P2P_NET_ERR_SYNC, p2p_network_on_gossip(&ctx, bad_payload, sizeof(bad_payload)));
    MTEST_ASSERT_EQ(0, (int)ctx.node_count);
    MTEST_ASSERT_EQ(P2P_NET_ERR_NOT_FOUND, p2p_network_group_members(&ctx, (const uint8_t[16]){1U}, members, P2P_MAX_MEMBERS, &count));
    p2p_network_deinit(&ctx);
}

MTEST(test_network_clock_wraparound_offline_detection)
{
    p2p_network_t ctx;
    p2p_node_t node;

    net_fake_now_ms = UINT32_MAX - 50U;
    init_net(&ctx, 90U);
    node = make_node(91U, 90U);
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_add_node(&ctx, &node));
    net_fake_now_ms = 75U;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_tick(&ctx));
    MTEST_ASSERT_FALSE(ctx.nodes[0].is_online);
    p2p_network_deinit(&ctx);
}

MTEST(test_network_gossip_preserves_unauthorized_state)
{
    p2p_network_t sender;
    p2p_network_t receiver;
    p2p_node_t node;
    p2p_node_t out;
    uint8_t delta[1024];
    size_t delta_len = sizeof(delta);

    net_fake_now_ms = 800U;
    init_net(&sender, 81U);
    init_net(&receiver, 82U);

    node = make_node(83U, 81U);
    node.db_version = 2U;
    node.is_authorized = false;
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_add_node(&sender, &node));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_gossip_build_delta(&sender, delta, &delta_len));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_on_gossip(&receiver, delta, delta_len));
    MTEST_ASSERT_EQ(P2P_NET_OK, p2p_network_find_node(&receiver, node.node_id, &out));
    MTEST_ASSERT_FALSE(out.is_authorized);
    MTEST_ASSERT_TRUE(out.is_online);
    p2p_network_deinit(&sender);
    p2p_network_deinit(&receiver);
}

MTEST_SUITE(network)
{
    MTEST_RUN(test_network_add_node);
    MTEST_RUN(test_network_online_offline_detection);
    MTEST_RUN(test_network_group_create);
    MTEST_RUN(test_network_invite_and_join);
    MTEST_RUN(test_network_gossip_spread);
    MTEST_RUN(test_network_db_sync);
    MTEST_RUN(test_network_web_of_trust);
    MTEST_RUN(test_network_group_leave);
    MTEST_RUN(test_network_gossip_rejects_malformed_payload);
    MTEST_RUN(test_network_gossip_preserves_unauthorized_state);
    MTEST_RUN(test_network_clock_wraparound_offline_detection);
}

void run_network_suite(void)
{
    MTEST_SUITE_RUN(network);
}

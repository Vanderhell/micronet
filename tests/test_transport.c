#include "mtest.h"

#include "transport/p2p_transport.h"

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
static void test_sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <unistd.h>
static void test_sleep_ms(unsigned ms) { usleep(ms * 1000U); }
#endif

static p2p_transport_config_t transport_test_config(uint16_t port)
{
    p2p_transport_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.stun_host = "stun.l.google.com";
    cfg.stun_port = 19302U;
    cfg.local_port = port;
    cfg.heartbeat_ms = 50U;
    cfg.timeout_ms = 250U;
    cfg.retry_count = 2U;
    cfg.retry_delay_ms = 10U;
    cfg.rx_buf_size = sizeof(p2p_packet_t) * 4U;
    cfg.tx_buf_size = sizeof(p2p_transport_retry_entry_t) * 4U;
    return cfg;
}

static void init_pair(p2p_transport_t *a, p2p_transport_t *b, uint16_t port_a, uint16_t port_b)
{
    p2p_transport_config_t cfg_a = transport_test_config(port_a);
    p2p_transport_config_t cfg_b = transport_test_config(port_b);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(a, &cfg_a));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(b, &cfg_b));
}

static void deinit_pair(p2p_transport_t *a, p2p_transport_t *b)
{
    p2p_transport_deinit(a);
    p2p_transport_deinit(b);
}

MTEST(test_transport_stun_resolve)
{
    p2p_transport_t ctx;
    p2p_transport_config_t cfg = transport_test_config(0U);
    uint8_t ip[4];
    uint16_t port = 0U;

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&ctx, &cfg));
    if (p2p_transport_stun_resolve(&ctx) != P2P_OK) {
        p2p_transport_deinit(&ctx);
        MTEST_SKIP("STUN unavailable in current environment");
    }

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_get_external_addr(&ctx, ip, &port));
    MTEST_ASSERT_NE(0, ip[0] | ip[1] | ip[2] | ip[3]);
    MTEST_ASSERT_NE(0, port);
    p2p_transport_deinit(&ctx);
}

MTEST(test_transport_loopback_send_recv)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "hello-transport";

    init_pair(&a, &b, 32101U, 32102U);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32102U, payload, sizeof(payload)));
    test_sleep_ms(10U);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ((int)sizeof(payload), (int)pkt.len);
    MTEST_ASSERT_MEM_EQ(payload, pkt.data, sizeof(payload));
    MTEST_ASSERT_GT(pkt.seq, 0);
    deinit_pair(&a, &b);
}

MTEST(test_transport_compression_flag)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    uint8_t payload[128];

    memset(payload, 'A', sizeof(payload));
    init_pair(&a, &b, 32111U, 32112U);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32112U, payload, sizeof(payload)));
    test_sleep_ms(10U);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_TRUE((pkt.flags & P2P_PACKET_FLAG_COMPRESSED) != 0U);
    MTEST_ASSERT_EQ((int)sizeof(payload), (int)pkt.len);
    MTEST_ASSERT_MEM_EQ(payload, pkt.data, sizeof(payload));
    deinit_pair(&a, &b);
}

MTEST(test_transport_retry_delivery)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "retry-me";

    init_pair(&a, &b, 32121U, 32122U);
    a.retry_ctx.retry_delay_ms = 1U;
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32122U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(1, (int)a.tx_ring.count);
    test_sleep_ms(5U);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_tick(&a));
    test_sleep_ms(5U);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_MEM_EQ(payload, pkt.data, sizeof(payload));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_tick(&a));
    MTEST_ASSERT_EQ(0, (int)a.tx_ring.count);
    deinit_pair(&a, &b);
}

MTEST(test_transport_heartbeat_timeout)
{
    p2p_transport_t ctx;
    p2p_transport_config_t cfg = transport_test_config(0U);

    cfg.timeout_ms = 1U;
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&ctx, &cfg));
    ctx.timeout_timer.last_ms = ctx.hal->now_ms() - 10U;
    MTEST_ASSERT_EQ(P2P_ERR_TIMEOUT, p2p_transport_tick(&ctx));
    p2p_transport_deinit(&ctx);
}

MTEST(test_transport_buffer_full)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_transport_config_t cfg_a = transport_test_config(32131U);
    p2p_transport_config_t cfg_b = transport_test_config(32132U);
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "buf";

    cfg_b.rx_buf_size = sizeof(p2p_packet_t);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&a, &cfg_a));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&b, &cfg_b));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32132U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32132U, payload, sizeof(payload)));
    test_sleep_ms(10U);
    MTEST_ASSERT_EQ(P2P_ERR_BUF_FULL, p2p_transport_tick(&b));
    deinit_pair(&a, &b);
}

MTEST_SUITE(transport)
{
    MTEST_RUN(test_transport_stun_resolve);
    MTEST_RUN(test_transport_loopback_send_recv);
    MTEST_RUN(test_transport_compression_flag);
    MTEST_RUN(test_transport_retry_delivery);
    MTEST_RUN(test_transport_heartbeat_timeout);
    MTEST_RUN(test_transport_buffer_full);
}

void run_transport_suite(void)
{
    MTEST_SUITE_RUN(transport);
}

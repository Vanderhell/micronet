#include "mtest.h"

#include "transport/p2p_transport.h"

#include <string.h>

#define FAKE_SOCKET_CAPACITY 8U
#define FAKE_QUEUE_CAPACITY 64U

typedef struct {
    uint8_t src_ip[4];
    uint16_t src_port;
    uint8_t wire[P2P_TRANSPORT_HEADER_SIZE + P2P_MAX_PACKET_SIZE];
    size_t wire_len;
} fake_wire_t;

typedef struct {
    int in_use;
    uint16_t port;
    fake_wire_t queue[FAKE_QUEUE_CAPACITY];
    size_t head;
    size_t count;
} fake_socket_t;

static fake_socket_t fake_sockets[FAKE_SOCKET_CAPACITY];
static uint32_t fake_now_ms;
static int fake_drop_first_ack;

static uint32_t fake_now(void)
{
    return fake_now_ms;
}

static int fake_sock_open(uint16_t port)
{
    size_t i;

    for (i = 0U; i < FAKE_SOCKET_CAPACITY; ++i) {
        if (!fake_sockets[i].in_use) {
            memset(&fake_sockets[i], 0, sizeof(fake_sockets[i]));
            fake_sockets[i].in_use = 1;
            fake_sockets[i].port = port;
            return (int)i;
        }
    }

    return -1;
}

static void fake_sock_close(int fd)
{
    if (fd < 0 || (size_t)fd >= FAKE_SOCKET_CAPACITY) {
        return;
    }

    memset(&fake_sockets[fd], 0, sizeof(fake_sockets[fd]));
}

static int fake_find_socket_by_port(uint16_t port)
{
    size_t i;

    for (i = 0U; i < FAKE_SOCKET_CAPACITY; ++i) {
        if (fake_sockets[i].in_use && fake_sockets[i].port == port) {
            return (int)i;
        }
    }

    return -1;
}

static int fake_sock_send(int fd,
                          const uint8_t *ip,
                          uint16_t port,
                          const uint8_t *data,
                          size_t len)
{
    fake_socket_t *sender;
    fake_socket_t *target;
    fake_wire_t *slot;
    uint8_t flags;
    int target_fd;

    if (fd < 0 || (size_t)fd >= FAKE_SOCKET_CAPACITY || data == NULL || len == 0U ||
        len > sizeof(((fake_wire_t *)0)->wire)) {
        return -1;
    }

    sender = &fake_sockets[fd];
    if (!sender->in_use) {
        return -1;
    }

    flags = data[3];
    if ((flags & P2P_PACKET_FLAG_ACK) != 0U && fake_drop_first_ack) {
        fake_drop_first_ack = 0;
        return (int)len;
    }

    target_fd = fake_find_socket_by_port(port);
    if (target_fd < 0) {
        return (int)len;
    }

    target = &fake_sockets[target_fd];
    if (target->count >= FAKE_QUEUE_CAPACITY) {
        return -1;
    }

    slot = &target->queue[(target->head + target->count) % FAKE_QUEUE_CAPACITY];
    memset(slot, 0, sizeof(*slot));
    if (ip != NULL) {
        memcpy(slot->src_ip, ip, sizeof(slot->src_ip));
    } else {
        slot->src_ip[0] = 127U;
        slot->src_ip[1] = 0U;
        slot->src_ip[2] = 0U;
        slot->src_ip[3] = 1U;
    }
    slot->src_port = sender->port;
    memcpy(slot->wire, data, len);
    slot->wire_len = len;
    target->count++;
    return (int)len;
}

static int fake_sock_recv(int fd,
                          uint8_t *ip,
                          uint16_t *port,
                          uint8_t *buf,
                          size_t buf_len)
{
    fake_socket_t *sock;
    fake_wire_t *slot;

    if (fd < 0 || (size_t)fd >= FAKE_SOCKET_CAPACITY || buf == NULL || buf_len == 0U) {
        return -1;
    }

    sock = &fake_sockets[fd];
    if (!sock->in_use || sock->count == 0U) {
        return 0;
    }

    slot = &sock->queue[sock->head];
    if (slot->wire_len > buf_len) {
        return -1;
    }

    if (ip != NULL) {
        memcpy(ip, slot->src_ip, sizeof(slot->src_ip));
    }
    if (port != NULL) {
        *port = slot->src_port;
    }
    memcpy(buf, slot->wire, slot->wire_len);

    sock->head = (sock->head + 1U) % FAKE_QUEUE_CAPACITY;
    sock->count--;
    return (int)slot->wire_len;
}

static const p2p_hal_t fake_hal = {
    fake_sock_open,
    fake_sock_close,
    fake_sock_send,
    fake_sock_recv,
    fake_now
};

static void reset_fake_transport(void)
{
    memset(fake_sockets, 0, sizeof(fake_sockets));
    fake_now_ms = 1000U;
    fake_drop_first_ack = 0;
}

static p2p_transport_config_t transport_test_config(uint16_t port)
{
    p2p_transport_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.local_port = port;
    cfg.heartbeat_ms = 50U;
    cfg.timeout_ms = 250U;
    cfg.retry_count = 2U;
    cfg.retry_delay_ms = 10U;
    cfg.rx_buf_size = sizeof(p2p_packet_t) * 64U;
    cfg.tx_buf_size = sizeof(p2p_transport_retry_entry_t) * 64U;
    cfg.hal = &fake_hal;
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

static void enqueue_wire(uint16_t dst_port,
                         uint16_t src_port,
                         const uint8_t *wire,
                         size_t len)
{
    int dst_fd;
    fake_socket_t *target;
    fake_wire_t *slot;

    dst_fd = fake_find_socket_by_port(dst_port);
    MTEST_ASSERT_TRUE(dst_fd >= 0);

    target = &fake_sockets[dst_fd];
    MTEST_ASSERT_TRUE(target->count < FAKE_QUEUE_CAPACITY);

    slot = &target->queue[(target->head + target->count) % FAKE_QUEUE_CAPACITY];
    memset(slot, 0, sizeof(*slot));
    slot->src_ip[0] = 127U;
    slot->src_ip[1] = 0U;
    slot->src_ip[2] = 0U;
    slot->src_ip[3] = 1U;
    slot->src_port = src_port;
    memcpy(slot->wire, wire, len);
    slot->wire_len = len;
    target->count++;
}

static void build_bad_wire(uint8_t wire[P2P_TRANSPORT_HEADER_SIZE])
{
    memset(wire, 0, P2P_TRANSPORT_HEADER_SIZE);
    wire[0] = (uint8_t)'X';
    wire[1] = P2P_TRANSPORT_MAGIC_1;
    wire[2] = P2P_TRANSPORT_VERSION;
    wire[3] = 0U;
    wire[4] = 0U;
    wire[5] = 1U;
    wire[6] = 0U;
    wire[7] = 0U;
}

MTEST(test_transport_retry_count_zero_sends_once_without_retry_entry)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "no-retry";
    p2p_transport_config_t cfg_a;
    p2p_transport_config_t cfg_b;

    reset_fake_transport();
    cfg_a = transport_test_config(32101U);
    cfg_a.retry_count = 0U;
    cfg_b = transport_test_config(32102U);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&a, &cfg_a));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&b, &cfg_b));

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32102U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(0, (int)a.tx_ring.count);
    MTEST_ASSERT_EQ(1, (int)a.stats.sent);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ((int)sizeof(payload), (int)pkt.len);
    MTEST_ASSERT_MEM_EQ(payload, pkt.data, sizeof(payload));

    deinit_pair(&a, &b);
}

MTEST(test_transport_send_ack_clears_retry_entry)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "hello-transport";

    reset_fake_transport();
    init_pair(&a, &b, 32111U, 32112U);

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32112U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(1, (int)a.tx_ring.count);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ((int)sizeof(payload), (int)pkt.len);
    MTEST_ASSERT_MEM_EQ(payload, pkt.data, sizeof(payload));
    MTEST_ASSERT_EQ(P2P_ERR_NO_PACKET, p2p_transport_recv(&a, &pkt));
    MTEST_ASSERT_EQ(0, (int)a.tx_ring.count);
    MTEST_ASSERT_EQ(1, (int)b.stats.received);
    MTEST_ASSERT_EQ(1, (int)b.stats.ack_sent);

    deinit_pair(&a, &b);
}

MTEST(test_transport_lost_ack_retransmit_and_dedup)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "retry-me";

    reset_fake_transport();
    fake_drop_first_ack = 1;
    init_pair(&a, &b, 32121U, 32122U);
    a.retry_ctx.retry_delay_ms = 1U;

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32122U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(P2P_ERR_NO_PACKET, p2p_transport_recv(&a, &pkt));
    MTEST_ASSERT_EQ(1, (int)a.tx_ring.count);

    fake_now_ms += 5U;
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_tick(&a));
    MTEST_ASSERT_EQ(1, (int)a.stats.retry_sent);
    MTEST_ASSERT_EQ(P2P_ERR_NO_PACKET, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(1, (int)b.stats.duplicate_dropped);

    MTEST_ASSERT_EQ(P2P_ERR_NO_PACKET, p2p_transport_recv(&a, &pkt));
    MTEST_ASSERT_EQ(0, (int)a.tx_ring.count);

    deinit_pair(&a, &b);
}

MTEST(test_transport_same_seq_from_two_endpoints_is_not_duplicate)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_transport_t c;
    p2p_packet_t pkt1;
    p2p_packet_t pkt2;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "same-seq";

    reset_fake_transport();
    init_pair(&a, &b, 32131U, 32132U);
    {
        p2p_transport_config_t cfg_c = transport_test_config(32133U);
        MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&c, &cfg_c));
    }

    a.next_seq = 42U;
    c.next_seq = 42U;
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32132U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&c, ip, 32132U, payload, sizeof(payload)));

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt1));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt2));
    MTEST_ASSERT_EQ(42, (int)pkt1.seq);
    MTEST_ASSERT_EQ(42, (int)pkt2.seq);
    MTEST_ASSERT_NE((int)pkt1.remote_port, (int)pkt2.remote_port);

    deinit_pair(&a, &b);
    p2p_transport_deinit(&c);
}

MTEST(test_transport_reorder_within_window)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "window";

    reset_fake_transport();
    init_pair(&a, &b, 32141U, 32142U);

    a.next_seq = 2U;
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32142U, payload, sizeof(payload)));
    a.next_seq = 1U;
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32142U, payload, sizeof(payload)));

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(2, (int)pkt.seq);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(1, (int)pkt.seq);

    deinit_pair(&a, &b);
}

MTEST(test_transport_wraparound_65535_to_1)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "wrap";

    reset_fake_transport();
    init_pair(&a, &b, 32151U, 32152U);

    a.next_seq = 65535U;
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32152U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(1U, a.next_seq);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32152U, payload, sizeof(payload)));

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(65535, (int)pkt.seq);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(1, (int)pkt.seq);

    deinit_pair(&a, &b);
}

MTEST(test_transport_old_duplicate_after_window_shift_is_accepted_again)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "shift";
    uint16_t seq;

    reset_fake_transport();
    init_pair(&a, &b, 32161U, 32162U);

    a.next_seq = 1U;
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32162U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(1, (int)pkt.seq);

    for (seq = 2U; seq <= (uint16_t)(P2P_TRANSPORT_DEDUP_WINDOW_SIZE + 2U); ++seq) {
        a.next_seq = seq;
        MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32162U, payload, sizeof(payload)));
    }

    for (seq = 2U; seq <= (uint16_t)(P2P_TRANSPORT_DEDUP_WINDOW_SIZE + 2U); ++seq) {
        MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    }

    a.next_seq = 1U;
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32162U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(1, (int)pkt.seq);

    deinit_pair(&a, &b);
}

MTEST(test_transport_malformed_packet_not_acked_or_queued)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_packet_t pkt;
    uint8_t wire[P2P_TRANSPORT_HEADER_SIZE];

    reset_fake_transport();
    init_pair(&a, &b, 32171U, 32172U);

    build_bad_wire(wire);
    enqueue_wire(32172U, 32171U, wire, sizeof(wire));
    MTEST_ASSERT_EQ(P2P_ERR_BAD_PACKET, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(0, (int)b.rx_ring.count);
    MTEST_ASSERT_EQ(P2P_ERR_NO_PACKET, p2p_transport_recv(&a, &pkt));

    deinit_pair(&a, &b);
}

MTEST(test_transport_buffer_full_is_deterministic_and_preserves_cache)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_transport_config_t cfg_a;
    p2p_transport_config_t cfg_b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "buf";

    reset_fake_transport();
    cfg_a = transport_test_config(32181U);
    cfg_b = transport_test_config(32182U);
    cfg_b.rx_buf_size = sizeof(p2p_packet_t);

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&a, &cfg_a));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&b, &cfg_b));

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32182U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_tick(&b));
    MTEST_ASSERT_EQ(1, (int)b.rx_ring.count);

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32182U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(P2P_ERR_BUF_FULL, p2p_transport_tick(&b));
    MTEST_ASSERT_EQ(1, (int)b.rx_ring.count);

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(1, (int)pkt.seq);

    a.next_seq = 2U;
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32182U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(P2P_ERR_NO_PACKET, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(1, (int)b.stats.duplicate_dropped);

    deinit_pair(&a, &b);
}

MTEST(test_transport_retry_queue_full_fails_before_send)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_transport_config_t cfg_a;
    p2p_transport_config_t cfg_b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "cap";

    reset_fake_transport();
    cfg_a = transport_test_config(32191U);
    cfg_a.tx_buf_size = sizeof(p2p_transport_retry_entry_t);
    cfg_b = transport_test_config(32192U);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&a, &cfg_a));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&b, &cfg_b));

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32192U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(1, (int)a.stats.sent);
    MTEST_ASSERT_EQ(P2P_ERR_BUF_FULL, p2p_transport_send(&a, ip, 32192U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(1, (int)a.stats.sent);

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(P2P_ERR_NO_PACKET, p2p_transport_recv(&b, &pkt));

    deinit_pair(&a, &b);
}

MTEST(test_transport_retry_exhaustion_does_not_block_other_due_entries)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_transport_retry_entry_t *entries;
    p2p_transport_config_t cfg_a;
    p2p_transport_config_t cfg_b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};
    static const uint8_t payload[] = "retry";

    reset_fake_transport();
    cfg_a = transport_test_config(32201U);
    cfg_a.retry_count = 1U;
    cfg_a.retry_delay_ms = 1U;
    cfg_b = transport_test_config(32202U);
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&a, &cfg_a));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_init(&b, &cfg_b));

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32202U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_send(&a, ip, 32202U, payload, sizeof(payload)));
    MTEST_ASSERT_EQ(2, (int)a.tx_ring.count);

    entries = (p2p_transport_retry_entry_t *)a.tx_ring.storage;
    entries[0].retries_done = a.retry_ctx.max_retries;
    entries[0].last_send_ms = fake_now_ms - 10U;
    entries[1].last_send_ms = fake_now_ms - 10U;

    fake_now_ms += 5U;
    MTEST_ASSERT_EQ(P2P_ERR_RETRY, p2p_transport_tick(&a));
    MTEST_ASSERT_EQ(1, (int)a.stats.retry_exhausted);
    MTEST_ASSERT_EQ(1, (int)a.stats.retry_sent);
    MTEST_ASSERT_EQ(1, (int)a.tx_ring.count);

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_recv(&b, &pkt));

    deinit_pair(&a, &b);
}

MTEST(test_transport_heartbeat_does_not_create_app_packet)
{
    p2p_transport_t a;
    p2p_transport_t b;
    p2p_packet_t pkt;
    static const uint8_t ip[4] = {127U, 0U, 0U, 1U};

    reset_fake_transport();
    init_pair(&a, &b, 32211U, 32212U);

    a.last_peer_valid = true;
    memcpy(a.last_peer_ip, ip, sizeof(a.last_peer_ip));
    a.last_peer_port = 32212U;
    a.heartbeat_timer.last_ms = fake_now_ms - a.heartbeat_timer.interval_ms;

    MTEST_ASSERT_EQ(P2P_OK, p2p_transport_tick(&a));
    MTEST_ASSERT_EQ(1, (int)a.stats.sent);
    MTEST_ASSERT_EQ(P2P_ERR_NO_PACKET, p2p_transport_recv(&b, &pkt));
    MTEST_ASSERT_EQ(0, (int)b.stats.received);
    MTEST_ASSERT_EQ(1, (int)b.stats.ack_sent);

    deinit_pair(&a, &b);
}

MTEST_SUITE(transport)
{
    MTEST_RUN(test_transport_retry_count_zero_sends_once_without_retry_entry);
    MTEST_RUN(test_transport_send_ack_clears_retry_entry);
    MTEST_RUN(test_transport_lost_ack_retransmit_and_dedup);
    MTEST_RUN(test_transport_same_seq_from_two_endpoints_is_not_duplicate);
    MTEST_RUN(test_transport_reorder_within_window);
    MTEST_RUN(test_transport_wraparound_65535_to_1);
    MTEST_RUN(test_transport_old_duplicate_after_window_shift_is_accepted_again);
    MTEST_RUN(test_transport_malformed_packet_not_acked_or_queued);
    MTEST_RUN(test_transport_buffer_full_is_deterministic_and_preserves_cache);
    MTEST_RUN(test_transport_retry_queue_full_fails_before_send);
    MTEST_RUN(test_transport_retry_exhaustion_does_not_block_other_due_entries);
    MTEST_RUN(test_transport_heartbeat_does_not_create_app_packet);
}

void run_transport_suite(void)
{
    MTEST_SUITE_RUN(transport);
}

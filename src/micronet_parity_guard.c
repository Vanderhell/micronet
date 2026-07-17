#include "micronet.h"

#include "micronet_internal.h"
#include "data/p2p_data.h"
#include "network/p2p_network.h"
#include "protocol/p2p_protocol.h"
#include "security/p2p_security.h"
#include "transport/p2p_transport.h"

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define MNET_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#else
#define MNET_STATIC_ASSERT(cond, msg) typedef char mnet_static_assert_##__LINE__[(cond) ? 1 : -1]
#endif

#if defined(__GNUC__) || defined(__clang__)
#define MNET_UNUSED __attribute__((unused))
#else
#define MNET_UNUSED
#endif

MNET_STATIC_ASSERT(MNET_MAX_PUBLIC_PAYLOAD == 375U, "micronet payload cap drifted");

static void MNET_UNUSED micronet_parity_guard_touch(void)
{
    (void)mnet_init;
    (void)mnet_tick;
    (void)mnet_get_node_id;
    (void)mnet_group_create;
    (void)mnet_group_join;
    (void)mnet_group_leave;
    (void)mnet_peer_add_ip;
    (void)mnet_peer_connect;
    (void)mnet_publish;
    (void)mnet_update;
    (void)mnet_request;
    (void)mnet_subscribe;
    (void)mnet_unsubscribe;
    (void)mnet_list_vars;
    (void)mnet_query;
    (void)mnet_get_metrics;
    (void)mnet_send_custom;
    (void)mnet_send_group_custom;
    (void)mnet_broadcast_custom;
    (void)mnet_discover_lan;
    (void)mnet_register_handler;

    (void)mnet_internal_context;
    (void)p2p_data_collect_metrics;
    (void)p2p_network_group_create;
    (void)p2p_network_group_join;
    (void)p2p_network_group_leave;
    (void)p2p_protocol_collect_metrics;
    (void)p2p_security_random_fill;
    (void)p2p_transport_init;
}

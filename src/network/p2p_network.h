#ifndef P2P_NETWORK_H
#define P2P_NETWORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef P2P_MAX_GROUPS
#define P2P_MAX_GROUPS 8U
#endif

#ifndef P2P_MAX_MEMBERS
#define P2P_MAX_MEMBERS 32U
#endif

#ifndef P2P_MAX_NODES
#define P2P_MAX_NODES 32U
#endif

#ifndef P2P_MICROFSM_T_DEFINED
#define P2P_MICROFSM_T_DEFINED
typedef struct {
    uint8_t state;
} microfsm_t;
#endif

#ifndef P2P_MICROTIMER_T_DEFINED
#define P2P_MICROTIMER_T_DEFINED
typedef struct {
    uint32_t interval_ms;
    uint32_t last_ms;
    bool armed;
} microtimer_t;
#endif

#ifndef P2P_MICROBUS_EVENT_T_DEFINED
#define P2P_MICROBUS_EVENT_T_DEFINED
typedef struct {
    uint8_t event_id;
    const void *payload;
    size_t payload_len;
} microbus_event_t;
#endif

typedef struct {
    uint8_t node_id[32];
    uint8_t external_ip[4];
    uint16_t external_port;
    uint8_t invited_by[32];
    uint32_t first_seen;
    uint32_t last_seen;
    uint8_t group_hashes[P2P_MAX_GROUPS][16];
    uint8_t group_count;
    bool is_online;
    bool is_authorized;
    uint32_t db_version;
} p2p_node_t;

typedef struct {
    uint8_t group_hash[16];
    uint8_t group_key[16];
    uint8_t created_by[32];
    uint32_t created_at;
    uint8_t members[P2P_MAX_MEMBERS][32];
    uint8_t member_count;
    uint32_t db_version;
} p2p_group_t;

typedef struct {
    uint32_t gossip_interval_ms;
    uint32_t sync_interval_ms;
    uint32_t offline_timeout_ms;
    uint8_t max_nodes;
    uint8_t max_groups;
} p2p_network_config_t;

typedef enum {
    P2P_EVENT_NODE_ONLINE = 1,
    P2P_EVENT_NODE_OFFLINE = 2,
    P2P_EVENT_NODE_NEW = 3,
    P2P_EVENT_GROUP_INVITE = 4,
    P2P_EVENT_GROUP_JOINED = 5,
    P2P_EVENT_GROUP_LEFT = 6,
    P2P_EVENT_DB_SYNCED = 7
} p2p_network_event_id_t;

typedef enum {
    P2P_NET_OK = 0,
    P2P_NET_ERR_NODE_EXISTS = -1,
    P2P_NET_ERR_NODE_FULL = -2,
    P2P_NET_ERR_NOT_FOUND = -3,
    P2P_NET_ERR_NOT_MEMBER = -4,
    P2P_NET_ERR_GROUP_FULL = -5,
    P2P_NET_ERR_NO_INVITE = -6,
    P2P_NET_ERR_SYNC = -7,
} p2p_net_err_t;

typedef struct {
    p2p_node_t self;
    p2p_node_t nodes[P2P_MAX_NODES];
    uint8_t node_count;
    p2p_group_t groups[P2P_MAX_GROUPS];
    uint8_t group_count;
    microfsm_t fsm;
    microtimer_t gossip_timer;
    microtimer_t sync_timer;

    p2p_network_config_t config;
    uint32_t (*now_ms)(void);
    void (*event_publish)(const microbus_event_t *event, void *user);
    void *event_user;
    uint32_t last_sync_ms;
    uint32_t last_sync_version;
    uint32_t last_db_version;
} p2p_network_t;

p2p_net_err_t p2p_network_init(p2p_network_t *ctx, const p2p_network_config_t *cfg,
                               const uint8_t self_node_id[32]);
p2p_net_err_t p2p_network_add_node(p2p_network_t *ctx, const p2p_node_t *node);
p2p_net_err_t p2p_network_remove_node(p2p_network_t *ctx, const uint8_t node_id[32]);
p2p_net_err_t p2p_network_find_node(p2p_network_t *ctx, const uint8_t node_id[32],
                                    p2p_node_t *out);
p2p_net_err_t p2p_network_set_online(p2p_network_t *ctx, const uint8_t node_id[32], bool online);
p2p_net_err_t p2p_network_peer_join_group(p2p_network_t *ctx, const uint8_t node_id[32],
                                          const uint8_t group_hash[16]);
p2p_net_err_t p2p_network_peer_leave_group(p2p_network_t *ctx, const uint8_t node_id[32],
                                           const uint8_t group_hash[16]);
p2p_net_err_t p2p_network_group_exists(p2p_network_t *ctx, const uint8_t group_hash[16],
                                       bool *out_exists);
p2p_net_err_t p2p_network_group_create(p2p_network_t *ctx, uint8_t out_group_hash[16]);
p2p_net_err_t p2p_network_group_invite(p2p_network_t *ctx, const uint8_t node_id[32],
                                       const uint8_t group_hash[16]);
p2p_net_err_t p2p_network_group_join(p2p_network_t *ctx, const uint8_t group_hash[16],
                                     const uint8_t group_key[16]);
p2p_net_err_t p2p_network_group_leave(p2p_network_t *ctx, const uint8_t group_hash[16]);
p2p_net_err_t p2p_network_group_members(p2p_network_t *ctx, const uint8_t group_hash[16],
                                        uint8_t out_members[][32], uint8_t *count);
p2p_net_err_t p2p_network_tick(p2p_network_t *ctx);
p2p_net_err_t p2p_network_on_gossip(p2p_network_t *ctx, const uint8_t *msg, size_t len);
void p2p_network_deinit(p2p_network_t *ctx);

p2p_net_err_t p2p_network_gossip_build_delta(p2p_network_t *ctx, uint8_t *out, size_t *out_len);
p2p_net_err_t p2p_network_sync_apply(p2p_network_t *ctx, const p2p_node_t *node);

#endif

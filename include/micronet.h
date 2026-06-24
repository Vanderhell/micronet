#ifndef MICRONET_H
#define MICRONET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MNET_MAX_GROUPS
#define MNET_MAX_GROUPS 8U
#endif

#ifndef MNET_MAX_NODES
#define MNET_MAX_NODES 32U
#endif

typedef struct mnet_message_s {
    uint8_t type;
    uint16_t msg_id;
    uint32_t timestamp;
    uint8_t src[32];
    uint8_t dst[32];
    uint8_t group_hash[16];
    uint8_t payload[512];
    size_t payload_len;
} mnet_message_t;

typedef uint8_t mnet_group_id_t[16];
typedef uint32_t mnet_group_mask_t;

typedef enum {
    MNET_MODE_LAN_ONLY = 0,
    MNET_MODE_MANUAL_PEERS = 1,
    MNET_MODE_STUN_EXPERIMENTAL = 2,
} mnet_network_mode_t;

typedef struct {
    uint8_t node_id[32];
    uint8_t ip[4];
    uint16_t port;
    uint32_t last_seen_ms;
    bool is_online;
    bool is_authorized;
    uint8_t group_count;
    mnet_group_id_t groups[MNET_MAX_GROUPS];
} mnet_peer_info_t;

typedef struct {
    uint8_t bytes[256];
    size_t len;
} mnet_row_t;

typedef struct {
    uint32_t uptime_s;
    uint32_t free_heap;
    uint8_t connected_nodes;
    uint8_t group_count;
    uint32_t packets_sent;
    uint32_t packets_recv;
    uint32_t errors;
    uint8_t health_score;
} mnet_metrics_t;

typedef enum {
    MNET_OK = 0,
    MNET_ERR_NOT_INIT = -1,
    MNET_ERR_INVALID_ARG = -2,
    MNET_ERR_NOT_FOUND = -3,
    MNET_ERR_ACCESS = -4,
    MNET_ERR_OFFLINE = -5,
    MNET_ERR_TIMEOUT = -6,
    MNET_ERR_FULL = -7,
    MNET_ERR_CRYPTO = -8,
    MNET_ERR_TRANSPORT = -9,
    MNET_ERR_INTERNAL = -10,
    MNET_ERR_PROTOCOL = -11,
} mnet_err_t;

typedef struct {
    uint8_t group_hash[16];
    uint8_t group_key[16];
} mnet_group_seed_t;

typedef struct {
    uint8_t node_privkey[32];
    const char *node_name;
    mnet_network_mode_t network_mode;
    bool stun_enabled;
    const char *stun_host;
    uint16_t stun_port;
    uint16_t local_port;
    mnet_group_seed_t groups[MNET_MAX_GROUPS];
    uint8_t group_count;
    uint32_t heartbeat_ms;
    uint32_t offline_timeout_ms;
    uint32_t retry_interval_ms;
    uint8_t retry_count;
    uint8_t max_nodes;
    uint8_t max_vars;
    uint8_t max_pending;
    uint8_t log_level;
    void (*on_node_online)(const uint8_t node_id[32]);
    void (*on_node_offline)(const uint8_t node_id[32]);
    void (*on_custom_msg)(const mnet_message_t *msg);
} mnet_config_t;

mnet_err_t mnet_init(const mnet_config_t *cfg);
mnet_err_t mnet_tick(void);
mnet_err_t mnet_get_node_id(uint8_t out_node_id[32]);
void mnet_deinit(void);

bool mnet_node_is_online(const uint8_t node_id[32]);
mnet_err_t mnet_node_list_online(uint8_t out[][32], uint8_t *count);
mnet_err_t mnet_node_list_all(uint8_t out[][32], uint8_t *count);
mnet_err_t mnet_node_invited_by(const uint8_t node_id[32], uint8_t out_inviter[32]);

/* If node_id is NULL, a deterministic placeholder id is synthesized from ip:port.
 * That placeholder is for routing only and is not a device identity. */
mnet_err_t mnet_peer_add_ip(const uint8_t node_id[32], const uint8_t ip[4], uint16_t port);
mnet_err_t mnet_peer_remove(const uint8_t node_id[32]);
mnet_err_t mnet_peer_list(mnet_peer_info_t *out_peers, uint8_t capacity, uint8_t *out_count);
mnet_err_t mnet_peer_join_group(const uint8_t node_id[32], const uint8_t group_hash[16]);
mnet_err_t mnet_peer_leave_group(const uint8_t node_id[32], const uint8_t group_hash[16]);

mnet_err_t mnet_group_create(uint8_t out_group_hash[16], uint8_t out_group_key[16]);
mnet_err_t mnet_group_invite(const uint8_t node_id[32], const uint8_t group_hash[16]);
mnet_err_t mnet_group_join(const uint8_t group_hash[16], const uint8_t group_key[16]);
mnet_err_t mnet_group_leave(const uint8_t group_hash[16]);
mnet_err_t mnet_group_members(const uint8_t group_hash[16], uint8_t out[][32], uint8_t *count);
bool mnet_group_is_member(const uint8_t node_id[32], const uint8_t group_hash[16]);

mnet_err_t mnet_publish(const char *key, const void *value, size_t len);
mnet_err_t mnet_update(const char *key, const void *value, size_t len);
mnet_err_t mnet_request(const uint8_t node_id[32], const char *key,
                        void (*cb)(mnet_err_t, const void *, size_t));
mnet_err_t mnet_subscribe(const uint8_t node_id[32], const char *key,
                          void (*cb)(const char *, const void *, size_t));
mnet_err_t mnet_unsubscribe(const uint8_t node_id[32], const char *key);
mnet_err_t mnet_list_vars(const uint8_t node_id[32],
                          void (*cb)(mnet_err_t, const char **, uint8_t));
mnet_err_t mnet_query(const uint8_t node_id[32], const char *table, const char *filter,
                      void (*cb)(mnet_err_t, const mnet_row_t *, uint8_t));
mnet_err_t mnet_get_metrics(const uint8_t node_id[32],
                            void (*cb)(mnet_err_t, const mnet_metrics_t *));

mnet_err_t mnet_send_custom(const uint8_t node_id[32],
                            uint8_t msg_type,
                            const uint8_t *payload, size_t len);
mnet_err_t mnet_send_group_custom(const uint8_t group_hash[16],
                                  uint8_t msg_type,
                                  const uint8_t *payload,
                                  size_t len,
                                  uint8_t *out_sent_count);
/* If group_hash is NULL, the message is broadcast to all eligible peers. */
mnet_err_t mnet_broadcast_custom(const uint8_t group_hash[16],
                                 uint8_t msg_type,
                                 const uint8_t *payload, size_t len);
mnet_err_t mnet_discover_lan(void);
mnet_err_t mnet_register_handler(uint8_t msg_type,
                                 void (*handler)(const uint8_t src[32],
                                                 const uint8_t *payload,
                                                 size_t len));

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif

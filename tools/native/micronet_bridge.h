#ifndef MICRONET_BRIDGE_H
#define MICRONET_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define MNVIZ_API __declspec(dllexport)
#else
#define MNVIZ_API
#endif

#define MNVIZ_MAX_NODES 32U
#define MNVIZ_MAX_VARS 32U
#define MNVIZ_MAX_MESSAGES 64U

typedef struct {
    uint32_t uptime_s;
    uint32_t free_heap;
    uint8_t connected_nodes;
    uint8_t group_count;
    uint32_t packets_sent;
    uint32_t packets_recv;
    uint32_t errors;
    uint8_t health_score;
} mnviz_metrics_t;

typedef struct {
    uint8_t node_id[32];
    uint8_t invited_by[32];
    uint8_t external_ip[4];
    uint16_t external_port;
    uint32_t first_seen;
    uint32_t last_seen;
    uint32_t db_version;
    uint8_t group_count;
    uint32_t packets_sent;
    uint32_t packets_recv;
    uint8_t health_score;
    uint32_t free_heap;
    bool is_online;
    bool is_self;
} mnviz_node_t;

typedef struct {
    char key[32];
    uint8_t type;
    uint8_t access;
    uint8_t data[256];
    size_t data_len;
    uint32_t updated_at;
    bool is_public;
} mnviz_var_t;

typedef struct {
    uint32_t timestamp;
    uint8_t direction;
    uint8_t message_type;
    uint8_t src[32];
    uint8_t dst[32];
    uint8_t payload[128];
    uint32_t payload_len;
} mnviz_message_t;

typedef struct {
    char node_name[64];
    char stun_host[128];
    uint16_t stun_port;
    uint16_t local_port;
    uint32_t heartbeat_ms;
    uint32_t offline_timeout_ms;
    uint32_t retry_interval_ms;
    uint8_t retry_count;
    uint8_t max_nodes;
    uint8_t max_vars;
    uint8_t max_pending;
    uint8_t log_level;
    uint8_t node_privkey[32];
} mnviz_init_config_t;

typedef struct {
    uint8_t node_count;
    uint8_t online_count;
    uint8_t group_count;
    uint8_t var_count;
    uint8_t message_count;
    mnviz_metrics_t local_metrics;
} mnviz_snapshot_t;

MNVIZ_API int mnviz_init(const mnviz_init_config_t *cfg);
MNVIZ_API void mnviz_shutdown(void);
MNVIZ_API int mnviz_tick(void);
MNVIZ_API bool mnviz_is_initialized(void);
MNVIZ_API int mnviz_publish_text(const char *key, const char *value);
MNVIZ_API int mnviz_update_text(const char *key, const char *value);
MNVIZ_API int mnviz_send_custom(const uint8_t node_id[32], uint8_t msg_type, const uint8_t *payload, uint32_t payload_len);
MNVIZ_API int mnviz_broadcast_custom(const uint8_t group_hash[16], uint8_t msg_type, const uint8_t *payload, uint32_t payload_len);
MNVIZ_API int mnviz_group_create(uint8_t out_group_hash[16], uint8_t out_group_key[16]);
MNVIZ_API int mnviz_group_join(const uint8_t group_hash[16], const uint8_t group_key[16]);
MNVIZ_API int mnviz_group_leave(const uint8_t group_hash[16]);
MNVIZ_API int mnviz_snapshot(mnviz_snapshot_t *out_snapshot);
MNVIZ_API int mnviz_copy_nodes(mnviz_node_t *out_nodes, uint8_t capacity, uint8_t *out_count);
MNVIZ_API int mnviz_copy_vars(mnviz_var_t *out_vars, uint8_t capacity, uint8_t *out_count);
MNVIZ_API int mnviz_copy_messages(mnviz_message_t *out_messages, uint8_t capacity, uint8_t *out_count);

#endif

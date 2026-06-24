#ifndef MICRONET_DEBUG_H
#define MICRONET_DEBUG_H

#include "micronet.h"

typedef struct {
    uint8_t node_id[32];
    uint8_t invited_by[32];
    uint8_t external_ip[4];
    uint16_t external_port;
    uint32_t first_seen;
    uint32_t last_seen;
    uint32_t db_version;
    uint8_t group_count;
    uint8_t group_hashes[MNET_MAX_GROUPS][16];
    bool is_online;
    bool is_authorized;
    bool is_self;
} mnet_debug_node_t;

typedef struct {
    char key[32];
    uint8_t type;
    uint8_t access;
    uint8_t data[256];
    size_t data_len;
    uint32_t updated_at;
    bool is_public;
} mnet_debug_var_t;

typedef struct {
    uint8_t node_count;
    uint8_t online_count;
    uint8_t group_count;
    mnet_metrics_t local_metrics;
} mnet_debug_stats_t;

mnet_err_t mnet_debug_get_stats(mnet_debug_stats_t *out_stats);
mnet_err_t mnet_debug_copy_nodes(mnet_debug_node_t *out_nodes, uint8_t capacity, uint8_t *out_count);
mnet_err_t mnet_debug_copy_vars(mnet_debug_var_t *out_vars, uint8_t capacity, uint8_t *out_count);

#endif

#ifndef P2P_DATA_H
#define P2P_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../include/micronet_limits.h"

#ifndef P2P_MAX_KEY_LEN
#define P2P_MAX_KEY_LEN 32U
#endif

#ifndef P2P_MAX_VAL_LEN
#define P2P_MAX_VAL_LEN 256U
#endif

#ifndef P2P_MAX_VARS
#define P2P_MAX_VARS 32U
#endif

#ifndef P2P_MAX_SUBS
#define P2P_MAX_SUBS 16U
#endif

typedef enum {
    P2P_DATA_KV = 0,
    P2P_DATA_TABLE = 1,
    P2P_DATA_TIMESERIES = 2,
    P2P_DATA_VAR = 3,
    P2P_DATA_METRIC = 4,
} p2p_data_type_t;

typedef enum {
    P2P_ACCESS_PUBLIC = 0,
    P2P_ACCESS_GROUP = 1,
    P2P_ACCESS_PRIVATE = 2,
} p2p_access_t;

typedef enum {
    P2P_DATA_OK = 0,
    P2P_DATA_ERR_NOT_FOUND = -1,
    P2P_DATA_ERR_ACCESS = -2,
    P2P_DATA_ERR_TYPE = -3,
    P2P_DATA_ERR_FULL = -4,
    P2P_DATA_ERR_TIMEOUT = -5,
    P2P_DATA_ERR_OFFLINE = -6,
} p2p_data_err_t;

typedef struct {
    uint8_t bytes[P2P_MAX_VAL_LEN];
    size_t len;
} p2p_row_t;

typedef struct {
    uint32_t uptime_s;
    uint32_t free_heap;
    uint8_t connected_nodes;
    uint8_t group_count;
    uint32_t packets_sent;
    uint32_t packets_recv;
    uint32_t errors;
    uint8_t health_score;
} p2p_metrics_t;

typedef struct {
    char key[P2P_MAX_KEY_LEN];
    uint8_t type;
    uint8_t data[P2P_MAX_VAL_LEN];
    size_t data_len;
    uint16_t raw_len;
    uint8_t encoding;
    uint32_t updated_at;
    bool is_public;
    uint8_t group_hash[16];
    uint8_t access;
} p2p_var_t;

typedef struct {
    uint8_t subscriber[32];
    char key[P2P_MAX_KEY_LEN];
    uint32_t last_notified;
    void (*cb)(const char *, const void *, size_t);
} p2p_subscription_t;

typedef struct {
    uint8_t health_score;
    uint32_t sample_count;
} microhealth_t;

typedef struct {
    uint8_t payload[P2P_MAX_VAL_LEN];
    size_t payload_len;
    char key[P2P_MAX_KEY_LEN];
    bool in_use;
} iotspool_t;

typedef struct {
    uint8_t max_vars;
    uint8_t max_subs;
    uint32_t notify_min_interval_ms;
    bool compress_data;
    uint32_t spool_size;
    uint32_t (*now_ms)(void);
} p2p_data_config_t;

typedef struct {
    p2p_var_t vars[P2P_MAX_VARS];
    uint8_t var_count;
    p2p_subscription_t subs[P2P_MAX_SUBS];
    uint8_t sub_count;
    p2p_metrics_t metrics;
    microhealth_t health;
    iotspool_t spool;

    p2p_data_config_t config;
    uint32_t (*now_ms)(void);
    uint32_t started_ms;
} p2p_data_t;

p2p_data_err_t p2p_data_init(p2p_data_t *ctx, const p2p_data_config_t *cfg);
p2p_data_err_t p2p_data_publish(p2p_data_t *ctx, const char *key,
                                p2p_data_type_t type,
                                const void *value, size_t len);
p2p_data_err_t p2p_data_update(p2p_data_t *ctx, const char *key,
                               const void *value, size_t len);
p2p_data_err_t p2p_data_request(p2p_data_t *ctx, const uint8_t node_id[32],
                                const char *key,
                                void (*cb)(int, const void *, size_t));
p2p_data_err_t p2p_data_subscribe(p2p_data_t *ctx, const uint8_t node_id[32],
                                  const char *key,
                                  void (*cb)(const char *, const void *, size_t));
p2p_data_err_t p2p_data_unsubscribe(p2p_data_t *ctx, const uint8_t node_id[32],
                                    const char *key);
p2p_data_err_t p2p_data_query(p2p_data_t *ctx, const uint8_t node_id[32],
                              const char *table, const char *filter,
                              void (*cb)(int, const p2p_row_t *, uint8_t));
p2p_data_err_t p2p_data_get_metrics(p2p_data_t *ctx, const uint8_t node_id[32],
                                    void (*cb)(int, const p2p_metrics_t *));
p2p_data_err_t p2p_data_list_vars(p2p_data_t *ctx, const uint8_t node_id[32],
                                  void (*cb)(int, const char **, uint8_t));
p2p_data_err_t p2p_data_tick(p2p_data_t *ctx);
void p2p_data_deinit(p2p_data_t *ctx);

int p2p_data_find_var_index(const p2p_data_t *ctx, const char *key);
p2p_data_err_t p2p_data_copy_value(p2p_var_t *var, const void *value, size_t len, bool compress);
void p2p_data_refresh_metrics(p2p_data_t *ctx);
int p2p_data_decode_value(const p2p_var_t *var, uint8_t out[P2P_MAX_VAL_LEN], size_t *out_len);

#endif

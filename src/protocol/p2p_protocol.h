#ifndef P2P_PROTOCOL_H
#define P2P_PROTOCOL_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "../transport/p2p_transport.h"
#include "../security/p2p_security.h"
#include "../network/p2p_network.h"
#include "../data/p2p_data.h"
#include "../../include/micronet_limits.h"

#ifndef P2P_MAX_PAYLOAD
#define P2P_MAX_PAYLOAD MNET_MAX_PUBLIC_PAYLOAD
#endif

#ifndef P2P_MAX_PENDING
#define P2P_MAX_PENDING 16U
#endif

typedef enum {
    P2P_MSG_HELLO = 0x01,
    P2P_MSG_HELLO_ACK = 0x02,
    P2P_MSG_HEARTBEAT = 0x03,
    P2P_MSG_DISCONNECT = 0x04,
    P2P_MSG_GOSSIP = 0x10,
    P2P_MSG_SYNC_REQ = 0x11,
    P2P_MSG_SYNC_DATA = 0x12,
    P2P_MSG_GROUP_INVITE = 0x13,
    P2P_MSG_GROUP_JOIN = 0x14,
    P2P_MSG_GROUP_LEAVE = 0x15,
    P2P_MSG_DATA_REQUEST = 0x20,
    P2P_MSG_DATA_RESPONSE = 0x21,
    P2P_MSG_DATA_NOTIFY = 0x22,
    P2P_MSG_DATA_SUBSCRIBE = 0x23,
    P2P_MSG_DATA_UNSUBSCRIBE = 0x24,
    P2P_MSG_QUERY_REQ = 0x25,
    P2P_MSG_QUERY_RESP = 0x26,
    P2P_MSG_METRICS_REQ = 0x27,
    P2P_MSG_METRICS_RESP = 0x28,
    P2P_MSG_LIST_VARS_REQ = 0x29,
    P2P_MSG_LIST_VARS_RESP = 0x2A,
    P2P_MSG_CUSTOM = 0x80,
} p2p_msg_type_t;

typedef enum {
    P2P_PROTO_OK = 0,
    P2P_PROTO_ERR_SERIALIZE = -1,
    P2P_PROTO_ERR_PARSE = -2,
    P2P_PROTO_ERR_UNKNOWN = -3,
    P2P_PROTO_ERR_RETRY = -4,
    P2P_PROTO_ERR_PENDING = -5,
    P2P_PROTO_ERR_NO_HANDLER = -6,
    P2P_PROTO_ERR_NO_ROUTE = -7,
} p2p_proto_err_t;

typedef struct {
    uint8_t type;
    uint16_t msg_id;
    uint32_t timestamp;
    uint8_t src[32];
    uint8_t dst[32];
    uint8_t group_hash[16];
    uint8_t payload[P2P_MAX_PAYLOAD];
    size_t payload_len;
} p2p_message_t;

typedef void (*p2p_protocol_completion_cb_t)(p2p_proto_err_t status,
                                             const p2p_message_t *msg,
                                             void *user);

typedef struct {
    bool in_use;
    uint8_t request_type;
    uint8_t expected_response_type;
    uint16_t msg_id;
    uint8_t peer_id[32];
    uint32_t sent_at;
    uint8_t retry_count;
    p2p_protocol_completion_cb_t completion;
    void *completion_user;
    p2p_message_t msg;
} p2p_pending_t;

typedef enum {
    P2P_ENDPOINT_PENDING = 0,
    P2P_ENDPOINT_AUTHENTICATED = 1,
} p2p_endpoint_state_t;

typedef struct {
    uint8_t node_id[32];
    uint8_t local_ip[4];
    uint16_t local_port;
    uint8_t public_ip[4];
    uint16_t public_port;
    uint32_t last_seen_ms;
    p2p_endpoint_state_t state;
    bool valid;
} p2p_endpoint_t;


typedef struct {
    uint16_t next_msg_id;
    p2p_pending_t pending[P2P_MAX_PENDING];
    uint8_t pending_count;
    uint8_t max_pending;
    microfsm_t fsm;
    struct {
        uint8_t level;
    } log;
    struct {
        uint32_t retry_interval_ms;
        uint8_t retry_count;
    } retry;
    uint32_t started_ms;
    uint32_t protocol_timeouts;
    uint32_t data_request_success;
    uint32_t data_request_error;
    p2p_transport_t *transport;
    p2p_security_t *security;
    p2p_network_t *network;
    p2p_data_t *data;
    uint32_t (*now_ms)(void);
    void (*custom_handler)(const p2p_message_t *msg);
    void (*data_response_handler)(const p2p_message_t *msg);
    void (*custom_handlers[128])(const p2p_message_t *msg);
    uint8_t self_node_id[32];
    p2p_endpoint_t endpoints[32];
    uint8_t endpoint_count;
} p2p_protocol_t;

typedef struct {
    uint8_t max_pending;
    uint32_t retry_interval_ms;
    uint8_t retry_count;
    uint8_t log_level;
    void (*custom_handler)(const p2p_message_t *msg);
    void (*data_response_handler)(const p2p_message_t *msg);
    uint32_t (*now_ms)(void);
} p2p_protocol_config_t;

p2p_proto_err_t p2p_protocol_init(p2p_protocol_t *ctx,
                                  const p2p_protocol_config_t *cfg,
                                  p2p_transport_t *transport,
                                  p2p_security_t *security,
                                  p2p_network_t *network,
                                  p2p_data_t *data);
p2p_proto_err_t p2p_protocol_send(p2p_protocol_t *ctx, const p2p_message_t *msg);
p2p_proto_err_t p2p_protocol_send_transaction(p2p_protocol_t *ctx,
                                              const p2p_message_t *msg,
                                              p2p_protocol_completion_cb_t completion,
                                              void *user);
p2p_proto_err_t p2p_protocol_broadcast(p2p_protocol_t *ctx,
                                       const uint8_t group_hash[16],
                                       const p2p_message_t *msg);
p2p_proto_err_t p2p_protocol_on_packet(p2p_protocol_t *ctx,
                                       const uint8_t *data, size_t len,
                                       const uint8_t src_ip[4], uint16_t src_port,
                                       uint8_t transport_flags);
p2p_proto_err_t p2p_protocol_register_handler(p2p_protocol_t *ctx,
                                              uint8_t msg_type,
                                              void (*handler)(const p2p_message_t *));
p2p_proto_err_t p2p_protocol_tick(p2p_protocol_t *ctx);
void p2p_protocol_deinit(p2p_protocol_t *ctx);

p2p_proto_err_t p2p_protocol_serialize(const p2p_message_t *msg, uint8_t *out, size_t *out_len);
p2p_proto_err_t p2p_protocol_parse(p2p_message_t *msg, const uint8_t *data, size_t len);
p2p_proto_err_t p2p_protocol_dispatch(p2p_protocol_t *ctx, const p2p_message_t *msg);
p2p_proto_err_t p2p_protocol_collect_metrics(p2p_protocol_t *ctx, p2p_metrics_t *out);

#endif

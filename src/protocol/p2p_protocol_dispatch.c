#include "p2p_protocol.h"

#include <string.h>

static const uint8_t p2p_proto_zero32[32] = {0};

static void p2p_protocol_dispatch_data_request(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    char key[P2P_MAX_KEY_LEN];

    if (msg->payload_len == 0U) {
        return;
    }

    memset(key, 0, sizeof(key));
    memcpy(key, msg->payload, msg->payload_len < sizeof(key) ? msg->payload_len : sizeof(key) - 1U);
    (void)p2p_data_request(ctx->data, msg->src, key, NULL);
}

static void p2p_protocol_dispatch_query(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    char table[P2P_MAX_KEY_LEN];

    if (msg->payload_len == 0U) {
        return;
    }

    memset(table, 0, sizeof(table));
    memcpy(table, msg->payload, msg->payload_len < sizeof(table) ? msg->payload_len : sizeof(table) - 1U);
    (void)p2p_data_query(ctx->data, msg->src, table, "", NULL);
}

p2p_proto_err_t p2p_protocol_dispatch(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    if (ctx == NULL || msg == NULL) {
        return P2P_PROTO_ERR_NO_HANDLER;
    }

    switch (msg->type) {
        case P2P_MSG_HELLO:
            if (p2p_security_handshake(ctx->security, msg->src) != P2P_SEC_OK) {
                return P2P_PROTO_ERR_NO_HANDLER;
            }
            ctx->fsm.state = 4U;
            return P2P_PROTO_OK;

        case P2P_MSG_HEARTBEAT:
        case P2P_MSG_DISCONNECT:
            return P2P_PROTO_OK;

        case P2P_MSG_GOSSIP:
            return p2p_network_on_gossip(ctx->network, msg->payload, msg->payload_len) == P2P_NET_OK
                       ? P2P_PROTO_OK
                       : P2P_PROTO_ERR_NO_HANDLER;

        case P2P_MSG_SYNC_REQ:
        case P2P_MSG_SYNC_DATA:
            return P2P_PROTO_OK;

        case P2P_MSG_GROUP_INVITE:
            if (msg->payload_len < 16U) {
                return P2P_PROTO_ERR_PARSE;
            }
            return p2p_network_group_join(ctx->network, msg->group_hash, msg->payload) == P2P_NET_OK
                       ? P2P_PROTO_OK
                       : P2P_PROTO_ERR_NO_HANDLER;

        case P2P_MSG_GROUP_JOIN:
        case P2P_MSG_GROUP_LEAVE:
            return P2P_PROTO_OK;

        case P2P_MSG_DATA_REQUEST:
            p2p_protocol_dispatch_data_request(ctx, msg);
            return P2P_PROTO_OK;

        case P2P_MSG_DATA_RESPONSE:
        case P2P_MSG_DATA_NOTIFY:
            return P2P_PROTO_OK;

        case P2P_MSG_DATA_SUBSCRIBE:
            return p2p_data_subscribe(ctx->data, msg->src, (const char *)msg->payload, NULL) == P2P_DATA_OK
                       ? P2P_PROTO_OK
                       : P2P_PROTO_ERR_NO_HANDLER;

        case P2P_MSG_DATA_UNSUBSCRIBE:
            return p2p_data_unsubscribe(ctx->data, msg->src, (const char *)msg->payload) == P2P_DATA_OK
                       ? P2P_PROTO_OK
                       : P2P_PROTO_ERR_NO_HANDLER;

        case P2P_MSG_QUERY_REQ:
            p2p_protocol_dispatch_query(ctx, msg);
            return P2P_PROTO_OK;

        case P2P_MSG_QUERY_RESP:
        case P2P_MSG_METRICS_REQ:
        case P2P_MSG_METRICS_RESP:
            return P2P_PROTO_OK;

        default:
            if (msg->type >= P2P_MSG_CUSTOM) {
                uint8_t idx = (uint8_t)(msg->type - P2P_MSG_CUSTOM);
                if (ctx->custom_handlers[idx] != NULL) {
                    ctx->custom_handlers[idx](msg);
                    return P2P_PROTO_OK;
                }
                if (ctx->custom_handler != NULL) {
                    ctx->custom_handler(msg);
                    return P2P_PROTO_OK;
                }
            }
            break;
    }

    (void)p2p_proto_zero32;
    return P2P_PROTO_ERR_UNKNOWN;
}

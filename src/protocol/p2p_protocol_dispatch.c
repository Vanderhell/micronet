#include "p2p_protocol.h"

#include <string.h>

static const uint8_t p2p_proto_zero32[32] = {0};

enum {
    P2P_DATA_RESP_OK = 0,
    P2P_DATA_RESP_NOT_FOUND = 1,
    P2P_DATA_RESP_ACCESS = 2,
    P2P_DATA_RESP_ERR = 3,
};

static void p2p_proto_write_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)((value >> 8) & 0xFFU);
    dst[1] = (uint8_t)(value & 0xFFU);
}

static void p2p_protocol_dispatch_data_request(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    char key[P2P_MAX_KEY_LEN];
    int idx;
    p2p_message_t resp;
    uint8_t status = P2P_DATA_RESP_ERR;
    uint8_t raw[P2P_MAX_VAL_LEN];
    size_t raw_len = 0U;
    uint16_t value_len = 0U;

    if (msg->payload_len == 0U) {
        return;
    }
    if (msg->payload_len >= sizeof(key)) {
        return;
    }

    memset(key, 0, sizeof(key));
    memcpy(key, msg->payload, msg->payload_len);

    if (ctx == NULL || ctx->data == NULL) {
        return;
    }

    idx = p2p_data_find_var_index(ctx->data, key);
    if (idx < 0) {
        status = P2P_DATA_RESP_NOT_FOUND;
    } else if (!ctx->data->vars[idx].is_public) {
        status = P2P_DATA_RESP_ACCESS;
    } else {
        if (!p2p_data_decode_value(&ctx->data->vars[idx], raw, &raw_len)) {
            status = P2P_DATA_RESP_ERR;
        } else {
            status = P2P_DATA_RESP_OK;
            if (raw_len > 0xFFFFU) {
                value_len = 0xFFFFU;
            } else {
                value_len = (uint16_t)raw_len;
            }
        }
    }

    memset(&resp, 0, sizeof(resp));
    resp.type = P2P_MSG_DATA_RESPONSE;
    resp.msg_id = msg->msg_id;
    memcpy(resp.dst, msg->src, 32U);
    resp.payload[0] = status;
    p2p_proto_write_u16(&resp.payload[1], value_len);
    resp.payload_len = 3U;
    if (status == P2P_DATA_RESP_OK && value_len > 0U) {
        if ((size_t)(resp.payload_len + value_len) <= sizeof(resp.payload)) {
            memcpy(resp.payload + resp.payload_len, raw, value_len);
            resp.payload_len = (size_t)(resp.payload_len + value_len);
        }
    }

    (void)p2p_protocol_send(ctx, &resp);
}

static void p2p_protocol_dispatch_query(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    char table[P2P_MAX_KEY_LEN];

    if (msg->payload_len == 0U) {
        return;
    }
    if (msg->payload_len >= sizeof(table)) {
        return;
    }

    memset(table, 0, sizeof(table));
    memcpy(table, msg->payload, msg->payload_len);
    (void)p2p_data_query(ctx->data, msg->src, table, "", NULL);
}

p2p_proto_err_t p2p_protocol_dispatch(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    bool authed;

    if (ctx == NULL || msg == NULL) {
        return P2P_PROTO_ERR_NO_HANDLER;
    }

    authed = p2p_security_is_authenticated(ctx->security, msg->src);
    if (!authed) {
        if (msg->type != P2P_MSG_HELLO &&
            msg->type != P2P_MSG_HELLO_ACK &&
            msg->type != P2P_MSG_HEARTBEAT &&
            msg->type != P2P_MSG_DISCONNECT &&
            msg->type != P2P_MSG_GOSSIP) {
            return P2P_PROTO_ERR_NO_HANDLER;
        }
    }

    switch (msg->type) {
        case P2P_MSG_HELLO:
        {
            uint8_t mac[P2P_HMAC_SIZE];
            p2p_message_t ack;

            if (msg->payload_len < P2P_HMAC_SIZE) {
                return P2P_PROTO_ERR_PARSE;
            }
            if (p2p_security_verify_hello_mac(ctx->security, msg->src, msg->payload) != P2P_SEC_OK) {
                return P2P_PROTO_ERR_NO_HANDLER;
            }
            if (p2p_security_build_hello_mac(ctx->security, msg->src, mac) != P2P_SEC_OK) {
                return P2P_PROTO_ERR_NO_HANDLER;
            }
            memset(&ack, 0, sizeof(ack));
            ack.type = P2P_MSG_HELLO_ACK;
            memcpy(ack.dst, msg->src, 32U);
            memcpy(ack.payload, mac, P2P_HMAC_SIZE);
            ack.payload_len = P2P_HMAC_SIZE;
            (void)p2p_protocol_send(ctx, &ack);
            ctx->fsm.state = 4U;
            return P2P_PROTO_OK;
        }

        case P2P_MSG_HELLO_ACK:
            if (msg->payload_len < P2P_HMAC_SIZE) {
                return P2P_PROTO_ERR_PARSE;
            }
            if (p2p_security_verify_hello_mac(ctx->security, msg->src, msg->payload) != P2P_SEC_OK) {
                return P2P_PROTO_ERR_NO_HANDLER;
            }
            if (p2p_security_mark_outbound_verified(ctx->security, msg->src) != P2P_SEC_OK) {
                return P2P_PROTO_ERR_NO_HANDLER;
            }
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
            if (msg->type == P2P_MSG_DATA_RESPONSE && ctx->data_response_handler != NULL) {
                ctx->data_response_handler(msg);
            }
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

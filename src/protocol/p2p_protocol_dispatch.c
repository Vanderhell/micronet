#include "p2p_protocol.h"

#include <string.h>

static const uint8_t p2p_proto_zero32[32] = {0};

typedef struct {
    uint8_t version;
    uint8_t group_hash[16];
    uint8_t group_key[16];
    uint8_t inviter[32];
} p2p_group_invite_wire_t;

typedef struct {
    uint8_t version;
    uint8_t group_hash[16];
    uint8_t peer_id[32];
} p2p_group_member_wire_t;

static int p2p_protocol_group_is_zero(const uint8_t group_hash[16])
{
    return group_hash == NULL || memcmp(group_hash, p2p_proto_zero32, 16U) == 0;
}

static int p2p_protocol_group_validate_local_member(p2p_protocol_t *ctx, const uint8_t group_hash[16])
{
    uint8_t members[P2P_MAX_MEMBERS][32];
    uint8_t count = 0U;
    uint8_t i;

    if (ctx == NULL || group_hash == NULL) {
        return 0;
    }

    if (p2p_network_group_members(ctx->network, group_hash, members, P2P_MAX_MEMBERS, &count) != P2P_NET_OK) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        if (memcmp(members[i], ctx->network->self.node_id, 32U) == 0) {
            return 1;
        }
    }
    return 0;
}

static int p2p_protocol_group_validate_sender(p2p_protocol_t *ctx, const uint8_t group_hash[16], const uint8_t src[32])
{
    uint8_t members[P2P_MAX_MEMBERS][32];
    uint8_t count = 0U;
    uint8_t i;
    p2p_node_t node;

    if (ctx == NULL || group_hash == NULL || src == NULL) {
        return 0;
    }
    if (p2p_network_find_node(ctx->network, src, &node) != P2P_NET_OK) {
        return 0;
    }
    if (!node.is_online || !node.is_authorized) {
        return 0;
    }
    if (p2p_network_group_members(ctx->network, group_hash, members, P2P_MAX_MEMBERS, &count) != P2P_NET_OK) {
        return 0;
    }

    for (i = 0U; i < count; ++i) {
        if (memcmp(members[i], src, 32U) == 0) {
            return 1;
        }
    }
    return 0;
}

static int p2p_protocol_group_wire_parse_invite(const p2p_message_t *msg, p2p_group_invite_wire_t *out)
{
    if (msg == NULL || out == NULL || msg->payload_len != (1U + 16U + 16U + 32U)) {
        return 0;
    }
    out->version = msg->payload[0];
    memcpy(out->group_hash, msg->payload + 1U, 16U);
    memcpy(out->group_key, msg->payload + 17U, 16U);
    memcpy(out->inviter, msg->payload + 33U, 32U);
    return out->version == 1U && memcmp(out->inviter, msg->src, 32U) == 0;
}

static int p2p_protocol_group_wire_parse_member(const p2p_message_t *msg, p2p_group_member_wire_t *out)
{
    if (msg == NULL || out == NULL || msg->payload_len != (1U + 16U + 32U)) {
        return 0;
    }
    out->version = msg->payload[0];
    memcpy(out->group_hash, msg->payload + 1U, 16U);
    memcpy(out->peer_id, msg->payload + 17U, 32U);
    return out->version == 1U && memcmp(out->peer_id, msg->src, 32U) == 0;
}

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

static int p2p_protocol_parse_key_payload(const p2p_message_t *msg, char key[P2P_MAX_KEY_LEN])
{
    uint8_t key_len;

    if (msg == NULL || key == NULL || msg->payload_len == 0U) {
        return 0;
    }

    if (msg->payload[0] == 1U && msg->payload_len >= 2U) {
        key_len = msg->payload[1];
        if (key_len == 0U || key_len >= P2P_MAX_KEY_LEN || msg->payload_len != (size_t)(2U + key_len)) {
            return 0;
        }
        memset(key, 0, P2P_MAX_KEY_LEN);
        memcpy(key, msg->payload + 2U, key_len);
        key[key_len] = '\0';
        return 1;
    }

    memset(key, 0, P2P_MAX_KEY_LEN);
    if (msg->payload_len >= P2P_MAX_KEY_LEN) {
        return 0;
    }
    memcpy(key, msg->payload, msg->payload_len);
    key[msg->payload_len] = '\0';
    return 1;
}

static int p2p_protocol_parse_query_payload(const p2p_message_t *msg,
                                            char table[P2P_MAX_KEY_LEN],
                                            char filter[P2P_MAX_KEY_LEN])
{
    uint8_t table_len;
    uint8_t filter_len;
    size_t needed;

    if (msg == NULL || table == NULL || filter == NULL || msg->payload_len < 3U || msg->payload[0] != 1U) {
        return 0;
    }

    table_len = msg->payload[1];
    if (table_len == 0U || table_len >= P2P_MAX_KEY_LEN) {
        return 0;
    }

    if ((size_t)(2U + table_len + 1U) > msg->payload_len) {
        return 0;
    }

    filter_len = msg->payload[2U + table_len];
    if (filter_len >= P2P_MAX_KEY_LEN) {
        return 0;
    }

    needed = (size_t)(3U + table_len + filter_len);
    if (msg->payload_len != needed) {
        return 0;
    }

    memset(table, 0, P2P_MAX_KEY_LEN);
    memcpy(table, msg->payload + 2U, table_len);
    table[table_len] = '\0';

    memset(filter, 0, P2P_MAX_KEY_LEN);
    if (filter_len > 0U) {
        memcpy(filter, msg->payload + 3U + table_len, filter_len);
        filter[filter_len] = '\0';
    }

    return 1;
}

static int p2p_protocol_parse_notify_payload(const p2p_message_t *msg,
                                             char key[P2P_MAX_KEY_LEN],
                                             const uint8_t **value,
                                             size_t *value_len)
{
    uint8_t key_len;
    uint16_t wire_value_len;

    if (msg == NULL || key == NULL || value == NULL || value_len == NULL ||
        msg->payload_len < 4U || msg->payload[0] != 1U) {
        return 0;
    }

    key_len = msg->payload[1];
    if (key_len == 0U || key_len >= P2P_MAX_KEY_LEN) {
        return 0;
    }
    if (msg->payload_len < (size_t)(4U + key_len)) {
        return 0;
    }

    wire_value_len = (uint16_t)(((uint16_t)msg->payload[2U + key_len] << 8) | msg->payload[3U + key_len]);
    if (msg->payload_len != (size_t)(4U + key_len + wire_value_len)) {
        return 0;
    }

    memset(key, 0, P2P_MAX_KEY_LEN);
    memcpy(key, msg->payload + 2U, key_len);
    key[key_len] = '\0';
    *value = wire_value_len > 0U ? (const uint8_t *)(msg->payload + 4U + key_len) : NULL;
    *value_len = wire_value_len;
    return 1;
}

static int p2p_protocol_var_access_ok(p2p_protocol_t *ctx,
                                      const p2p_message_t *msg,
                                      const p2p_var_t *var)
{
    if (ctx == NULL || msg == NULL || var == NULL) {
        return 0;
    }

    if (p2p_security_is_authenticated(ctx->security, msg->src)) {
        p2p_node_t node;
        if (p2p_network_find_node(ctx->network, msg->src, &node) != P2P_NET_OK || !node.is_authorized) {
            return 0;
        }
    } else {
        return 0;
    }

    switch (var->access) {
        case P2P_ACCESS_PUBLIC:
            return 1;
        case P2P_ACCESS_GROUP:
            return p2p_protocol_group_validate_sender(ctx, var->group_hash, msg->src);
        case P2P_ACCESS_PRIVATE:
            return memcmp(msg->src, ctx->network->self.node_id, 32U) == 0;
        default:
            return 0;
    }
}

static int p2p_protocol_var_visible_to_sender(p2p_protocol_t *ctx,
                                              const p2p_message_t *msg,
                                              const p2p_var_t *var)
{
    if (ctx == NULL || msg == NULL || var == NULL) {
        return 0;
    }

    switch (var->access) {
        case P2P_ACCESS_PUBLIC:
            return 1;
        case P2P_ACCESS_GROUP:
            return p2p_protocol_group_validate_sender(ctx, var->group_hash, msg->src);
        case P2P_ACCESS_PRIVATE:
            return memcmp(msg->src, ctx->network->self.node_id, 32U) == 0;
        default:
            return 0;
    }
}

static void p2p_protocol_data_send_response(p2p_protocol_t *ctx,
                                            const p2p_message_t *msg,
                                            uint8_t status,
                                            const uint8_t *value,
                                            size_t value_len)
{
    p2p_message_t resp;

    if (ctx == NULL || msg == NULL || value_len > P2P_MAX_PAYLOAD || value_len > 0xFFFFU) {
        return;
    }

    memset(&resp, 0, sizeof(resp));
    resp.type = P2P_MSG_DATA_RESPONSE;
    resp.msg_id = msg->msg_id;
    memcpy(resp.dst, msg->src, 32U);
    resp.payload[0] = status;
    p2p_proto_write_u16(&resp.payload[1], (uint16_t)value_len);
    resp.payload_len = 3U;
    if (value_len > 0U) {
        memcpy(resp.payload + resp.payload_len, value, value_len);
        resp.payload_len += value_len;
    }
    (void)p2p_protocol_send(ctx, &resp);
}

static void p2p_protocol_data_send_notify(p2p_protocol_t *ctx,
                                          const uint8_t dst[32],
                                          const char *key,
                                          const uint8_t *value,
                                          size_t value_len)
{
    p2p_message_t msg;
    size_t key_len;

    if (ctx == NULL || dst == NULL || key == NULL || value_len > P2P_MAX_PAYLOAD || value_len > 0xFFFFU) {
        return;
    }

    key_len = strlen(key);
    if (key_len == 0U || key_len >= P2P_MAX_KEY_LEN || (size_t)(4U + key_len + value_len) > P2P_MAX_PAYLOAD) {
        return;
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = P2P_MSG_DATA_NOTIFY;
    memcpy(msg.dst, dst, 32U);
    msg.payload[0] = 1U;
    msg.payload[1] = (uint8_t)key_len;
    memcpy(msg.payload + 2U, key, key_len);
    p2p_proto_write_u16(&msg.payload[2U + key_len], (uint16_t)value_len);
    msg.payload_len = (size_t)(4U + key_len);
    if (value_len > 0U) {
        memcpy(msg.payload + msg.payload_len, value, value_len);
        msg.payload_len += value_len;
    }
    (void)p2p_protocol_send(ctx, &msg);
}

static void p2p_protocol_send_list_vars_response(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    uint8_t payload[P2P_MAX_PAYLOAD];
    p2p_message_t resp;
    uint8_t count = 0U;
    uint8_t i;
    size_t pos = 0U;

    if (ctx == NULL || msg == NULL) {
        return;
    }

    payload[pos++] = 1U;
    payload[pos++] = 0U;
    for (i = 0U; i < ctx->data->var_count; ++i) {
        const p2p_var_t *var = &ctx->data->vars[i];
        size_t key_len;

        if (!p2p_protocol_var_visible_to_sender(ctx, msg, var)) {
            continue;
        }
        key_len = strlen(var->key);
        if (key_len == 0U || key_len >= P2P_MAX_KEY_LEN || (size_t)(pos + 1U + key_len) > sizeof(payload)) {
            continue;
        }
        payload[pos++] = (uint8_t)key_len;
        memcpy(payload + pos, var->key, key_len);
        pos += key_len;
        count++;
        if (count >= 8U) {
            break;
        }
    }
    payload[1] = count;
    memset(&resp, 0, sizeof(resp));
    resp.type = P2P_MSG_LIST_VARS_RESP;
    resp.msg_id = msg->msg_id;
    memcpy(resp.dst, msg->src, 32U);
    memcpy(resp.payload, payload, pos);
    resp.payload_len = pos;
    (void)p2p_protocol_send(ctx, &resp);
}


static void p2p_protocol_dispatch_data_request(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    char key[P2P_MAX_KEY_LEN];
    int allowed;
    int idx;
    const p2p_var_t *var;
    uint8_t raw[P2P_MAX_VAL_LEN];
    size_t raw_len = 0U;
    uint8_t status = P2P_DATA_RESP_ERR;

    if (ctx == NULL || ctx->data == NULL || msg == NULL) {
        return;
    }
    if (!p2p_protocol_parse_key_payload(msg, key)) {
        return;
    }

    idx = p2p_data_find_var_index(ctx->data, key);
    if (idx < 0) {
        status = P2P_DATA_RESP_NOT_FOUND;
    } else {
        var = &ctx->data->vars[idx];
        allowed = p2p_protocol_var_access_ok(ctx, msg, var);
        if (!allowed) {
            status = P2P_DATA_RESP_ACCESS;
        } else if (!p2p_data_decode_value(var, raw, &raw_len)) {
            status = P2P_DATA_RESP_ERR;
        } else {
            status = P2P_DATA_RESP_OK;
        }
    }

    if (status == P2P_DATA_RESP_OK) {
        p2p_protocol_data_send_response(ctx, msg, status, raw, raw_len);
    } else {
        p2p_protocol_data_send_response(ctx, msg, status, NULL, 0U);
    }
}

static void p2p_protocol_dispatch_query(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    char table[P2P_MAX_KEY_LEN];
    char filter[P2P_MAX_KEY_LEN];
    int idx;
    const p2p_var_t *var;

    if (ctx == NULL || ctx->data == NULL || msg == NULL) {
        return;
    }
    if (!p2p_protocol_parse_query_payload(msg, table, filter)) {
        return;
    }

    if (filter[0] != '\0') {
        return;
    }

    idx = p2p_data_find_var_index(ctx->data, table);
    if (idx < 0) {
        p2p_protocol_data_send_response(ctx, msg, P2P_DATA_RESP_NOT_FOUND, NULL, 0U);
        return;
    }

    var = &ctx->data->vars[idx];
    if (!p2p_protocol_var_access_ok(ctx, msg, var) || var->type != P2P_DATA_TABLE) {
        p2p_protocol_data_send_response(ctx, msg, P2P_DATA_RESP_ACCESS, NULL, 0U);
        return;
    }

    if (ctx->data->query_row_count == 0U) {
        memset(ctx->data->query_rows, 0, sizeof(ctx->data->query_rows));
        if (var->data_len > P2P_MAX_VAL_LEN) {
            p2p_protocol_data_send_response(ctx, msg, P2P_DATA_RESP_ERR, NULL, 0U);
            return;
        }
        memcpy(ctx->data->query_rows[0].bytes, var->data, var->data_len);
        ctx->data->query_rows[0].len = var->data_len;
        ctx->data->query_row_count = 1U;
    }

    {
        p2p_message_t resp;
        size_t payload_len = 0U;
        uint8_t count = ctx->data->query_row_count > 0U ? 1U : 0U;
        size_t row_len;

        memset(&resp, 0, sizeof(resp));
        resp.type = P2P_MSG_QUERY_RESP;
        resp.msg_id = msg->msg_id;
        memcpy(resp.dst, msg->src, 32U);
        resp.payload[payload_len++] = 1U;
        resp.payload[payload_len++] = count;
        if (count > 0U) {
            row_len = ctx->data->query_rows[0].len;
            if (row_len > 0xFFFFU || (size_t)(payload_len + 2U + row_len) > sizeof(resp.payload)) {
                p2p_protocol_data_send_response(ctx, msg, P2P_DATA_RESP_ERR, NULL, 0U);
                return;
            }
            p2p_proto_write_u16(&resp.payload[payload_len], (uint16_t)row_len);
            payload_len += 2U;
            memcpy(resp.payload + payload_len, ctx->data->query_rows[0].bytes, row_len);
            payload_len += row_len;
        }
        resp.payload_len = payload_len;
        (void)p2p_protocol_send(ctx, &resp);
    }
}

static void p2p_protocol_dispatch_data_subscribe(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    char key[P2P_MAX_KEY_LEN];

    if (ctx == NULL || ctx->data == NULL || msg == NULL) {
        return;
    }
    if (p2p_security_is_authenticated(ctx->security, msg->src) != true) {
        return;
    }
    if (!p2p_protocol_parse_key_payload(msg, key)) {
        return;
    }
    if (p2p_data_remote_subscribe(ctx->data, msg->src, key) != P2P_DATA_OK) {
        p2p_protocol_data_send_response(ctx, msg, P2P_DATA_RESP_ERR, NULL, 0U);
        return;
    }
    p2p_protocol_data_send_response(ctx, msg, P2P_DATA_RESP_OK, NULL, 0U);
}

static void p2p_protocol_dispatch_data_unsubscribe(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    char key[P2P_MAX_KEY_LEN];

    if (ctx == NULL || ctx->data == NULL || msg == NULL) {
        return;
    }
    if (p2p_security_is_authenticated(ctx->security, msg->src) != true) {
        return;
    }
    if (!p2p_protocol_parse_key_payload(msg, key)) {
        return;
    }
    if (p2p_data_remote_unsubscribe(ctx->data, msg->src, key) != P2P_DATA_OK) {
        p2p_protocol_data_send_response(ctx, msg, P2P_DATA_RESP_NOT_FOUND, NULL, 0U);
        return;
    }
    p2p_protocol_data_send_response(ctx, msg, P2P_DATA_RESP_OK, NULL, 0U);
}

static void p2p_protocol_dispatch_data_notify(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    char key[P2P_MAX_KEY_LEN];
    const uint8_t *value;
    size_t value_len;
    void (*cb)(const char *, const void *, size_t) = NULL;

    if (ctx == NULL || ctx->data == NULL || msg == NULL) {
        return;
    }
    if (!p2p_security_is_authenticated(ctx->security, msg->src)) {
        return;
    }
    if (!p2p_protocol_parse_notify_payload(msg, key, &value, &value_len)) {
        return;
    }
    if (p2p_data_find_subscription(ctx->data, msg->src, key, &cb) != P2P_DATA_OK || cb == NULL) {
        return;
    }
    cb(key, value, value_len);
}

static void p2p_protocol_dispatch_list_vars(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    p2p_message_t resp;
    size_t payload_len = 0U;
    uint8_t count = 0U;
    uint8_t i;

    if (ctx == NULL || ctx->data == NULL || msg == NULL) {
        return;
    }

    memset(&resp, 0, sizeof(resp));
    resp.type = P2P_MSG_LIST_VARS_RESP;
    resp.msg_id = msg->msg_id;
    memcpy(resp.dst, msg->src, 32U);
    resp.payload[payload_len++] = 1U;
    resp.payload[payload_len++] = 0U;
    for (i = 0U; i < ctx->data->var_count; ++i) {
        const p2p_var_t *var = &ctx->data->vars[i];
        size_t key_len;

        if (!p2p_protocol_var_visible_to_sender(ctx, msg, var)) {
            continue;
        }
        key_len = strlen(var->key);
        if (key_len == 0U || key_len >= P2P_MAX_KEY_LEN ||
            (size_t)(payload_len + 1U + key_len) > sizeof(resp.payload)) {
            continue;
        }
        resp.payload[payload_len++] = (uint8_t)key_len;
        memcpy(resp.payload + payload_len, var->key, key_len);
        payload_len += key_len;
        count++;
        if (count >= 8U) {
            break;
        }
    }
    resp.payload[1] = count;
    resp.payload_len = payload_len;
    (void)p2p_protocol_send(ctx, &resp);
}

static void p2p_protocol_dispatch_metrics(p2p_protocol_t *ctx, const p2p_message_t *msg)
{
    p2p_message_t resp;
    p2p_metrics_t snapshot;

    if (ctx == NULL || msg == NULL) {
        return;
    }
    if (p2p_protocol_collect_metrics(ctx, &snapshot) != P2P_PROTO_OK) {
        return;
    }
    memset(&resp, 0, sizeof(resp));
    resp.type = P2P_MSG_METRICS_RESP;
    resp.msg_id = msg->msg_id;
    memcpy(resp.dst, msg->src, 32U);
    if (sizeof(snapshot) > sizeof(resp.payload)) {
        return;
    }
    memcpy(resp.payload, &snapshot, sizeof(snapshot));
    resp.payload_len = sizeof(snapshot);
    (void)p2p_protocol_send(ctx, &resp);
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
            ack.msg_id = msg->msg_id;
            memcpy(ack.dst, msg->src, 32U);
            memcpy(ack.payload, mac, P2P_HMAC_SIZE);
            ack.payload_len = P2P_HMAC_SIZE;
            (void)p2p_protocol_send(ctx, &ack);
            ctx->fsm.state = 4U;
            return P2P_PROTO_OK;
        }

        case P2P_MSG_HELLO_ACK:
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
        {
            p2p_group_invite_wire_t invite;

            if (!p2p_protocol_group_wire_parse_invite(msg, &invite)) {
                return P2P_PROTO_ERR_PARSE;
            }
            {
                uint8_t derived[16];

                if (p2p_network_group_derive_hash(invite.group_key, derived) != P2P_NET_OK ||
                    memcmp(derived, invite.group_hash, 16U) != 0) {
                    return P2P_PROTO_ERR_PARSE;
                }
            }
            if (!p2p_protocol_group_validate_sender(ctx, invite.group_hash, msg->src)) {
                return P2P_PROTO_ERR_NO_HANDLER;
            }
            {
                p2p_group_invite_t stored;
                memset(&stored, 0, sizeof(stored));
                memcpy(stored.group_hash, invite.group_hash, 16U);
                memcpy(stored.group_key, invite.group_key, 16U);
                memcpy(stored.inviter, invite.inviter, 32U);
                memcpy(stored.invitee, msg->dst, 32U);
                stored.version = invite.version;
                stored.valid = true;
                return p2p_network_invite_store(ctx->network, &stored) == P2P_NET_OK
                           ? P2P_PROTO_OK
                           : P2P_PROTO_ERR_NO_HANDLER;
            }
        }

        case P2P_MSG_GROUP_JOIN:
        {
            p2p_group_member_wire_t member;

            if (!p2p_protocol_group_wire_parse_member(msg, &member)) {
                return P2P_PROTO_ERR_PARSE;
            }
            if (!p2p_protocol_group_validate_sender(ctx, member.group_hash, msg->src)) {
                return P2P_PROTO_ERR_NO_HANDLER;
            }
            if (!p2p_protocol_group_validate_local_member(ctx, member.group_hash)) {
                return P2P_PROTO_ERR_NO_HANDLER;
            }
            return p2p_network_peer_join_group(ctx->network, member.peer_id, member.group_hash) == P2P_NET_OK
                       ? P2P_PROTO_OK
                       : P2P_PROTO_ERR_NO_HANDLER;
        }

        case P2P_MSG_GROUP_LEAVE:
        {
            p2p_group_member_wire_t member;

            if (!p2p_protocol_group_wire_parse_member(msg, &member)) {
                return P2P_PROTO_ERR_PARSE;
            }
            if (!p2p_protocol_group_validate_sender(ctx, member.group_hash, msg->src)) {
                return P2P_PROTO_ERR_NO_HANDLER;
            }
            if (!p2p_protocol_group_validate_local_member(ctx, member.group_hash)) {
                return P2P_PROTO_ERR_NO_HANDLER;
            }
            return p2p_network_peer_leave_group(ctx->network, member.peer_id, member.group_hash) == P2P_NET_OK
                       ? P2P_PROTO_OK
                       : P2P_PROTO_ERR_NO_HANDLER;
        }

        case P2P_MSG_DATA_REQUEST:
            p2p_protocol_dispatch_data_request(ctx, msg);
            return P2P_PROTO_OK;

        case P2P_MSG_DATA_RESPONSE:
            return P2P_PROTO_OK;

        case P2P_MSG_DATA_SUBSCRIBE:
            p2p_protocol_dispatch_data_subscribe(ctx, msg);
            return P2P_PROTO_OK;

        case P2P_MSG_DATA_UNSUBSCRIBE:
            p2p_protocol_dispatch_data_unsubscribe(ctx, msg);
            return P2P_PROTO_OK;

        case P2P_MSG_DATA_NOTIFY:
            p2p_protocol_dispatch_data_notify(ctx, msg);
            return P2P_PROTO_OK;

        case P2P_MSG_LIST_VARS_REQ:
            p2p_protocol_dispatch_list_vars(ctx, msg);
            return P2P_PROTO_OK;

        case P2P_MSG_QUERY_REQ:
            p2p_protocol_dispatch_query(ctx, msg);
            return P2P_PROTO_OK;

        case P2P_MSG_QUERY_RESP:
            return P2P_PROTO_OK;

        case P2P_MSG_METRICS_REQ:
            p2p_protocol_dispatch_metrics(ctx, msg);
            return P2P_PROTO_OK;

        case P2P_MSG_METRICS_RESP:
        case P2P_MSG_LIST_VARS_RESP:
            return P2P_PROTO_OK;

        default:
            if (msg->type >= P2P_MSG_CUSTOM) {
                if (!p2p_protocol_group_is_zero(msg->group_hash)) {
                    if (!p2p_protocol_group_validate_local_member(ctx, msg->group_hash)) {
                        return P2P_PROTO_ERR_NO_HANDLER;
                    }
                    if (!p2p_protocol_group_validate_sender(ctx, msg->group_hash, msg->src)) {
                        return P2P_PROTO_ERR_NO_HANDLER;
                    }
                }
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

#include "p2p_protocol.h"

#include <string.h>

static void p2p_proto_write_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)((value >> 8) & 0xFFU);
    dst[1] = (uint8_t)(value & 0xFFU);
}

static void p2p_proto_write_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)((value >> 24) & 0xFFU);
    dst[1] = (uint8_t)((value >> 16) & 0xFFU);
    dst[2] = (uint8_t)((value >> 8) & 0xFFU);
    dst[3] = (uint8_t)(value & 0xFFU);
}

static uint16_t p2p_proto_read_u16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static uint32_t p2p_proto_read_u32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

p2p_proto_err_t p2p_protocol_serialize(const p2p_message_t *msg, uint8_t *out, size_t *out_len)
{
    size_t needed;
    size_t offset = 0U;

    if (msg == NULL || out == NULL || out_len == NULL) {
        return P2P_PROTO_ERR_SERIALIZE;
    }

    needed = MNET_PROTOCOL_SERIALIZED_HEADER_SIZE + msg->payload_len;
    if (*out_len < needed || msg->payload_len > P2P_MAX_PAYLOAD) {
        return P2P_PROTO_ERR_SERIALIZE;
    }

    out[offset++] = msg->type;
    p2p_proto_write_u16(out + offset, msg->msg_id);
    offset += 2U;
    p2p_proto_write_u32(out + offset, msg->timestamp);
    offset += 4U;
    memcpy(out + offset, msg->src, 32U);
    offset += 32U;
    memcpy(out + offset, msg->dst, 32U);
    offset += 32U;
    memcpy(out + offset, msg->group_hash, 16U);
    offset += 16U;
    p2p_proto_write_u16(out + offset, (uint16_t)msg->payload_len);
    offset += 2U;
    if (msg->payload_len > 0U) {
        memcpy(out + offset, msg->payload, msg->payload_len);
        offset += msg->payload_len;
    }

    *out_len = offset;
    return P2P_PROTO_OK;
}

p2p_proto_err_t p2p_protocol_parse(p2p_message_t *msg, const uint8_t *data, size_t len)
{
    size_t offset = 0U;
    uint16_t payload_len;

    if (msg == NULL || data == NULL || len < MNET_PROTOCOL_SERIALIZED_HEADER_SIZE) {
        return P2P_PROTO_ERR_PARSE;
    }

    memset(msg, 0, sizeof(*msg));
    msg->type = data[offset++];
    msg->msg_id = p2p_proto_read_u16(data + offset);
    offset += 2U;
    msg->timestamp = p2p_proto_read_u32(data + offset);
    offset += 4U;
    memcpy(msg->src, data + offset, 32U);
    offset += 32U;
    memcpy(msg->dst, data + offset, 32U);
    offset += 32U;
    memcpy(msg->group_hash, data + offset, 16U);
    offset += 16U;
    payload_len = p2p_proto_read_u16(data + offset);
    offset += 2U;

    if ((size_t)payload_len > P2P_MAX_PAYLOAD || (offset + payload_len) > len) {
        return P2P_PROTO_ERR_PARSE;
    }

    msg->payload_len = payload_len;
    if (payload_len > 0U) {
        memcpy(msg->payload, data + offset, payload_len);
    }

    return P2P_PROTO_OK;
}

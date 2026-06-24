#include "p2p_network.h"

#include <string.h>

static size_t p2p_network_node_wire_size(void)
{
    return 32U + 4U + 2U + 32U + 4U + 4U + 1U + (P2P_MAX_GROUPS * 16U) + 1U + 1U + 1U + 4U;
}

static void p2p_network_write_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)((value >> 8) & 0xFFU);
    dst[1] = (uint8_t)(value & 0xFFU);
}

static void p2p_network_write_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)((value >> 24) & 0xFFU);
    dst[1] = (uint8_t)((value >> 16) & 0xFFU);
    dst[2] = (uint8_t)((value >> 8) & 0xFFU);
    dst[3] = (uint8_t)(value & 0xFFU);
}

static uint16_t p2p_network_read_u16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static uint32_t p2p_network_read_u32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static void p2p_network_encode_node(uint8_t *dst, const p2p_node_t *node)
{
    size_t offset = 0U;

    memcpy(dst + offset, node->node_id, 32U);
    offset += 32U;
    memcpy(dst + offset, node->external_ip, 4U);
    offset += 4U;
    p2p_network_write_u16(dst + offset, node->external_port);
    offset += 2U;
    memcpy(dst + offset, node->invited_by, 32U);
    offset += 32U;
    p2p_network_write_u32(dst + offset, node->first_seen);
    offset += 4U;
    p2p_network_write_u32(dst + offset, node->last_seen);
    offset += 4U;
    dst[offset++] = node->group_count;
    memcpy(dst + offset, node->group_hashes, P2P_MAX_GROUPS * 16U);
    offset += P2P_MAX_GROUPS * 16U;
    dst[offset++] = node->is_online ? 1U : 0U;
    dst[offset++] = 0U;
    dst[offset++] = node->is_authorized ? 1U : 0U;
    p2p_network_write_u32(dst + offset, node->db_version);
}

static void p2p_network_decode_node(p2p_node_t *node, const uint8_t *src)
{
    size_t offset = 0U;

    memset(node, 0, sizeof(*node));
    memcpy(node->node_id, src + offset, 32U);
    offset += 32U;
    memcpy(node->external_ip, src + offset, 4U);
    offset += 4U;
    node->external_port = p2p_network_read_u16(src + offset);
    offset += 2U;
    memcpy(node->invited_by, src + offset, 32U);
    offset += 32U;
    node->first_seen = p2p_network_read_u32(src + offset);
    offset += 4U;
    node->last_seen = p2p_network_read_u32(src + offset);
    offset += 4U;
    node->group_count = src[offset++];
    memcpy(node->group_hashes, src + offset, P2P_MAX_GROUPS * 16U);
    offset += P2P_MAX_GROUPS * 16U;
    node->is_online = src[offset++] != 0U;
    offset++;
    node->is_authorized = src[offset++] != 0U;
    node->db_version = p2p_network_read_u32(src + offset);
}

p2p_net_err_t p2p_network_gossip_build_delta(p2p_network_t *ctx, uint8_t *out, size_t *out_len)
{
    uint8_t count = 0U;
    uint8_t i;
    size_t node_size = p2p_network_node_wire_size();
    size_t needed = 2U;

    if (ctx == NULL || out == NULL || out_len == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    for (i = 0U; i < ctx->node_count; ++i) {
        if (ctx->nodes[i].db_version > ctx->last_sync_version) {
            needed += node_size;
            count++;
        }
    }

    if (*out_len < needed) {
        return P2P_NET_ERR_SYNC;
    }

    out[0] = 1U;
    out[1] = count;
    needed = 2U;
    for (i = 0U; i < ctx->node_count; ++i) {
        if (ctx->nodes[i].db_version > ctx->last_sync_version) {
            p2p_network_encode_node(out + needed, &ctx->nodes[i]);
            needed += node_size;
        }
    }

    *out_len = needed;
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_on_gossip(p2p_network_t *ctx, const uint8_t *msg, size_t len)
{
    uint8_t count;
    uint8_t i;
    size_t offset = 2U;
    size_t node_size = p2p_network_node_wire_size();

    if (ctx == NULL || msg == NULL || len < 2U || msg[0] != 1U) {
        return P2P_NET_ERR_SYNC;
    }

    count = msg[1];
    if (len < (2U + ((size_t)count * node_size))) {
        return P2P_NET_ERR_SYNC;
    }

    for (i = 0U; i < count; ++i) {
        p2p_node_t node;
        p2p_network_decode_node(&node, msg + offset);
        offset += node_size;
        if (p2p_network_sync_apply(ctx, &node) != P2P_NET_OK) {
            return P2P_NET_ERR_SYNC;
        }
    }

    return P2P_NET_OK;
}

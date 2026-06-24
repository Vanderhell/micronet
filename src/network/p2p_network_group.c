#include "p2p_network.h"

#include "../security/p2p_security.h"
#include "mcrypt.h"

#include <string.h>

static const uint8_t p2p_network_zero32[32] = {0};

static void p2p_network_group_publish(p2p_network_t *ctx,
                                      p2p_network_event_id_t event_id,
                                      const void *payload,
                                      size_t payload_len)
{
    microbus_event_t event;

    if (ctx == NULL || ctx->event_publish == NULL) {
        return;
    }

    event.event_id = (uint8_t)event_id;
    event.payload = payload;
    event.payload_len = payload_len;
    ctx->event_publish(&event, ctx->event_user);
}

static p2p_group_t *p2p_network_group_find(p2p_network_t *ctx, const uint8_t group_hash[16])
{
    uint8_t i;

    if (ctx == NULL || group_hash == NULL) {
        return NULL;
    }

    for (i = 0U; i < ctx->group_count; ++i) {
        if (memcmp(ctx->groups[i].group_hash, group_hash, 16U) == 0) {
            return &ctx->groups[i];
        }
    }

    return NULL;
}

static int p2p_network_group_has_member(const p2p_group_t *group, const uint8_t node_id[32])
{
    uint8_t i;

    for (i = 0U; i < group->member_count; ++i) {
        if (memcmp(group->members[i], node_id, 32U) == 0) {
            return 1;
        }
    }

    return 0;
}

static void p2p_network_next_group_hash(p2p_network_t *ctx,
                                        const uint8_t group_key[16],
                                        uint32_t created_at,
                                        uint8_t out_group_hash[16])
{
    uint8_t material[20];
    uint8_t digest[MCRYPT_HMAC_SHA256_SIZE];

    memcpy(material, group_key, 16U);
    material[16] = (uint8_t)((created_at >> 24) & 0xFFU);
    material[17] = (uint8_t)((created_at >> 16) & 0xFFU);
    material[18] = (uint8_t)((created_at >> 8) & 0xFFU);
    material[19] = (uint8_t)(created_at & 0xFFU);
    mcrypt_hmac_sha256(group_key, 16U, material, sizeof(material), digest);
    memcpy(out_group_hash, digest, 16U);
    (void)ctx;
}

p2p_net_err_t p2p_network_group_create(p2p_network_t *ctx, uint8_t out_group_hash[16])
{
    p2p_group_t *group;
    p2p_sec_err_t sec_err;

    if (ctx == NULL || out_group_hash == NULL) {
        return P2P_NET_ERR_GROUP_FULL;
    }

    if (ctx->group_count >= ctx->config.max_groups || ctx->group_count >= P2P_MAX_GROUPS) {
        return P2P_NET_ERR_GROUP_FULL;
    }

    group = &ctx->groups[ctx->group_count];
    memset(group, 0, sizeof(*group));
    sec_err = p2p_security_random_fill(group->group_key, sizeof(group->group_key));
    if (sec_err != P2P_SEC_OK) {
        return P2P_NET_ERR_SYNC;
    }

    group->created_at = ctx->now_ms();
    memcpy(group->created_by, ctx->self.node_id, 32U);
    p2p_network_next_group_hash(ctx, group->group_key, group->created_at, group->group_hash);
    memcpy(group->members[group->member_count++], ctx->self.node_id, 32U);
    ctx->self.is_authorized = true;
    group->db_version = ++ctx->last_db_version;
    if (ctx->self.group_count < P2P_MAX_GROUPS) {
        memcpy(ctx->self.group_hashes[ctx->self.group_count++], group->group_hash, 16U);
    }
    memcpy(out_group_hash, group->group_hash, 16U);
    ctx->group_count++;
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_group_invite(p2p_network_t *ctx,
                                       const uint8_t node_id[32],
                                       const uint8_t group_hash[16])
{
    p2p_group_t *group;
    p2p_node_t node;

    if (ctx == NULL || node_id == NULL || group_hash == NULL) {
        return P2P_NET_ERR_NO_INVITE;
    }

    group = p2p_network_group_find(ctx, group_hash);
    if (group == NULL) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    if (!p2p_network_group_has_member(group, ctx->self.node_id)) {
        return P2P_NET_ERR_NO_INVITE;
    }

    if (p2p_network_find_node(ctx, node_id, &node) != P2P_NET_OK) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    if (p2p_network_group_has_member(group, node_id)) {
        return P2P_NET_OK;
    }

    if (group->member_count >= P2P_MAX_MEMBERS) {
        return P2P_NET_ERR_GROUP_FULL;
    }

    memcpy(group->members[group->member_count++], node_id, 32U);
    group->db_version = ++ctx->last_db_version;
    p2p_network_group_publish(ctx, P2P_EVENT_GROUP_INVITE, group, sizeof(*group));
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_group_join(p2p_network_t *ctx,
                                     const uint8_t group_hash[16],
                                     const uint8_t group_key[16])
{
    p2p_group_t *group;

    if (ctx == NULL || group_hash == NULL || group_key == NULL) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    group = p2p_network_group_find(ctx, group_hash);
    if (group == NULL) {
        if (ctx->group_count >= ctx->config.max_groups || ctx->group_count >= P2P_MAX_GROUPS) {
            return P2P_NET_ERR_GROUP_FULL;
        }
        group = &ctx->groups[ctx->group_count++];
        memset(group, 0, sizeof(*group));
        memcpy(group->group_hash, group_hash, 16U);
        memcpy(group->created_by, p2p_network_zero32, 32U);
        group->created_at = ctx->now_ms();
    }

    memcpy(group->group_key, group_key, 16U);
    if (!p2p_network_group_has_member(group, ctx->self.node_id)) {
        if (group->member_count >= P2P_MAX_MEMBERS) {
            return P2P_NET_ERR_GROUP_FULL;
        }
        memcpy(group->members[group->member_count++], ctx->self.node_id, 32U);
    }

    if (ctx->self.group_count < P2P_MAX_GROUPS) {
        uint8_t i;
        for (i = 0U; i < ctx->self.group_count; ++i) {
            if (memcmp(ctx->self.group_hashes[i], group_hash, 16U) == 0) {
                break;
            }
        }
        if (i == ctx->self.group_count) {
            memcpy(ctx->self.group_hashes[ctx->self.group_count++], group_hash, 16U);
        }
    }
    ctx->self.is_authorized = true;

    group->db_version = ++ctx->last_db_version;
    p2p_network_group_publish(ctx, P2P_EVENT_GROUP_JOINED, group, sizeof(*group));
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_group_leave(p2p_network_t *ctx, const uint8_t group_hash[16])
{
    p2p_group_t *group;
    uint8_t i;

    if (ctx == NULL || group_hash == NULL) {
        return P2P_NET_ERR_NOT_MEMBER;
    }

    group = p2p_network_group_find(ctx, group_hash);
    if (group == NULL || !p2p_network_group_has_member(group, ctx->self.node_id)) {
        return P2P_NET_ERR_NOT_MEMBER;
    }

    for (i = 0U; i < group->member_count; ++i) {
        if (memcmp(group->members[i], ctx->self.node_id, 32U) == 0) {
            if ((i + 1U) < group->member_count) {
                memmove(group->members[i],
                        group->members[i + 1U],
                        (size_t)(group->member_count - i - 1U) * 32U);
            }
            memset(group->members[group->member_count - 1U], 0, 32U);
            group->member_count--;
            break;
        }
    }

    for (i = 0U; i < ctx->self.group_count; ++i) {
        if (memcmp(ctx->self.group_hashes[i], group_hash, 16U) == 0) {
            if ((i + 1U) < ctx->self.group_count) {
                memmove(ctx->self.group_hashes[i],
                        ctx->self.group_hashes[i + 1U],
                        (size_t)(ctx->self.group_count - i - 1U) * 16U);
            }
            memset(ctx->self.group_hashes[ctx->self.group_count - 1U], 0, 16U);
            ctx->self.group_count--;
            break;
        }
    }

    ctx->self.is_authorized = ctx->self.group_count > 0U;

    group->db_version = ++ctx->last_db_version;
    p2p_network_group_publish(ctx, P2P_EVENT_GROUP_LEFT, group, sizeof(*group));
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_group_members(p2p_network_t *ctx,
                                        const uint8_t group_hash[16],
                                        uint8_t out_members[][32],
                                        uint8_t *count)
{
    p2p_group_t *group;
    uint8_t i;

    if (ctx == NULL || group_hash == NULL || out_members == NULL || count == NULL) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    group = p2p_network_group_find(ctx, group_hash);
    if (group == NULL) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    for (i = 0U; i < group->member_count; ++i) {
        memcpy(out_members[i], group->members[i], 32U);
    }
    *count = group->member_count;
    return P2P_NET_OK;
}

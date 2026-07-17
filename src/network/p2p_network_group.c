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

static p2p_group_t *p2p_network_group_find_mut(p2p_network_t *ctx, const uint8_t group_hash[16])
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

static int p2p_network_self_group_has(const p2p_network_t *ctx, const uint8_t group_hash[16])
{
    uint8_t i;

    if (ctx == NULL || group_hash == NULL) {
        return 0;
    }

    for (i = 0U; i < ctx->self.group_count; ++i) {
        if (memcmp(ctx->self.group_hashes[i], group_hash, 16U) == 0) {
            return 1;
        }
    }

    return 0;
}

static p2p_net_err_t p2p_network_self_group_add(p2p_network_t *ctx, const uint8_t group_hash[16])
{
    if (ctx == NULL || group_hash == NULL) {
        return P2P_NET_ERR_SYNC;
    }
    if (p2p_network_self_group_has(ctx, group_hash)) {
        return P2P_NET_OK;
    }
    if (ctx->self.group_count >= P2P_MAX_GROUPS) {
        return P2P_NET_ERR_GROUP_FULL;
    }

    memcpy(ctx->self.group_hashes[ctx->self.group_count++], group_hash, 16U);
    return P2P_NET_OK;
}

static void p2p_network_self_group_remove(p2p_network_t *ctx, const uint8_t group_hash[16])
{
    uint8_t i;

    if (ctx == NULL || group_hash == NULL) {
        return;
    }

    for (i = 0U; i < ctx->self.group_count; ++i) {
        if (memcmp(ctx->self.group_hashes[i], group_hash, 16U) == 0) {
            if ((i + 1U) < ctx->self.group_count) {
                memmove(ctx->self.group_hashes[i],
                        ctx->self.group_hashes[i + 1U],
                        (size_t)(ctx->self.group_count - i - 1U) * sizeof(ctx->self.group_hashes[0]));
            }
            memset(ctx->self.group_hashes[ctx->self.group_count - 1U], 0, 16U);
            ctx->self.group_count--;
            break;
        }
    }
}

static void p2p_network_next_group_hash(const uint8_t group_key[16],
                                        uint8_t out_group_hash[16])
{
    uint8_t material[16];
    uint8_t digest[MCRYPT_HMAC_SHA256_SIZE];

    memcpy(material, group_key, 16U);
    mcrypt_hmac_sha256(group_key, 16U, material, sizeof(material), digest);
    memcpy(out_group_hash, digest, 16U);
}

p2p_net_err_t p2p_network_group_derive_hash(const uint8_t group_key[16], uint8_t out_group_hash[16])
{
    if (group_key == NULL || out_group_hash == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    p2p_network_next_group_hash(group_key, out_group_hash);
    return P2P_NET_OK;
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
    if (ctx->self.group_count >= P2P_MAX_GROUPS) {
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
    p2p_network_next_group_hash(group->group_key, group->group_hash);
    memcpy(group->members[group->member_count++], ctx->self.node_id, 32U);
    ctx->self.is_authorized = true;
    group->db_version = ++ctx->last_db_version;
    memcpy(ctx->self.group_hashes[ctx->self.group_count++], group->group_hash, 16U);
    memcpy(out_group_hash, group->group_hash, 16U);
    ctx->group_count++;
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_group_remove(p2p_network_t *ctx, const uint8_t group_hash[16])
{
    uint8_t i;

    if (ctx == NULL || group_hash == NULL) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    for (i = 0U; i < ctx->group_count; ++i) {
        if (memcmp(ctx->groups[i].group_hash, group_hash, 16U) == 0) {
            if (ctx->groups[i].member_count != 0U) {
                return P2P_NET_ERR_NOT_MEMBER;
            }
            p2p_network_self_group_remove(ctx, group_hash);
            if ((i + 1U) < ctx->group_count) {
                memmove(&ctx->groups[i],
                        &ctx->groups[i + 1U],
                        (size_t)(ctx->group_count - i - 1U) * sizeof(ctx->groups[0]));
            }
            memset(&ctx->groups[ctx->group_count - 1U], 0, sizeof(ctx->groups[0]));
            ctx->group_count--;
            return P2P_NET_OK;
        }
    }

    return P2P_NET_ERR_NOT_FOUND;
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

    group = p2p_network_group_find_mut(ctx, group_hash);
    if (group == NULL) {
        return P2P_NET_ERR_NOT_FOUND;
    }

    if (!p2p_network_group_has_member(group, ctx->self.node_id)) {
        return P2P_NET_ERR_NO_INVITE;
    }

    if (p2p_network_find_node(ctx, node_id, &node) != P2P_NET_OK) {
        return P2P_NET_ERR_NOT_FOUND;
    }

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

    group = p2p_network_group_find_mut(ctx, group_hash);
    if (group == NULL) {
        if (ctx->group_count >= ctx->config.max_groups || ctx->group_count >= P2P_MAX_GROUPS) {
            return P2P_NET_ERR_GROUP_FULL;
        }
        if (ctx->self.group_count >= P2P_MAX_GROUPS) {
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
        if (!p2p_network_self_group_has(ctx, group_hash) && ctx->self.group_count >= P2P_MAX_GROUPS) {
            return P2P_NET_ERR_GROUP_FULL;
        }
        memcpy(group->members[group->member_count++], ctx->self.node_id, 32U);
    }

    if (p2p_network_self_group_add(ctx, group_hash) != P2P_NET_OK) {
        if (group->member_count > 0U && memcmp(group->members[group->member_count - 1U], ctx->self.node_id, 32U) == 0) {
            memset(group->members[group->member_count - 1U], 0, 32U);
            group->member_count--;
        }
        if (group->member_count == 0U && memcmp(group->created_by, p2p_network_zero32, 32U) == 0) {
            memset(group, 0, sizeof(*group));
            if (ctx->group_count > 0U) {
                ctx->group_count--;
            }
        }
        return P2P_NET_ERR_GROUP_FULL;
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
    int local_member_found = 0;

    if (ctx == NULL || group_hash == NULL) {
        return P2P_NET_ERR_NOT_MEMBER;
    }

    group = p2p_network_group_find_mut(ctx, group_hash);
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
            local_member_found = 1;
            break;
        }
    }

    if (!local_member_found) {
        return P2P_NET_ERR_NOT_MEMBER;
    }
    p2p_network_self_group_remove(ctx, group_hash);

    ctx->self.is_authorized = ctx->self.group_count > 0U;

    group->db_version = ++ctx->last_db_version;
    p2p_network_group_publish(ctx, P2P_EVENT_GROUP_LEFT, group, sizeof(*group));

    if (group->member_count == 0U) {
        (void)p2p_network_group_remove(ctx, group_hash);
    }
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_group_members(p2p_network_t *ctx,
                                        const uint8_t group_hash[16],
                                        uint8_t out_members[][32],
                                        uint8_t capacity,
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
        if (i >= capacity) {
            return P2P_NET_ERR_GROUP_FULL;
        }
        memcpy(out_members[i], group->members[i], 32U);
    }
    *count = group->member_count;
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_invite_store(p2p_network_t *ctx, const p2p_group_invite_t *invite)
{
    uint8_t i;
    p2p_group_invite_t *slot = NULL;

    if (ctx == NULL || invite == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    for (i = 0U; i < ctx->invite_count; ++i) {
        if (ctx->invites[i].valid && memcmp(ctx->invites[i].group_hash, invite->group_hash, 16U) == 0) {
            if (memcmp(ctx->invites[i].group_key, invite->group_key, 16U) == 0 &&
                memcmp(ctx->invites[i].inviter, invite->inviter, 32U) == 0 &&
                memcmp(ctx->invites[i].invitee, invite->invitee, 32U) == 0) {
                ctx->invites[i].version = invite->version;
                return P2P_NET_OK;
            }
            return P2P_NET_ERR_GROUP_FULL;
        }
        if (slot == NULL && !ctx->invites[i].valid) {
            slot = &ctx->invites[i];
        }
    }

    if (slot == NULL) {
        if (ctx->invite_count >= P2P_MAX_GROUPS) {
            return P2P_NET_ERR_GROUP_FULL;
        }
        slot = &ctx->invites[ctx->invite_count++];
    }

    memset(slot, 0, sizeof(*slot));
    *slot = *invite;
    slot->valid = true;
    return P2P_NET_OK;
}

p2p_net_err_t p2p_network_invite_find(p2p_network_t *ctx, const uint8_t group_hash[16],
                                     p2p_group_invite_t *out_invite)
{
    uint8_t i;

    if (ctx == NULL || group_hash == NULL || out_invite == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    for (i = 0U; i < ctx->invite_count; ++i) {
        if (ctx->invites[i].valid && memcmp(ctx->invites[i].group_hash, group_hash, 16U) == 0) {
            *out_invite = ctx->invites[i];
            return P2P_NET_OK;
        }
    }

    return P2P_NET_ERR_NOT_FOUND;
}

p2p_net_err_t p2p_network_invite_remove(p2p_network_t *ctx, const uint8_t group_hash[16])
{
    uint8_t i;

    if (ctx == NULL || group_hash == NULL) {
        return P2P_NET_ERR_SYNC;
    }

    for (i = 0U; i < ctx->invite_count; ++i) {
        if (ctx->invites[i].valid && memcmp(ctx->invites[i].group_hash, group_hash, 16U) == 0) {
            memset(&ctx->invites[i], 0, sizeof(ctx->invites[i]));
            return P2P_NET_OK;
        }
    }

    return P2P_NET_ERR_NOT_FOUND;
}

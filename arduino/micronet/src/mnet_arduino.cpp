#include "mnet_arduino.h"

#include <Preferences.h>
#include <string.h>

#include "mcrypt.h"
#include "mnet_identity.h"

namespace {

constexpr const char *kGroupsNamespace = "mneta_groups";
constexpr const char *kGroupsBlobKey = "groupmap";
constexpr uint32_t kMagic = 0x4154454dUL;
constexpr uint8_t kVersion = 1U;

struct mneta_group_blob_t {
  uint32_t magic;
  uint8_t version;
  uint8_t group_count;
  uint8_t reserved[2];
  mneta_group_t groups[MNETA_MAX_GROUPS];
};

mneta_err_t mneta_require_init(mneta_t *ctx)
{
  return (ctx != nullptr && ctx->initialized) ? MNETA_OK : MNETA_ERR_NOT_INIT;
}

mneta_err_t mneta_map_sec_err(p2p_sec_err_t err)
{
  switch (err) {
    case P2P_SEC_OK:
      return MNETA_OK;
    case P2P_SEC_ERR_NO_GROUP:
      return MNETA_ERR_NOT_FOUND;
    case P2P_SEC_ERR_NO_SESSION:
      return MNETA_ERR_NOT_FOUND;
    case P2P_SEC_ERR_BUF:
      return MNETA_ERR_FULL;
    default:
      return MNETA_ERR_CRYPTO;
  }
}

bool mneta_group_blob_valid(const mneta_group_blob_t &blob)
{
  return blob.magic == kMagic && blob.version == kVersion && blob.group_count <= MNETA_MAX_GROUPS;
}

bool mneta_load_groups(mneta_t *ctx)
{
  Preferences prefs;
  mneta_group_blob_t blob;
  size_t i;

  if (ctx == nullptr || !ctx->store_group_keys) {
    return false;
  }

  if (!prefs.begin(kGroupsNamespace, true)) {
    return false;
  }

  memset(&blob, 0, sizeof(blob));
  size_t got = prefs.getBytes(kGroupsBlobKey, &blob, sizeof(blob));
  prefs.end();
  if (got != sizeof(blob) || !mneta_group_blob_valid(blob)) {
    return false;
  }

  ctx->group_count = blob.group_count;
  memcpy(ctx->groups, blob.groups, sizeof(ctx->groups));

  for (i = 0; i < ctx->group_count; ++i) {
    if (p2p_security_add_group_key(&ctx->security, ctx->groups[i].group_key) != P2P_SEC_OK) {
      return false;
    }
  }

  return true;
}

bool mneta_store_groups(const mneta_t *ctx)
{
  Preferences prefs;
  mneta_group_blob_t blob;

  if (ctx == nullptr || !ctx->store_group_keys) {
    return true;
  }

  if (!prefs.begin(kGroupsNamespace, false)) {
    return false;
  }

  memset(&blob, 0, sizeof(blob));
  blob.magic = kMagic;
  blob.version = kVersion;
  blob.group_count = ctx->group_count;
  memcpy(blob.groups, ctx->groups, sizeof(blob.groups));

  bool ok = prefs.putBytes(kGroupsBlobKey, &blob, sizeof(blob)) == sizeof(blob);
  prefs.end();
  return ok;
}

int mneta_find_group_index(mneta_t *ctx, const uint8_t group_hash[16])
{
  uint8_t i;

  if (ctx == nullptr || group_hash == nullptr) {
    return -1;
  }

  for (i = 0; i < ctx->group_count; ++i) {
    if (memcmp(ctx->groups[i].group_hash, group_hash, 16U) == 0) {
      return (int)i;
    }
  }

  return -1;
}

void mneta_compute_group_hash(const uint8_t group_key[16], uint8_t out_group_hash[16])
{
  uint8_t digest[MCRYPT_SHA256_DIGEST_SIZE];

  mcrypt_sha256(group_key, 16U, digest);
  memcpy(out_group_hash, digest, 16U);
}

}  // namespace

extern "C" mneta_err_t mneta_init(mneta_t *ctx, const mneta_config_t *cfg)
{
  p2p_security_config_t sec_cfg;
  uint8_t i;

  if (ctx == nullptr) {
    return MNETA_ERR_INVALID_ARG;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->store_group_keys = true;
  if (cfg != nullptr) {
    ctx->store_group_keys = cfg->store_group_keys;
  }

  memset(&sec_cfg, 0, sizeof(sec_cfg));
  sec_cfg.store_keys = false;

  if (cfg != nullptr) {
    memcpy(sec_cfg.node_privkey, cfg->node_privkey, sizeof(sec_cfg.node_privkey));
    memcpy(sec_cfg.node_pubkey, cfg->node_pubkey, sizeof(sec_cfg.node_pubkey));
  }

  if (p2p_security_init(&ctx->security, &sec_cfg) != P2P_SEC_OK) {
    return MNETA_ERR_CRYPTO;
  }

  if (cfg != nullptr && cfg->group_count > 0U) {
    ctx->group_count = cfg->group_count <= MNETA_MAX_GROUPS ? cfg->group_count : MNETA_MAX_GROUPS;
    for (i = 0; i < ctx->group_count; ++i) {
      ctx->groups[i] = cfg->groups[i];
      if (p2p_security_add_group_key(&ctx->security, ctx->groups[i].group_key) != P2P_SEC_OK) {
        p2p_security_deinit(&ctx->security);
        memset(ctx, 0, sizeof(*ctx));
        return MNETA_ERR_CRYPTO;
      }
    }
    if (!mneta_store_groups(ctx)) {
      p2p_security_deinit(&ctx->security);
      memset(ctx, 0, sizeof(*ctx));
      return MNETA_ERR_STORAGE;
    }
  } else if (ctx->store_group_keys) {
    if (!mneta_load_groups(ctx)) {
      ctx->group_count = 0U;
    }
  }

  ctx->initialized = true;
  return MNETA_OK;
}

extern "C" void mneta_deinit(mneta_t *ctx)
{
  if (ctx == nullptr || !ctx->initialized) {
    return;
  }

  p2p_security_deinit(&ctx->security);
  memset(ctx, 0, sizeof(*ctx));
}

extern "C" mneta_err_t mneta_get_node_id(mneta_t *ctx, uint8_t out_node_id[32])
{
  mneta_err_t state = mneta_require_init(ctx);
  if (state != MNETA_OK) {
    return state;
  }
  if (out_node_id == nullptr) {
    return MNETA_ERR_INVALID_ARG;
  }

  memcpy(out_node_id, ctx->security.node_pubkey, 32U);
  return MNETA_OK;
}

extern "C" mneta_err_t mneta_get_pubkey(mneta_t *ctx, uint8_t out_pubkey[32])
{
  mneta_err_t state = mneta_require_init(ctx);
  if (state != MNETA_OK) {
    return state;
  }
  if (out_pubkey == nullptr) {
    return MNETA_ERR_INVALID_ARG;
  }

  memcpy(out_pubkey, ctx->security.node_pubkey, 32U);
  return MNETA_OK;
}

extern "C" mneta_err_t mneta_get_privkey(mneta_t *ctx, uint8_t out_privkey[32])
{
  mneta_err_t state = mneta_require_init(ctx);
  if (state != MNETA_OK) {
    return state;
  }
  if (out_privkey == nullptr) {
    return MNETA_ERR_INVALID_ARG;
  }

  memcpy(out_privkey, ctx->security.node_privkey, 32U);
  return MNETA_OK;
}

extern "C" mneta_err_t mneta_group_create(mneta_t *ctx, uint8_t out_group_hash[16], uint8_t out_group_key[16])
{
  int index;

  mneta_err_t state = mneta_require_init(ctx);
  if (state != MNETA_OK) {
    return state;
  }
  if (out_group_hash == nullptr || out_group_key == nullptr) {
    return MNETA_ERR_INVALID_ARG;
  }
  if (ctx->group_count >= MNETA_MAX_GROUPS) {
    return MNETA_ERR_FULL;
  }

  if (!mnet_identity_init() || p2p_security_random_fill(out_group_key, 16U) != P2P_SEC_OK) {
    return MNETA_ERR_CRYPTO;
  }

  mneta_compute_group_hash(out_group_key, out_group_hash);
  index = (int)ctx->group_count;
  memcpy(ctx->groups[index].group_hash, out_group_hash, 16U);
  memcpy(ctx->groups[index].group_key, out_group_key, 16U);

  if (p2p_security_add_group_key(&ctx->security, out_group_key) != P2P_SEC_OK) {
    memset(&ctx->groups[index], 0, sizeof(ctx->groups[index]));
    return MNETA_ERR_CRYPTO;
  }

  ctx->group_count++;
  if (!mneta_store_groups(ctx)) {
    return MNETA_ERR_STORAGE;
  }
  return MNETA_OK;
}

extern "C" mneta_err_t mneta_group_join(mneta_t *ctx, const uint8_t group_hash[16], const uint8_t group_key[16])
{
  int index;
  uint8_t expected_hash[16];

  mneta_err_t state = mneta_require_init(ctx);
  if (state != MNETA_OK) {
    return state;
  }
  if (group_hash == nullptr || group_key == nullptr) {
    return MNETA_ERR_INVALID_ARG;
  }
  if (ctx->group_count >= MNETA_MAX_GROUPS) {
    return MNETA_ERR_FULL;
  }

  mneta_compute_group_hash(group_key, expected_hash);
  if (memcmp(expected_hash, group_hash, 16U) != 0) {
    return MNETA_ERR_CRYPTO;
  }

  index = mneta_find_group_index(ctx, group_hash);
  if (index >= 0) {
    return MNETA_OK;
  }

  index = (int)ctx->group_count;
  memcpy(ctx->groups[index].group_hash, group_hash, 16U);
  memcpy(ctx->groups[index].group_key, group_key, 16U);

  if (p2p_security_add_group_key(&ctx->security, group_key) != P2P_SEC_OK) {
    memset(&ctx->groups[index], 0, sizeof(ctx->groups[index]));
    return MNETA_ERR_CRYPTO;
  }

  ctx->group_count++;
  if (!mneta_store_groups(ctx)) {
    return MNETA_ERR_STORAGE;
  }
  return MNETA_OK;
}

extern "C" mneta_err_t mneta_group_get(mneta_t *ctx, uint8_t index, mneta_group_t *out_group)
{
  mneta_err_t state = mneta_require_init(ctx);
  if (state != MNETA_OK) {
    return state;
  }
  if (out_group == nullptr) {
    return MNETA_ERR_INVALID_ARG;
  }
  if (index >= ctx->group_count) {
    return MNETA_ERR_NOT_FOUND;
  }

  *out_group = ctx->groups[index];
  return MNETA_OK;
}

extern "C" uint8_t mneta_group_count(mneta_t *ctx)
{
  return (ctx != nullptr && ctx->initialized) ? ctx->group_count : 0U;
}

extern "C" mneta_err_t mneta_handshake(mneta_t *ctx, const uint8_t remote_pubkey[32])
{
  mneta_err_t state = mneta_require_init(ctx);
  if (state != MNETA_OK) {
    return state;
  }
  if (remote_pubkey == nullptr) {
    return MNETA_ERR_INVALID_ARG;
  }
  return mneta_map_sec_err(p2p_security_handshake(&ctx->security, remote_pubkey));
}

extern "C" bool mneta_is_authenticated(mneta_t *ctx, const uint8_t remote_pubkey[32])
{
  return mneta_require_init(ctx) == MNETA_OK &&
         remote_pubkey != nullptr &&
         p2p_security_is_authenticated(&ctx->security, remote_pubkey);
}

extern "C" mneta_err_t mneta_encrypt_to(mneta_t *ctx,
                                        const uint8_t remote_pubkey[32],
                                        const uint8_t *plain,
                                        size_t plain_len,
                                        uint8_t *out,
                                        size_t *out_len)
{
  mneta_err_t state = mneta_require_init(ctx);
  if (state != MNETA_OK) {
    return state;
  }
  if (remote_pubkey == nullptr || out == nullptr || out_len == nullptr) {
    return MNETA_ERR_INVALID_ARG;
  }
  return mneta_map_sec_err(p2p_security_encrypt(&ctx->security, remote_pubkey, plain, plain_len, out, out_len));
}

extern "C" mneta_err_t mneta_decrypt_from(mneta_t *ctx,
                                          const uint8_t remote_pubkey[32],
                                          const uint8_t *cipher,
                                          size_t cipher_len,
                                          uint8_t *out,
                                          size_t *out_len)
{
  mneta_err_t state = mneta_require_init(ctx);
  if (state != MNETA_OK) {
    return state;
  }
  if (remote_pubkey == nullptr || cipher == nullptr || out == nullptr || out_len == nullptr) {
    return MNETA_ERR_INVALID_ARG;
  }
  return mneta_map_sec_err(p2p_security_decrypt(&ctx->security, remote_pubkey, cipher, cipher_len, out, out_len));
}

extern "C" mneta_err_t mneta_encrypt_group(mneta_t *ctx,
                                           const uint8_t group_hash[16],
                                           const uint8_t *plain,
                                           size_t plain_len,
                                           uint8_t *out,
                                           size_t *out_len)
{
  int index;

  mneta_err_t state = mneta_require_init(ctx);
  if (state != MNETA_OK) {
    return state;
  }
  if (group_hash == nullptr || out == nullptr || out_len == nullptr) {
    return MNETA_ERR_INVALID_ARG;
  }

  index = mneta_find_group_index(ctx, group_hash);
  if (index < 0) {
    return MNETA_ERR_NOT_FOUND;
  }

  return mneta_map_sec_err(p2p_security_encrypt_group(&ctx->security, (uint8_t)index, plain, plain_len, out, out_len));
}

extern "C" mneta_err_t mneta_decrypt_group(mneta_t *ctx,
                                           const uint8_t group_hash[16],
                                           const uint8_t *cipher,
                                           size_t cipher_len,
                                           uint8_t *out,
                                           size_t *out_len)
{
  int index;

  mneta_err_t state = mneta_require_init(ctx);
  if (state != MNETA_OK) {
    return state;
  }
  if (group_hash == nullptr || cipher == nullptr || out == nullptr || out_len == nullptr) {
    return MNETA_ERR_INVALID_ARG;
  }

  index = mneta_find_group_index(ctx, group_hash);
  if (index < 0) {
    return MNETA_ERR_NOT_FOUND;
  }

  return mneta_map_sec_err(p2p_security_decrypt_group(&ctx->security, (uint8_t)index, cipher, cipher_len, out, out_len));
}

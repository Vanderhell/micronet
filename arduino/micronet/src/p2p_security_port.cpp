#include "p2p_security.h"

#include <Preferences.h>
#include <esp_system.h>
#include <string.h>

extern "C" {
#include "mnet_identity.h"
}

namespace {

constexpr const char *kNamespace = "micronet_sec";
constexpr const char *kGroupsBlobKey = "groups";
constexpr uint32_t kMagic = 0x32434553UL;
constexpr uint8_t kVersion = 1U;

struct p2p_group_store_blob_t {
  uint32_t magic;
  uint8_t version;
  uint8_t group_count;
  uint8_t reserved[2];
  uint8_t group_keys[P2P_MAX_GROUPS][P2P_SESSION_KEY_SIZE];
};

mdh_err_t p2p_security_rng(uint8_t *buf, size_t len)
{
  if (buf == nullptr) {
    return MDH_ERR_RNG;
  }
  esp_fill_random(buf, len);
  return MDH_OK;
}

bool p2p_group_blob_valid(const p2p_group_store_blob_t &blob)
{
  return blob.magic == kMagic && blob.version == kVersion && blob.group_count <= P2P_MAX_GROUPS;
}

}  // namespace

extern "C" p2p_sec_err_t p2p_security_random_fill(uint8_t *out, size_t len)
{
  if (out == nullptr) {
    return P2P_SEC_ERR_KEYGEN;
  }
  esp_fill_random(out, len);
  return P2P_SEC_OK;
}

extern "C" p2p_sec_err_t p2p_security_generate_keypair(uint8_t privkey[P2P_NODE_KEY_SIZE],
                                                        uint8_t pubkey[P2P_NODE_KEY_SIZE])
{
  mdh_keypair_t kp;

  if (privkey == nullptr || pubkey == nullptr) {
    return P2P_SEC_ERR_KEYGEN;
  }

  if (mdh_generate_keypair(&kp, p2p_security_rng) != MDH_OK) {
    return P2P_SEC_ERR_KEYGEN;
  }

  memcpy(privkey, kp.privkey, sizeof(kp.privkey));
  memcpy(pubkey, kp.pubkey, sizeof(kp.pubkey));
  return P2P_SEC_OK;
}

extern "C" p2p_sec_err_t p2p_security_store_keys(const p2p_security_t *ctx)
{
  Preferences prefs;
  p2p_group_store_blob_t blob;

  if (ctx == nullptr || !ctx->store_keys) {
    return P2P_SEC_OK;
  }

  if (!prefs.begin(kNamespace, false)) {
    return P2P_SEC_ERR_KEYGEN;
  }

  memset(&blob, 0, sizeof(blob));
  blob.magic = kMagic;
  blob.version = kVersion;
  blob.group_count = ctx->group_count <= P2P_MAX_GROUPS ? ctx->group_count : P2P_MAX_GROUPS;
  memcpy(blob.group_keys, ctx->group_keys, sizeof(blob.group_keys));

  bool ok = prefs.putBytes(kGroupsBlobKey, &blob, sizeof(blob)) == sizeof(blob);
  prefs.end();
  return ok ? P2P_SEC_OK : P2P_SEC_ERR_KEYGEN;
}

extern "C" p2p_sec_err_t p2p_security_load_keys(p2p_security_t *ctx, bool *loaded)
{
  Preferences prefs;
  p2p_group_store_blob_t blob;

  if (ctx == nullptr || loaded == nullptr) {
    return P2P_SEC_ERR_KEYGEN;
  }

  *loaded = false;
  if (!prefs.begin(kNamespace, true)) {
    return P2P_SEC_OK;
  }

  memset(&blob, 0, sizeof(blob));
  size_t got = prefs.getBytes(kGroupsBlobKey, &blob, sizeof(blob));
  prefs.end();
  if (got != sizeof(blob) || !p2p_group_blob_valid(blob)) {
    return P2P_SEC_OK;
  }

  ctx->group_count = blob.group_count;
  memcpy(ctx->group_keys, blob.group_keys, sizeof(ctx->group_keys));
  *loaded = true;
  return P2P_SEC_OK;
}

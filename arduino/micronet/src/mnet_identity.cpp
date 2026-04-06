#include "mnet_identity.h"

#include <Preferences.h>
#include <esp_system.h>
#include <string.h>

extern "C" {
#include "mdh.h"
}

namespace {

constexpr const char *kNamespace = "micronet";
constexpr const char *kBlobKey = "identity";
constexpr uint32_t kMagic = 0x3144494dUL;
constexpr uint8_t kVersion = 1U;

struct mnet_identity_blob_t {
  uint32_t magic;
  uint8_t version;
  uint8_t reserved[3];
  uint8_t privkey[32];
  uint8_t pubkey[32];
};

Preferences g_preferences;
bool g_identity_ready = false;
uint8_t g_privkey[32];
uint8_t g_pubkey[32];

mdh_err_t mnet_identity_rng(uint8_t *buf, size_t len)
{
  if (buf == NULL) {
    return MDH_ERR_RNG;
  }
  esp_fill_random(buf, len);
  return MDH_OK;
}

bool mnet_identity_blob_valid(const mnet_identity_blob_t &blob)
{
  return blob.magic == kMagic && blob.version == kVersion;
}

bool mnet_identity_store_blob(const mnet_identity_blob_t &blob)
{
  return g_preferences.putBytes(kBlobKey, &blob, sizeof(blob)) == sizeof(blob);
}

bool mnet_identity_load_blob(mnet_identity_blob_t &blob)
{
  size_t got = g_preferences.getBytes(kBlobKey, &blob, sizeof(blob));
  return got == sizeof(blob) && mnet_identity_blob_valid(blob);
}

void mnet_identity_cache_blob(const mnet_identity_blob_t &blob)
{
  memcpy(g_privkey, blob.privkey, sizeof(g_privkey));
  memcpy(g_pubkey, blob.pubkey, sizeof(g_pubkey));
  g_identity_ready = true;
}

}  // namespace

extern "C" bool mnet_identity_init(void)
{
  mnet_identity_blob_t blob;
  mdh_keypair_t kp;

  if (g_identity_ready) {
    return true;
  }

  if (!g_preferences.begin(kNamespace, false)) {
    return false;
  }

  memset(&blob, 0, sizeof(blob));
  if (mnet_identity_load_blob(blob)) {
    mnet_identity_cache_blob(blob);
    g_preferences.end();
    return true;
  }

  if (mdh_generate_keypair(&kp, mnet_identity_rng) != MDH_OK) {
    g_preferences.end();
    return false;
  }

  memset(&blob, 0, sizeof(blob));
  blob.magic = kMagic;
  blob.version = kVersion;
  memcpy(blob.privkey, kp.privkey, sizeof(blob.privkey));
  memcpy(blob.pubkey, kp.pubkey, sizeof(blob.pubkey));

  if (!mnet_identity_store_blob(blob)) {
    g_preferences.end();
    return false;
  }

  mnet_identity_cache_blob(blob);
  g_preferences.end();
  return true;
}

extern "C" bool mnet_identity_get_node_id(uint8_t out_node_id[32])
{
  if (!g_identity_ready || out_node_id == NULL) {
    return false;
  }
  memcpy(out_node_id, g_pubkey, 32U);
  return true;
}

extern "C" bool mnet_identity_get_pubkey(uint8_t out_pubkey[32])
{
  if (!g_identity_ready || out_pubkey == NULL) {
    return false;
  }
  memcpy(out_pubkey, g_pubkey, 32U);
  return true;
}

extern "C" bool mnet_identity_get_privkey(uint8_t out_privkey[32])
{
  if (!g_identity_ready || out_privkey == NULL) {
    return false;
  }
  memcpy(out_privkey, g_privkey, 32U);
  return true;
}

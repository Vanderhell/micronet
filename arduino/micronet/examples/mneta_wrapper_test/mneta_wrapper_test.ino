#include "../../src/mnet_arduino.h"

namespace {

void print_hex(const char *label, const uint8_t *data, size_t len)
{
  static const char *kHex = "0123456789abcdef";
  char buf[129];
  size_t i;

  for (i = 0; i < len && (i * 2U + 1U) < sizeof(buf); ++i) {
    buf[i * 2U] = kHex[(data[i] >> 4) & 0x0F];
    buf[i * 2U + 1U] = kHex[data[i] & 0x0F];
  }
  buf[len * 2U] = '\0';

  Serial.print("MNETA_TEST|");
  Serial.print(label);
  Serial.print('=');
  Serial.println(buf);
}

void print_status(const char *event, int value)
{
  Serial.print("MNETA_TEST|event=");
  Serial.print(event);
  Serial.print("|value=");
  Serial.println(value);
}

}  // namespace

void setup()
{
  static const uint8_t kPlainText[] = "hello-from-wrapper";

  mneta_t local_ctx;
  mneta_t peer_ctx;
  mneta_config_t peer_cfg = {};
  uint8_t local_pubkey[32];
  uint8_t peer_pubkey[32];
  uint8_t group_hash[16];
  uint8_t group_key[16];
  uint8_t cipher[128];
  uint8_t plain[128];
  size_t cipher_len = sizeof(cipher);
  size_t plain_len = sizeof(plain);

  Serial.begin(115200);
  delay(1000);
  Serial.println("MNETA_TEST|event=boot");

  if (mneta_init(&local_ctx, nullptr) != MNETA_OK) {
    Serial.println("MNETA_TEST|event=local_init_failed");
    return;
  }

  if (p2p_security_generate_keypair(peer_cfg.node_privkey, peer_cfg.node_pubkey) != P2P_SEC_OK ||
      mneta_init(&peer_ctx, &peer_cfg) != MNETA_OK) {
    Serial.println("MNETA_TEST|event=peer_init_failed");
    return;
  }

  if (mneta_get_node_id(&local_ctx, local_pubkey) != MNETA_OK ||
      mneta_get_node_id(&peer_ctx, peer_pubkey) != MNETA_OK) {
    Serial.println("MNETA_TEST|event=get_node_id_failed");
    return;
  }

  print_hex("local_node_id", local_pubkey, sizeof(local_pubkey));
  print_hex("peer_node_id", peer_pubkey, sizeof(peer_pubkey));

  print_status("local_handshake", mneta_handshake(&local_ctx, peer_pubkey));
  print_status("peer_handshake", mneta_handshake(&peer_ctx, local_pubkey));

  if (!mneta_is_authenticated(&local_ctx, peer_pubkey) ||
      !mneta_is_authenticated(&peer_ctx, local_pubkey)) {
    Serial.println("MNETA_TEST|event=auth_failed");
    return;
  }

  if (mneta_encrypt_to(&local_ctx, peer_pubkey, kPlainText, sizeof(kPlainText) - 1U, cipher, &cipher_len) != MNETA_OK ||
      mneta_decrypt_from(&peer_ctx, local_pubkey, cipher, cipher_len, plain, &plain_len) != MNETA_OK) {
    Serial.println("MNETA_TEST|event=peer_crypto_failed");
    return;
  }

  plain[plain_len] = '\0';
  Serial.print("MNETA_TEST|event=peer_crypto_ok|text=");
  Serial.println(reinterpret_cast<const char *>(plain));

  if (mneta_group_create(&local_ctx, group_hash, group_key) != MNETA_OK ||
      mneta_group_join(&peer_ctx, group_hash, group_key) != MNETA_OK) {
    Serial.println("MNETA_TEST|event=group_setup_failed");
    return;
  }

  print_hex("group_hash", group_hash, sizeof(group_hash));
  print_hex("group_key", group_key, sizeof(group_key));

  cipher_len = sizeof(cipher);
  plain_len = sizeof(plain);
  if (mneta_encrypt_group(&local_ctx, group_hash, kPlainText, sizeof(kPlainText) - 1U, cipher, &cipher_len) != MNETA_OK ||
      mneta_decrypt_group(&peer_ctx, group_hash, cipher, cipher_len, plain, &plain_len) != MNETA_OK) {
    Serial.println("MNETA_TEST|event=group_crypto_failed");
    return;
  }

  plain[plain_len] = '\0';
  Serial.print("MNETA_TEST|event=group_crypto_ok|text=");
  Serial.println(reinterpret_cast<const char *>(plain));
  Serial.print("MNETA_TEST|event=group_count|value=");
  Serial.println(mneta_group_count(&local_ctx));
  Serial.println("MNETA_TEST|event=done");
}

void loop()
{
  delay(1000);
}

#include "../../src/mnet_identity.h"
#include "../../src/p2p_security.h"

namespace {

void print_hex(const char *label, const uint8_t *data, size_t len)
{
  static const char *kHex = "0123456789abcdef";
  char buf[65];
  size_t i;

  if (len > 32U) {
    len = 32U;
  }

  for (i = 0; i < len; ++i) {
    buf[i * 2U] = kHex[(data[i] >> 4) & 0x0F];
    buf[i * 2U + 1U] = kHex[data[i] & 0x0F];
  }
  buf[len * 2U] = '\0';

  Serial.print("SECURITY_TEST|");
  Serial.print(label);
  Serial.print('=');
  Serial.println(buf);
}

void print_status(const char *event, int value)
{
  Serial.print("SECURITY_TEST|event=");
  Serial.print(event);
  Serial.print("|value=");
  Serial.println(value);
}

}  // namespace

void setup()
{
  static const uint8_t kGroupKey[P2P_SESSION_KEY_SIZE] = {
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
      0x90, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xf1, 0x02};
  static const uint8_t kPlainText[] = "hello-secure-world";

  uint8_t local_node_id[32];
  uint8_t local_pubkey[32];
  uint8_t peer_pubkey[32];
  uint8_t cipher[128];
  uint8_t plain[128];
  size_t cipher_len = sizeof(cipher);
  size_t plain_len = sizeof(plain);

  p2p_security_t local_ctx;
  p2p_security_t peer_ctx;
  p2p_security_config_t local_cfg = {};
  p2p_security_config_t peer_cfg = {};

  Serial.begin(115200);
  delay(1000);
  Serial.println("SECURITY_TEST|event=boot");

  if (!mnet_identity_init() || !mnet_identity_get_node_id(local_node_id)) {
    Serial.println("SECURITY_TEST|event=identity_init_failed");
    return;
  }

  print_hex("node_id", local_node_id, sizeof(local_node_id));

  if (p2p_security_generate_keypair(peer_cfg.node_privkey, peer_cfg.node_pubkey) != P2P_SEC_OK) {
    Serial.println("SECURITY_TEST|event=peer_keygen_failed");
    return;
  }

  if (p2p_security_init(&local_ctx, &local_cfg) != P2P_SEC_OK) {
    Serial.println("SECURITY_TEST|event=local_init_failed");
    return;
  }

  if (p2p_security_init(&peer_ctx, &peer_cfg) != P2P_SEC_OK) {
    Serial.println("SECURITY_TEST|event=peer_init_failed");
    return;
  }

  if (p2p_security_get_pubkey(&local_ctx, local_pubkey) != P2P_SEC_OK ||
      p2p_security_get_pubkey(&peer_ctx, peer_pubkey) != P2P_SEC_OK) {
    Serial.println("SECURITY_TEST|event=get_pubkey_failed");
    return;
  }

  print_hex("local_pubkey", local_pubkey, sizeof(local_pubkey));
  print_hex("peer_pubkey", peer_pubkey, sizeof(peer_pubkey));

  print_status("local_handshake", p2p_security_handshake(&local_ctx, peer_pubkey));
  print_status("peer_handshake", p2p_security_handshake(&peer_ctx, local_pubkey));

  if (!p2p_security_is_authenticated(&local_ctx, peer_pubkey) ||
      !p2p_security_is_authenticated(&peer_ctx, local_pubkey)) {
    Serial.println("SECURITY_TEST|event=auth_failed");
    return;
  }

  if (p2p_security_encrypt(&local_ctx,
                           peer_pubkey,
                           kPlainText,
                           sizeof(kPlainText) - 1U,
                           cipher,
                           &cipher_len) != P2P_SEC_OK) {
    Serial.println("SECURITY_TEST|event=encrypt_failed");
    return;
  }

  if (p2p_security_decrypt(&peer_ctx,
                           local_pubkey,
                           cipher,
                           cipher_len,
                           plain,
                           &plain_len) != P2P_SEC_OK) {
    Serial.println("SECURITY_TEST|event=decrypt_failed");
    return;
  }

  plain[plain_len] = '\0';
  Serial.print("SECURITY_TEST|event=session_ok|text=");
  Serial.println(reinterpret_cast<const char *>(plain));

  if (p2p_security_add_group_key(&local_ctx, kGroupKey) != P2P_SEC_OK ||
      p2p_security_add_group_key(&peer_ctx, kGroupKey) != P2P_SEC_OK) {
    Serial.println("SECURITY_TEST|event=group_add_failed");
    return;
  }

  cipher_len = sizeof(cipher);
  plain_len = sizeof(plain);
  if (p2p_security_encrypt_group(&local_ctx, 0U, kPlainText, sizeof(kPlainText) - 1U, cipher, &cipher_len) != P2P_SEC_OK ||
      p2p_security_decrypt_group(&peer_ctx, 0U, cipher, cipher_len, plain, &plain_len) != P2P_SEC_OK) {
    Serial.println("SECURITY_TEST|event=group_crypto_failed");
    return;
  }

  plain[plain_len] = '\0';
  Serial.print("SECURITY_TEST|event=group_ok|text=");
  Serial.println(reinterpret_cast<const char *>(plain));
  Serial.println("SECURITY_TEST|event=done");
}

void loop()
{
  delay(1000);
}

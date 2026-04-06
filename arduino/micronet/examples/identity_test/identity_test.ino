#include <Preferences.h>

#include "../../src/mnet_identity.h"

static Preferences g_test_prefs;

static void fill_hex(char *dst, size_t dst_len, const uint8_t *src, size_t src_len)
{
  static const char hex[] = "0123456789abcdef";
  size_t i;

  if (dst == NULL || src == NULL || dst_len < ((src_len * 2U) + 1U)) {
    return;
  }

  for (i = 0U; i < src_len; ++i) {
    dst[i * 2U] = hex[(src[i] >> 4) & 0x0FU];
    dst[(i * 2U) + 1U] = hex[src[i] & 0x0FU];
  }
  dst[src_len * 2U] = '\0';
}

void setup()
{
  uint8_t node_id[32];
  char node_id_hex[65];
  char previous_hex[65];
  size_t got = 0U;

  Serial.begin(115200);
  delay(1000);

  if (!mnet_identity_init()) {
    Serial.println("IDENTITY_TEST|event=init_fail");
    return;
  }
  if (!mnet_identity_get_node_id(node_id)) {
    Serial.println("IDENTITY_TEST|event=get_fail");
    return;
  }

  fill_hex(node_id_hex, sizeof(node_id_hex), node_id, sizeof(node_id));
  Serial.printf("IDENTITY_TEST|event=node_id|value=%s\r\n", node_id_hex);

  if (!g_test_prefs.begin("mnet_id_test", false)) {
    Serial.println("IDENTITY_TEST|event=prefs_fail");
    return;
  }

  memset(previous_hex, 0, sizeof(previous_hex));
  got = g_test_prefs.getBytes("last_node_id", previous_hex, sizeof(previous_hex));
  if (got == (sizeof(previous_hex) - 1U) && strcmp(previous_hex, node_id_hex) == 0) {
    Serial.println("IDENTITY_TEST|event=persistence_ok");
  } else if (got > 0U) {
    Serial.printf("IDENTITY_TEST|event=persistence_mismatch|previous=%s\r\n", previous_hex);
  } else {
    Serial.println("IDENTITY_TEST|event=first_boot");
  }

  g_test_prefs.putBytes("last_node_id", node_id_hex, sizeof(node_id_hex) - 1U);
  g_test_prefs.end();
}

void loop()
{
  delay(1000);
}

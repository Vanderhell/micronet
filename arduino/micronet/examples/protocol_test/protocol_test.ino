#include <WiFi.h>

#define MNET_ARDUINO_IMPLEMENTATION
#include "../../src/mnet_bundle.h"

static const uint8_t NODE_SLOT = 1;
static const char *WIFI_SSID = "SSID";
static const char *WIFI_PASSWORD = "PASSWORD";
static const uint16_t UDP_PORT = 33455;

static const char *NODE1_IP = "192.168.1.131";
static const char *NODE2_IP = "192.168.1.132";
static const char *NETMASK_IP = "255.255.255.0";
static const char *GATEWAY_IP = "192.168.1.1";
static const char *DNS1_IP = "8.8.8.8";
static const char *DNS2_IP = "1.1.1.1";

static const char *PEER_NODE_ID_HEX =
    "0000000000000000000000000000000000000000000000000000000000000000";

static const uint8_t MSG_TEXT = 1U;

static mneta_t g_mneta;
static MNetTransport g_transport;
static MNetProtocol g_protocol;
static char g_serial_line[160];
static size_t g_serial_len = 0U;
static uint8_t g_peer_pubkey[32];
static uint8_t g_local_node_id[32];

static bool parse_ip(const char *text, IPAddress &out_ip)
{
  uint8_t a, b, c, d;
  if (sscanf(text, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
    return false;
  }
  out_ip = IPAddress(a, b, c, d);
  return true;
}

static const char *local_ip_text()
{
  return NODE_SLOT == 1U ? NODE1_IP : NODE2_IP;
}

static const char *peer_ip_text()
{
  return NODE_SLOT == 1U ? NODE2_IP : NODE1_IP;
}

static void fill_hex(char *dst, size_t dst_len, const uint8_t *src, size_t src_len)
{
  static const char hex[] = "0123456789abcdef";
  size_t i;

  if (dst == NULL || dst_len == 0U || src == NULL || dst_len < (src_len * 2U + 1U)) {
    return;
  }

  for (i = 0U; i < src_len; ++i) {
    dst[i * 2U] = hex[(src[i] >> 4) & 0x0FU];
    dst[i * 2U + 1U] = hex[src[i] & 0x0FU];
  }
  dst[src_len * 2U] = '\0';
}

static int hex_nibble(char ch)
{
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  return -1;
}

static bool parse_hex_32(const char *text, uint8_t out[32])
{
  size_t i;

  if (text == NULL || out == NULL || strlen(text) != 64U) {
    return false;
  }
  for (i = 0U; i < 32U; ++i) {
    int hi = hex_nibble(text[i * 2U]);
    int lo = hex_nibble(text[i * 2U + 1U]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

static bool wifi_connect()
{
  IPAddress local_ip;
  IPAddress netmask;
  IPAddress gateway;
  IPAddress dns1;
  IPAddress dns2;
  uint32_t started = millis();

  if (!parse_ip(local_ip_text(), local_ip) ||
      !parse_ip(NETMASK_IP, netmask) ||
      !parse_ip(GATEWAY_IP, gateway) ||
      !parse_ip(DNS1_IP, dns1) ||
      !parse_ip(DNS2_IP, dns2)) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  if (!WiFi.config(local_ip, gateway, netmask, dns1, dns2)) {
    return false;
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - started) > 20000UL) {
      return false;
    }
    delay(250);
  }

  Serial.printf("PROTOCOL_TEST|node=%u|event=wifi_ok|ip=%s\r\n",
                (unsigned)NODE_SLOT,
                WiFi.localIP().toString().c_str());
  return true;
}

static void on_text_message(const MNetProtocolMessage &msg, void *user)
{
  char text[481];
  (void)user;

  memcpy(text, msg.payload, msg.payload_len);
  text[msg.payload_len] = '\0';
  Serial.printf("PROTOCOL_TEST|node=%u|event=rx|msg_type=%u|msg_id=%u|text=%s\r\n",
                (unsigned)NODE_SLOT,
                (unsigned)msg.msg_type,
                (unsigned)msg.msg_id,
                text);
}

static void print_help()
{
  Serial.println("Commands:");
  Serial.println("  help");
  Serial.println("  whoami");
  Serial.println("  status");
  Serial.println("  send <text>");
}

static void print_identity()
{
  char node_id_hex[65];
  fill_hex(node_id_hex, sizeof(node_id_hex), g_local_node_id, sizeof(g_local_node_id));
  Serial.printf("PROTOCOL_TEST|node=%u|event=identity|node_id=%s\r\n",
                (unsigned)NODE_SLOT,
                node_id_hex);
}

static void print_status()
{
  Serial.printf("PROTOCOL_TEST|node=%u|event=status|wifi=%s|ip=%s|udp=%u|peer_ip=%s\r\n",
                (unsigned)NODE_SLOT,
                WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                WiFi.localIP().toString().c_str(),
                g_transport.ready() ? 1U : 0U,
                peer_ip_text());
}

static void handle_command(const char *line)
{
  if (strcmp(line, "help") == 0) {
    print_help();
    return;
  }
  if (strcmp(line, "whoami") == 0) {
    print_identity();
    return;
  }
  if (strcmp(line, "status") == 0) {
    print_status();
    return;
  }
  if (strncmp(line, "send ", 5) == 0) {
    bool ok = g_protocol.sendCustomTextTo(g_peer_pubkey, MSG_TEXT, line + 5);
    Serial.printf("PROTOCOL_TEST|node=%u|event=send|ok=%u|text=%s\r\n",
                  (unsigned)NODE_SLOT,
                  ok ? 1U : 0U,
                  line + 5);
    return;
  }
  Serial.println("Unknown command. Type 'help'.");
}

static void poll_serial()
{
  while (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      g_serial_line[g_serial_len] = '\0';
      if (g_serial_len > 0U) {
        handle_command(g_serial_line);
      }
      g_serial_len = 0U;
      continue;
    }
    if (g_serial_len + 1U < sizeof(g_serial_line)) {
      g_serial_line[g_serial_len++] = (char)ch;
    }
  }
}

void setup()
{
  IPAddress peer_ip;

  Serial.begin(115200);
  delay(1000);
  print_help();

  if (mneta_init(&g_mneta, NULL) != MNETA_OK ||
      mneta_get_node_id(&g_mneta, g_local_node_id) != MNETA_OK) {
    Serial.printf("PROTOCOL_TEST|node=%u|event=identity_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }
  print_identity();

  if (!parse_hex_32(PEER_NODE_ID_HEX, g_peer_pubkey) ||
      mneta_handshake(&g_mneta, g_peer_pubkey) != MNETA_OK) {
    Serial.printf("PROTOCOL_TEST|node=%u|event=handshake_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }

  if (!wifi_connect()) {
    Serial.printf("PROTOCOL_TEST|node=%u|event=wifi_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }
  if (!g_transport.begin(&g_mneta, UDP_PORT) ||
      !parse_ip(peer_ip_text(), peer_ip) ||
      !g_transport.addPeer(g_peer_pubkey, peer_ip, UDP_PORT) ||
      !g_protocol.begin(&g_transport) ||
      !g_protocol.registerHandler(MSG_TEXT, on_text_message, NULL)) {
    Serial.printf("PROTOCOL_TEST|node=%u|event=protocol_init_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }

  Serial.printf("PROTOCOL_TEST|node=%u|event=ready|peer_ip=%s|port=%u\r\n",
                (unsigned)NODE_SLOT,
                peer_ip.toString().c_str(),
                (unsigned)UDP_PORT);
}

void loop()
{
  MNetProtocolMessage msg;

  poll_serial();
  if (WiFi.status() != WL_CONNECTED || !g_transport.ready()) {
    delay(20);
    return;
  }

  (void)g_protocol.tick(msg);
  delay(20);
}

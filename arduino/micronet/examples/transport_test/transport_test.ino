#include <WiFi.h>

#define MNET_ARDUINO_IMPLEMENTATION
#include "../../src/mnet_bundle.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef MNET_WIFI_SSID
#define MNET_WIFI_SSID "your_wifi_ssid"
#endif
#ifndef MNET_WIFI_PASSWORD
#define MNET_WIFI_PASSWORD "your_wifi_password"
#endif

/*
 * Two-node encrypted UDP test for the Arduino micronet port.
 *
 * Flash the same sketch to both boards.
 * Change only:
 * - NODE_SLOT
 * - WIFI_SSID / WIFI_PASSWORD
 * - NODE1_IP / NODE2_IP
 * - PEER_NODE_ID_HEX after first boot
 *
 * Flow:
 * 1. Boot both boards once and note printed node_id values.
 * 2. Paste node-2 node_id into node-1 PEER_NODE_ID_HEX.
 * 3. Paste node-1 node_id into node-2 PEER_NODE_ID_HEX.
 * 4. Reflash both boards.
 * 5. On node-1 type `send hello`.
 * 6. Node-2 should print decrypted payload.
 *
 * Note:
 * - `handshake_fail` on first boot is expected while `PEER_NODE_ID_HEX` is still placeholder.
 * - Captured real node-1 id from hardware: `9e72dd2cf08210fcff5dfdff5033b9ffe47465af947d2a6a41b0b7589cff2304`
 *
 * UART commands:
 * - help
 * - whoami
 * - status
 * - stun
 * - send <text>
 */

static const uint8_t NODE_SLOT = 2;
static const char *WIFI_SSID = MNET_WIFI_SSID;
static const char *WIFI_PASSWORD = MNET_WIFI_PASSWORD;
static const uint16_t UDP_PORT = 33444;

static const char *NODE1_IP = "192.168.1.121";
static const char *NODE2_IP = "192.168.1.122";
static const char *NETMASK_IP = "255.255.255.0";
static const char *GATEWAY_IP = "192.168.1.1";
static const char *DNS1_IP = "8.8.8.8";
static const char *DNS2_IP = "1.1.1.1";

static const char *PEER_NODE_ID_HEX =
    "9e72dd2cf08210fcff5dfdff5033b9ffe47465af947d2a6a41b0b7589cff2304";

static mneta_t g_mneta;
static MNetTransport g_transport;
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
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + (ch - 'A');
  }
  return -1;
}

static bool parse_hex_32(const char *text, uint8_t out[32])
{
  size_t i;

  if (text == NULL || strlen(text) != 64U || out == NULL) {
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
  Serial.printf("TRANSPORT_TEST|node=%u|event=wifi_connect|ssid=%s\r\n",
                (unsigned)NODE_SLOT,
                WIFI_SSID);

  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - started) > 20000UL) {
      Serial.printf("TRANSPORT_TEST|node=%u|event=wifi_fail\r\n", (unsigned)NODE_SLOT);
      return false;
    }
    delay(250);
    Serial.print('.');
  }

  Serial.println();
  Serial.printf("TRANSPORT_TEST|node=%u|event=wifi_ok|ip=%s\r\n",
                (unsigned)NODE_SLOT,
                WiFi.localIP().toString().c_str());
  return true;
}

static void print_help()
{
  Serial.println("Commands:");
  Serial.println("  help");
  Serial.println("  whoami");
  Serial.println("  status");
  Serial.println("  stun");
  Serial.println("  send <text>");
}

static void print_identity()
{
  char node_id_hex[65];
  fill_hex(node_id_hex, sizeof(node_id_hex), g_local_node_id, sizeof(g_local_node_id));
  Serial.printf("TRANSPORT_TEST|node=%u|event=identity|node_id=%s\r\n",
                (unsigned)NODE_SLOT,
                node_id_hex);
}

static void print_status()
{
  char peer_hex[65];

  fill_hex(peer_hex, sizeof(peer_hex), g_peer_pubkey, sizeof(g_peer_pubkey));
  Serial.printf("TRANSPORT_TEST|node=%u|event=status|wifi=%s|ip=%s|udp=%u|peer_ip=%s|peer_node_id=%s\r\n",
                (unsigned)NODE_SLOT,
                WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                WiFi.localIP().toString().c_str(),
                g_transport.ready() ? 1U : 0U,
                peer_ip_text(),
                peer_hex);
}

static void run_stun()
{
  IPAddress ext_ip;
  uint16_t ext_port = 0U;

  if (g_transport.resolveExternal(ext_ip, ext_port)) {
    Serial.printf("TRANSPORT_TEST|node=%u|event=stun_ok|mapped=%s:%u\r\n",
                  (unsigned)NODE_SLOT,
                  ext_ip.toString().c_str(),
                  (unsigned)ext_port);
    return;
  }

  Serial.printf("TRANSPORT_TEST|node=%u|event=stun_fail\r\n", (unsigned)NODE_SLOT);
}

static void send_text(const char *text)
{
  bool ok = g_transport.sendTo(g_peer_pubkey,
                               (const uint8_t *)text,
                               text != NULL ? strlen(text) : 0U);

  Serial.printf("TRANSPORT_TEST|node=%u|event=send|ok=%u|text=%s\r\n",
                (unsigned)NODE_SLOT,
                ok ? 1U : 0U,
                text != NULL ? text : "");
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
  if (strcmp(line, "stun") == 0) {
    run_stun();
    return;
  }
  if (strncmp(line, "send ", 5) == 0) {
    send_text(line + 5);
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
  randomSeed((uint32_t)micros());

  print_help();
  if (mneta_init(&g_mneta, NULL) != MNETA_OK) {
    Serial.printf("TRANSPORT_TEST|node=%u|event=mneta_init_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }
  if (mneta_get_node_id(&g_mneta, g_local_node_id) != MNETA_OK) {
    Serial.printf("TRANSPORT_TEST|node=%u|event=node_id_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }
  print_identity();

  if (!parse_hex_32(PEER_NODE_ID_HEX, g_peer_pubkey)) {
    Serial.printf("TRANSPORT_TEST|node=%u|event=peer_id_parse_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }
  if (mneta_handshake(&g_mneta, g_peer_pubkey) != MNETA_OK) {
    Serial.printf("TRANSPORT_TEST|node=%u|event=handshake_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }

  if (!wifi_connect()) {
    return;
  }
  if (!g_transport.begin(&g_mneta, UDP_PORT)) {
    Serial.printf("TRANSPORT_TEST|node=%u|event=transport_begin_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }
  if (!parse_ip(peer_ip_text(), peer_ip) || !g_transport.addPeer(g_peer_pubkey, peer_ip, UDP_PORT)) {
    Serial.printf("TRANSPORT_TEST|node=%u|event=peer_add_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }

  Serial.printf("TRANSPORT_TEST|node=%u|event=ready|peer_ip=%s|port=%u\r\n",
                (unsigned)NODE_SLOT,
                peer_ip.toString().c_str(),
                (unsigned)UDP_PORT);
}

void loop()
{
  MNetTransportPacket packet;
  char text[513];

  poll_serial();
  if (WiFi.status() != WL_CONNECTED || !g_transport.ready()) {
    delay(20);
    return;
  }

  if (g_transport.tick(packet) && packet.valid) {
    memcpy(text, packet.payload, packet.payload_len);
    text[packet.payload_len] = '\0';
    Serial.printf("TRANSPORT_TEST|node=%u|event=recv|from=%s|text=%s\r\n",
                  (unsigned)NODE_SLOT,
                  packet.remote_ip.toString().c_str(),
                  text);
  }

  delay(20);
}

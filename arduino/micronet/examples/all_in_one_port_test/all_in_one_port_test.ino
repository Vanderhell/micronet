#include <WiFi.h>

#define MNET_ARDUINO_IMPLEMENTATION
#include "../../src/mnet_bundle.h"

static const uint8_t NODE_SLOT = 1;
static const char *WIFI_SSID = "SSID";
static const char *WIFI_PASSWORD = "PASSWORD";
static const uint16_t UDP_PORT = 33477;

static const char *NODE1_IP = "192.168.1.151";
static const char *NODE2_IP = "192.168.1.152";
static const char *NETMASK_IP = "255.255.255.0";
static const char *GATEWAY_IP = "192.168.1.1";
static const char *DNS1_IP = "8.8.8.8";
static const char *DNS2_IP = "1.1.1.1";

static const char *PEER_NODE_ID_HEX =
    "0000000000000000000000000000000000000000000000000000000000000000";

/*
 * Hardware note:
 * - this is the final two-board smoke test over the new Arduino port
 * - run it only after `transport_test`, `protocol_test`, and `data_test` pass
 * - like the other two-board examples, it needs the real peer `node_id`
 */

static const uint8_t MSG_TEXT = 1U;

static mneta_t g_mneta;
static MNetTransport g_transport;
static MNetProtocol g_protocol;
static MNetData g_data;
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

  Serial.printf("ALLINONE|node=%u|event=wifi_ok|ip=%s\r\n",
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
  Serial.printf("ALLINONE|node=%u|event=text|msg_id=%u|value=%s\r\n",
                (unsigned)NODE_SLOT,
                (unsigned)msg.msg_id,
                text);
}

static void on_request_result(const uint8_t src_pubkey[32], const char *key, const char *value, void *user)
{
  char src_hex[65];
  (void)user;
  fill_hex(src_hex, sizeof(src_hex), src_pubkey, 32U);
  Serial.printf("ALLINONE|node=%u|event=request|src=%s|key=%s|value=%s\r\n",
                (unsigned)NODE_SLOT,
                src_hex,
                key != NULL ? key : "",
                value != NULL ? value : "");
}

static void on_list_result(const uint8_t src_pubkey[32], const char *csv_keys, void *user)
{
  char src_hex[65];
  (void)user;
  fill_hex(src_hex, sizeof(src_hex), src_pubkey, 32U);
  Serial.printf("ALLINONE|node=%u|event=list|src=%s|keys=%s\r\n",
                (unsigned)NODE_SLOT,
                src_hex,
                csv_keys != NULL ? csv_keys : "");
}

static void on_metrics_result(const uint8_t src_pubkey[32], const MNetDataMetrics &metrics, void *user)
{
  char src_hex[65];
  (void)user;
  fill_hex(src_hex, sizeof(src_hex), src_pubkey, 32U);
  Serial.printf("ALLINONE|node=%u|event=metrics|src=%s|uptime=%lu|heap=%lu|tx=%lu|rx=%lu\r\n",
                (unsigned)NODE_SLOT,
                src_hex,
                (unsigned long)metrics.uptime_s,
                (unsigned long)metrics.free_heap,
                (unsigned long)metrics.packets_sent,
                (unsigned long)metrics.packets_recv);
}

static void on_notify_result(const uint8_t src_pubkey[32], const char *key, const char *value, void *user)
{
  char src_hex[65];
  (void)user;
  fill_hex(src_hex, sizeof(src_hex), src_pubkey, 32U);
  Serial.printf("ALLINONE|node=%u|event=notify|src=%s|key=%s|value=%s\r\n",
                (unsigned)NODE_SLOT,
                src_hex,
                key != NULL ? key : "",
                value != NULL ? value : "");
}

static void on_query_result(const uint8_t src_pubkey[32], const char *rows, void *user)
{
  char src_hex[65];
  (void)user;
  fill_hex(src_hex, sizeof(src_hex), src_pubkey, 32U);
  Serial.printf("ALLINONE|node=%u|event=query|src=%s|rows=%s\r\n",
                (unsigned)NODE_SLOT,
                src_hex,
                rows != NULL ? rows : "");
}

static void print_help()
{
  Serial.println("Commands:");
  Serial.println("  help");
  Serial.println("  whoami");
  Serial.println("  status");
  Serial.println("  stun");
  Serial.println("  vars");
  Serial.println("  send <text>");
  Serial.println("  set <key> <value>");
  Serial.println("  request <key>");
  Serial.println("  list");
  Serial.println("  metrics");
  Serial.println("  subscribe <key>");
  Serial.println("  unsubscribe <key>");
  Serial.println("  query <prefix>");
}

static void print_identity()
{
  char node_id_hex[65];
  fill_hex(node_id_hex, sizeof(node_id_hex), g_local_node_id, sizeof(g_local_node_id));
  Serial.printf("ALLINONE|node=%u|event=identity|node_id=%s\r\n",
                (unsigned)NODE_SLOT,
                node_id_hex);
}

static void print_status()
{
  Serial.printf("ALLINONE|node=%u|event=status|wifi=%s|ip=%s|udp=%u|vars=%u\r\n",
                (unsigned)NODE_SLOT,
                WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                WiFi.localIP().toString().c_str(),
                g_transport.ready() ? 1U : 0U,
                (unsigned)g_data.varCount());
}

static void print_vars()
{
  char value[96];
  if (g_data.getLocal("node_name", value, sizeof(value))) {
    Serial.printf("ALLINONE|node=%u|event=var|key=node_name|value=%s\r\n", (unsigned)NODE_SLOT, value);
  }
  if (g_data.getLocal("temperature_c", value, sizeof(value))) {
    Serial.printf("ALLINONE|node=%u|event=var|key=temperature_c|value=%s\r\n", (unsigned)NODE_SLOT, value);
  }
}

static void run_stun()
{
  IPAddress ext_ip;
  uint16_t ext_port = 0U;
  if (g_transport.resolveExternal(ext_ip, ext_port)) {
    Serial.printf("ALLINONE|node=%u|event=stun_ok|mapped=%s:%u\r\n",
                  (unsigned)NODE_SLOT,
                  ext_ip.toString().c_str(),
                  (unsigned)ext_port);
  } else {
    Serial.printf("ALLINONE|node=%u|event=stun_fail\r\n", (unsigned)NODE_SLOT);
  }
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
  if (strcmp(line, "vars") == 0) {
    print_vars();
    return;
  }
  if (strncmp(line, "send ", 5) == 0) {
    (void)g_protocol.sendCustomTextTo(g_peer_pubkey, MSG_TEXT, line + 5);
    return;
  }
  if (strncmp(line, "request ", 8) == 0) {
    (void)g_data.request(g_peer_pubkey, line + 8);
    return;
  }
  if (strcmp(line, "list") == 0) {
    (void)g_data.listVars(g_peer_pubkey);
    return;
  }
  if (strcmp(line, "metrics") == 0) {
    (void)g_data.getMetrics(g_peer_pubkey);
    return;
  }
  if (strncmp(line, "subscribe ", 10) == 0) {
    (void)g_data.subscribe(g_peer_pubkey, line + 10);
    return;
  }
  if (strncmp(line, "unsubscribe ", 12) == 0) {
    (void)g_data.unsubscribe(g_peer_pubkey, line + 12);
    return;
  }
  if (strncmp(line, "query ", 6) == 0) {
    (void)g_data.query(g_peer_pubkey, line + 6);
    return;
  }
  if (strncmp(line, "set ", 4) == 0) {
    const char *key = line + 4;
    const char *value = strchr(key, ' ');
    if (value != NULL) {
      char key_buf[32];
      size_t key_len = (size_t)(value - key);
      if (key_len >= sizeof(key_buf)) {
        key_len = sizeof(key_buf) - 1U;
      }
      memcpy(key_buf, key, key_len);
      key_buf[key_len] = '\0';
      while (*value == ' ') {
        value++;
      }
      (void)g_data.update(key_buf, value);
    }
    return;
  }
  Serial.println("Unknown command. Type 'help'.");
}

static void poll_serial()
{
  while (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch == '\r') continue;
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
  char node_name[16];

  Serial.begin(115200);
  delay(1000);
  print_help();

  if (mneta_init(&g_mneta, NULL) != MNETA_OK ||
      mneta_get_node_id(&g_mneta, g_local_node_id) != MNETA_OK) {
    Serial.printf("ALLINONE|node=%u|event=identity_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }
  print_identity();

  if (!parse_hex_32(PEER_NODE_ID_HEX, g_peer_pubkey) ||
      mneta_handshake(&g_mneta, g_peer_pubkey) != MNETA_OK) {
    Serial.printf("ALLINONE|node=%u|event=handshake_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }

  if (!wifi_connect()) {
    Serial.printf("ALLINONE|node=%u|event=wifi_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }
  if (!g_transport.begin(&g_mneta, UDP_PORT) ||
      !parse_ip(peer_ip_text(), peer_ip) ||
      !g_transport.addPeer(g_peer_pubkey, peer_ip, UDP_PORT) ||
      !g_protocol.begin(&g_transport) ||
      !g_protocol.registerHandler(MSG_TEXT, on_text_message, NULL) ||
      !g_data.begin(&g_protocol)) {
    Serial.printf("ALLINONE|node=%u|event=init_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }

  g_data.setRequestCallback(&on_request_result, NULL);
  g_data.setListCallback(&on_list_result, NULL);
  g_data.setMetricsCallback(&on_metrics_result, NULL);
  g_data.setNotifyCallback(&on_notify_result, NULL);
  g_data.setQueryCallback(&on_query_result, NULL);

  snprintf(node_name, sizeof(node_name), "node-%u", (unsigned)NODE_SLOT);
  (void)g_data.publish("node_name", node_name);
  (void)g_data.publish("temperature_c", NODE_SLOT == 1U ? "21.50" : "22.75");

  Serial.printf("ALLINONE|node=%u|event=ready|peer_ip=%s|port=%u\r\n",
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

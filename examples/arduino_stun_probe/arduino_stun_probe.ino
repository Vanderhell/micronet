#include <WiFi.h>
#include <WiFiUdp.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

/*
 * Arduino STUN probe for ESP32 / ESP32-S3.
 *
 * Usage:
 * 1. Fill in secrets.h.
 * 2. Select your ESP32-S3 board in Arduino IDE.
 * 3. Upload the sketch.
 * 4. Open Serial Monitor at 115200 baud.
 * 5. Type: probe
 *
 * UART commands:
 * - help
 * - status
 * - probe
 */

#ifndef MNET_WIFI_SSID
#define MNET_WIFI_SSID ""
#endif
#ifndef MNET_WIFI_PASSWORD
#define MNET_WIFI_PASSWORD ""
#endif
#ifndef MNET_STUN_HOST
#define MNET_STUN_HOST ""
#endif
#ifndef MNET_STUN_PORT
#define MNET_STUN_PORT 19302U
#endif

static const char *WIFI_SSID = MNET_WIFI_SSID;
static const char *WIFI_PASSWORD = MNET_WIFI_PASSWORD;
static const char *STUN_HOST = MNET_STUN_HOST;
static const uint16_t STUN_PORT = (uint16_t)MNET_STUN_PORT;
static const uint16_t LOCAL_UDP_PORT = 3479;

static const uint16_t STUN_BINDING_REQUEST = 0x0001U;
static const uint16_t STUN_BINDING_SUCCESS = 0x0101U;
static const uint32_t STUN_MAGIC_COOKIE = 0x2112A442UL;
static const uint16_t STUN_ATTR_MAPPED_ADDRESS = 0x0001U;
static const uint16_t STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020U;

static WiFiUDP g_udp;
static bool g_udp_ready = false;
static bool g_last_probe_ok = false;
static IPAddress g_last_mapped_ip(0, 0, 0, 0);
static uint16_t g_last_mapped_port = 0U;
static char g_serial_line[96];
static size_t g_serial_len = 0U;

static uint16_t read_u16(const uint8_t *src)
{
  return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

static uint32_t read_u32(const uint8_t *src)
{
  return ((uint32_t)src[0] << 24) |
         ((uint32_t)src[1] << 16) |
         ((uint32_t)src[2] << 8) |
         (uint32_t)src[3];
}

static void write_u16(uint8_t *dst, uint16_t value)
{
  dst[0] = (uint8_t)((value >> 8) & 0xFFU);
  dst[1] = (uint8_t)(value & 0xFFU);
}

static void write_u32(uint8_t *dst, uint32_t value)
{
  dst[0] = (uint8_t)((value >> 24) & 0xFFU);
  dst[1] = (uint8_t)((value >> 16) & 0xFFU);
  dst[2] = (uint8_t)((value >> 8) & 0xFFU);
  dst[3] = (uint8_t)(value & 0xFFU);
}

static bool parse_address_attr(const uint8_t *attr,
                               size_t attr_len,
                               IPAddress &out_ip,
                               uint16_t &out_port,
                               bool is_xor)
{
  uint8_t ip[4];
  uint16_t port;

  if (attr == NULL || attr_len < 8U || attr[1] != 0x01U) {
    return false;
  }

  port = read_u16(&attr[2]);
  memcpy(ip, &attr[4], sizeof(ip));

  if (is_xor) {
    port ^= (uint16_t)(STUN_MAGIC_COOKIE >> 16);
    ip[0] ^= (uint8_t)((STUN_MAGIC_COOKIE >> 24) & 0xFFU);
    ip[1] ^= (uint8_t)((STUN_MAGIC_COOKIE >> 16) & 0xFFU);
    ip[2] ^= (uint8_t)((STUN_MAGIC_COOKIE >> 8) & 0xFFU);
    ip[3] ^= (uint8_t)(STUN_MAGIC_COOKIE & 0xFFU);
  }

  out_ip = IPAddress(ip[0], ip[1], ip[2], ip[3]);
  out_port = port;
  return true;
}

static bool parse_stun_response(const uint8_t *resp,
                                size_t resp_len,
                                const uint8_t txid[12],
                                IPAddress &out_ip,
                                uint16_t &out_port)
{
  size_t offset = 20U;

  if (resp == NULL || txid == NULL || resp_len < 20U) {
    return false;
  }

  if (read_u16(resp) != STUN_BINDING_SUCCESS ||
      read_u32(resp + 4) != STUN_MAGIC_COOKIE ||
      memcmp(resp + 8, txid, 12U) != 0) {
    return false;
  }

  while ((offset + 4U) <= resp_len) {
    uint16_t attr_type = read_u16(resp + offset);
    uint16_t attr_len = read_u16(resp + offset + 2U);
    const uint8_t *attr_data = resp + offset + 4U;
    size_t padded_len = (size_t)((attr_len + 3U) & ~3U);

    if ((offset + 4U + padded_len) > resp_len) {
      return false;
    }

    if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS &&
        parse_address_attr(attr_data, attr_len, out_ip, out_port, true)) {
      return true;
    }

    if (attr_type == STUN_ATTR_MAPPED_ADDRESS &&
        parse_address_attr(attr_data, attr_len, out_ip, out_port, false)) {
      return true;
    }

    offset += 4U + padded_len;
  }

  return false;
}

static void print_help()
{
  Serial.println("Commands:");
  Serial.println("  help");
  Serial.println("  status");
  Serial.println("  probe");
}

static void print_status()
{
  Serial.printf("STUN_PROBE|event=status|ssid=%s|host=%s|port=%u|local_port=%u|wifi=%s|udp=%u|last_ok=%u\r\n",
                WIFI_SSID,
                STUN_HOST,
                (unsigned)STUN_PORT,
                (unsigned)LOCAL_UDP_PORT,
                WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                g_udp_ready ? 1U : 0U,
                g_last_probe_ok ? 1U : 0U);

  if (g_last_probe_ok) {
    Serial.printf("STUN_PROBE|event=last_result|mapped=%u.%u.%u.%u:%u\r\n",
                  (unsigned)g_last_mapped_ip[0],
                  (unsigned)g_last_mapped_ip[1],
                  (unsigned)g_last_mapped_ip[2],
                  (unsigned)g_last_mapped_ip[3],
                  (unsigned)g_last_mapped_port);
  }
}

static bool wifi_connect()
{
  uint32_t started = millis();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("STUN_PROBE|event=wifi_connect|ssid=%s\r\n", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - started) > 20000UL) {
      Serial.println("STUN_PROBE|event=wifi_fail");
      return false;
    }
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  Serial.printf("STUN_PROBE|event=wifi_ok|ip=%s\r\n", WiFi.localIP().toString().c_str());
  return true;
}

static bool udp_init()
{
  if (g_udp.begin(LOCAL_UDP_PORT) == 1) {
    g_udp_ready = true;
    Serial.printf("STUN_PROBE|event=udp_ok|local_port=%u\r\n", (unsigned)LOCAL_UDP_PORT);
    return true;
  }

  g_udp_ready = false;
  Serial.printf("STUN_PROBE|event=udp_fail|local_port=%u\r\n", (unsigned)LOCAL_UDP_PORT);
  return false;
}

/*
 * This is the Arduino equivalent of the ESP-IDF probe:
 * resolve the STUN hostname, send a binding request, and print the mapped public endpoint.
 */
static void run_probe()
{
  IPAddress stun_ip;
  uint8_t request[20];
  uint8_t response[512];
  uint8_t txid[12];
  int packet_len;
  uint32_t started;
  IPAddress mapped_ip;
  uint16_t mapped_port = 0U;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("STUN_PROBE|event=skip|reason=wifi_not_connected");
    g_last_probe_ok = false;
    return;
  }

  if (!g_udp_ready) {
    Serial.println("STUN_PROBE|event=skip|reason=udp_not_ready");
    g_last_probe_ok = false;
    return;
  }

  if (STUN_HOST == NULL || STUN_HOST[0] == '\0') {
    Serial.println("STUN_PROBE|event=skip|reason=stun_disabled");
    g_last_probe_ok = false;
    return;
  }

  if (!WiFi.hostByName(STUN_HOST, stun_ip)) {
    Serial.printf("STUN_PROBE|event=dns_fail|host=%s\r\n", STUN_HOST);
    g_last_probe_ok = false;
    return;
  }

  for (size_t i = 0; i < sizeof(txid); ++i) {
    txid[i] = (uint8_t)random(0, 256);
  }

  memset(request, 0, sizeof(request));
  write_u16(request, STUN_BINDING_REQUEST);
  write_u16(request + 2, 0U);
  write_u32(request + 4, STUN_MAGIC_COOKIE);
  memcpy(request + 8, txid, sizeof(txid));

  g_udp.flush();
  g_udp.beginPacket(stun_ip, STUN_PORT);
  g_udp.write(request, sizeof(request));
  g_udp.endPacket();

  Serial.printf("STUN_PROBE|event=probe_sent|server=%s:%u|resolved=%s\r\n",
                STUN_HOST,
                (unsigned)STUN_PORT,
                stun_ip.toString().c_str());

  started = millis();
  while ((millis() - started) < 3000UL) {
    packet_len = g_udp.parsePacket();
    if (packet_len <= 0) {
      delay(20);
      continue;
    }

    if ((size_t)packet_len > sizeof(response)) {
      while (g_udp.available() > 0) {
        (void)g_udp.read();
      }
      Serial.printf("STUN_PROBE|event=fail|reason=packet_too_large|len=%d\r\n", packet_len);
      g_last_probe_ok = false;
      return;
    }

    int read_len = g_udp.read(response, packet_len);
    if (read_len != packet_len) {
      Serial.println("STUN_PROBE|event=fail|reason=short_read");
      g_last_probe_ok = false;
      return;
    }

    if (parse_stun_response(response, (size_t)read_len, txid, mapped_ip, mapped_port)) {
      g_last_probe_ok = true;
      g_last_mapped_ip = mapped_ip;
      g_last_mapped_port = mapped_port;
      Serial.printf("STUN_PROBE|event=ok|server=%s:%u|mapped=%u.%u.%u.%u:%u\r\n",
                    STUN_HOST,
                    (unsigned)STUN_PORT,
                    (unsigned)mapped_ip[0],
                    (unsigned)mapped_ip[1],
                    (unsigned)mapped_ip[2],
                    (unsigned)mapped_ip[3],
                    (unsigned)mapped_port);
      return;
    }
  }

  g_last_probe_ok = false;
  Serial.printf("STUN_PROBE|event=fail|server=%s:%u|reason=timeout\r\n",
                STUN_HOST,
                (unsigned)STUN_PORT);
}

static void handle_command(const char *line)
{
  if (strcmp(line, "help") == 0) {
    print_help();
    return;
  }
  if (strcmp(line, "status") == 0) {
    print_status();
    return;
  }
  if (strcmp(line, "probe") == 0) {
    run_probe();
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
  Serial.begin(115200);
  delay(1000);

  randomSeed((uint32_t)micros());
  print_help();

  if (!wifi_connect()) {
    return;
  }

  if (!udp_init()) {
    return;
  }

  run_probe();
}

void loop()
{
  poll_serial();

  if (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    return;
  }

  delay(20);
}

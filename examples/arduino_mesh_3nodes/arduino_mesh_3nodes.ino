#include <WiFi.h>
#include <WiFiUdp.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

/*
 * Universal 3-node Arduino UDP mesh demo for ESP32-S3 boards.
 *
 * Flash the same sketch to all three boards.
 * Only change NODE_SLOT on each board:
 *   board 1 -> NODE_SLOT 1
 *   board 2 -> NODE_SLOT 2
 *   board 3 -> NODE_SLOT 3
 *
 * Then set:
 * - secrets.h (local, ignored by git)
 * - NODE1_IP / NODE2_IP / NODE3_IP
 *
 * UART commands:
 * - help
 * - status
 * - peers
 * - hello
 * - pingall
 * - ping <slot>
 * - send <slot> <text>
 * - all <text>
 * - temp <value>
 * - counter
 * - snapshot
 * - whoami
 */

static const uint8_t NODE_SLOT = 1;
#ifndef MNET_WIFI_SSID
#define MNET_WIFI_SSID ""
#endif
#ifndef MNET_WIFI_PASSWORD
#define MNET_WIFI_PASSWORD ""
#endif
#ifndef MNET_NODE1_IP
#define MNET_NODE1_IP "192.168.1.101"
#endif
#ifndef MNET_NODE2_IP
#define MNET_NODE2_IP "192.168.1.102"
#endif
#ifndef MNET_NODE3_IP
#define MNET_NODE3_IP "192.168.1.103"
#endif

static const char *WIFI_SSID = MNET_WIFI_SSID;
static const char *WIFI_PASSWORD = MNET_WIFI_PASSWORD;
static const uint16_t UDP_PORT = 33333;

static const bool USE_STATIC_IP = true;
static const char *NETMASK_IP = "255.255.255.0";
static const char *GATEWAY_IP = "192.168.1.1";

static const uint32_t TELEMETRY_PERIOD_MS = 2000UL;
static const uint32_t HELLO_PERIOD_MS = 10000UL;
static const uint32_t OFFLINE_TIMEOUT_MS = TELEMETRY_PERIOD_MS * 3UL;

struct PeerState {
  uint8_t slot;
  IPAddress ip;
  bool configured;
  bool online;
  uint32_t lastSeenMs;
  uint32_t txCount;
  uint32_t rxCount;
  uint32_t pingCount;
  uint32_t lastCounter;
  int lastTempCenti;
  char lastText[96];
};

struct DemoVar {
  const char *key;
  char value[96];
};

static WiFiUDP g_udp;
static bool g_udpReady = false;
static PeerState g_peers[3];
static char g_nodeName[8];
static uint8_t g_nodeId[32];
static uint8_t g_groupKey[16];
static uint8_t g_groupHash[16];
static uint32_t g_localCounter = 0UL;
static uint32_t g_localSequence = 0UL;
static int g_localTempCenti = 2150;
static DemoVar g_vars[4];
static size_t g_varCount = 0U;
static uint32_t g_lastTelemetryMs = 0UL;
static uint32_t g_lastHelloMs = 0UL;
static char g_serialLine[160];
static size_t g_serialLen = 0U;

static PeerState *peerBySlot(uint8_t slot)
{
  if (slot < 1U || slot > 3U) {
    return NULL;
  }
  return &g_peers[slot - 1U];
}

static const char *configuredIpForSlot(uint8_t slot)
{
  switch (slot) {
    case 1: return MNET_NODE1_IP;
    case 2: return MNET_NODE2_IP;
    case 3: return MNET_NODE3_IP;
    default: return "";
  }
}

static bool parseIp(const char *text, IPAddress &outIp)
{
  uint8_t a, b, c, d;
  if (sscanf(text, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
    return false;
  }
  outIp = IPAddress(a, b, c, d);
  return true;
}

static bool configuredLocalIp(IPAddress &outIp)
{
  return parseIp(configuredIpForSlot(NODE_SLOT), outIp);
}

static void fillHex(char *dst, size_t dstLen, const uint8_t *src, size_t srcLen)
{
  static const char hex[] = "0123456789abcdef";
  size_t i;

  if (dst == NULL || dstLen == 0U) {
    return;
  }

  if (src == NULL || (dstLen < ((srcLen * 2U) + 1U))) {
    dst[0] = '\0';
    return;
  }

  for (i = 0U; i < srcLen; ++i) {
    dst[i * 2U] = hex[(src[i] >> 4) & 0x0FU];
    dst[(i * 2U) + 1U] = hex[src[i] & 0x0FU];
  }
  dst[srcLen * 2U] = '\0';
}

static uint32_t mixSeed(uint32_t x)
{
  x ^= x >> 16;
  x *= 0x7feb352dUL;
  x ^= x >> 15;
  x *= 0x846ca68bUL;
  x ^= x >> 16;
  return x;
}

/*
 * These IDs are deterministic demo identities, not production crypto keys.
 * They let you inspect stable node/group values from one .ino without vendoring the full library.
 */
static void initDemoIdentity()
{
  uint64_t mac = ESP.getEfuseMac();
  uint32_t seed = (uint32_t)(mac ^ (mac >> 32) ^ NODE_SLOT ^ 0x4d4e4554UL);
  size_t i;

  for (i = 0U; i < sizeof(g_nodeId); ++i) {
    seed = mixSeed(seed + (uint32_t)i + 0x9e3779b9UL);
    g_nodeId[i] = (uint8_t)(seed & 0xFFU);
  }

  seed = 0x47525031UL;
  for (i = 0U; i < sizeof(g_groupKey); ++i) {
    seed = mixSeed(seed + (uint32_t)"micronet-demo"[i % 13U] + (uint32_t)i);
    g_groupKey[i] = (uint8_t)(seed & 0xFFU);
  }

  seed = 0x48415348UL;
  for (i = 0U; i < sizeof(g_groupHash); ++i) {
    seed = mixSeed(seed + g_groupKey[i % sizeof(g_groupKey)] + (uint32_t)i);
    g_groupHash[i] = (uint8_t)(seed & 0xFFU);
  }
}

static DemoVar *findVar(const char *key)
{
  size_t i;

  if (key == NULL) {
    return NULL;
  }
  for (i = 0U; i < g_varCount; ++i) {
    if (strcmp(g_vars[i].key, key) == 0) {
      return &g_vars[i];
    }
  }
  return NULL;
}

static void setVarValue(const char *key, const char *value)
{
  DemoVar *var = findVar(key);

  if (var == NULL || value == NULL) {
    return;
  }
  snprintf(var->value, sizeof(var->value), "%s", value);
}

static void refreshLocalVars()
{
  char tempText[24];
  char counterText[24];

  snprintf(tempText, sizeof(tempText), "%.2f", (double)g_localTempCenti / 100.0);
  snprintf(counterText, sizeof(counterText), "%lu", (unsigned long)g_localCounter);
  setVarValue("temperature_c", tempText);
  setVarValue("counter", counterText);
}

static void initVars()
{
  g_varCount = 0U;
  g_vars[g_varCount++] = {"temperature_c", ""};
  g_vars[g_varCount++] = {"counter", ""};
  g_vars[g_varCount++] = {"last_text", ""};
  g_vars[g_varCount++] = {"node_name", ""};
  setVarValue("node_name", g_nodeName);
  setVarValue("last_text", "");
  refreshLocalVars();
}

static void logSimple(const char *event)
{
  Serial.printf("MNET_DEMO|node=%u|event=%s\r\n", (unsigned)NODE_SLOT, event);
}

static void logTx(uint8_t peerSlot, const char *kind, const char *detail)
{
  if (detail != NULL && detail[0] != '\0') {
    Serial.printf("MNET_DEMO|node=%u|event=tx|peer=%u|kind=%s|%s\r\n",
                  (unsigned)NODE_SLOT,
                  (unsigned)peerSlot,
                  kind,
                  detail);
  } else {
    Serial.printf("MNET_DEMO|node=%u|event=tx|peer=%u|kind=%s\r\n",
                  (unsigned)NODE_SLOT,
                  (unsigned)peerSlot,
                  kind);
  }
}

static void logRx(uint8_t peerSlot, const char *kind, const char *detail)
{
  if (detail != NULL && detail[0] != '\0') {
    Serial.printf("MNET_DEMO|node=%u|event=rx|peer=%u|kind=%s|%s\r\n",
                  (unsigned)NODE_SLOT,
                  (unsigned)peerSlot,
                  kind,
                  detail);
  } else {
    Serial.printf("MNET_DEMO|node=%u|event=rx|peer=%u|kind=%s\r\n",
                  (unsigned)NODE_SLOT,
                  (unsigned)peerSlot,
                  kind);
  }
}

/* Human-readable payloads make debugging easier across three serial consoles. */
static bool sendPayload(uint8_t peerSlot, const char *kind, const char *payload)
{
  PeerState *peer = peerBySlot(peerSlot);

  if (peer == NULL || !peer->configured || payload == NULL) {
    return false;
  }

  g_udp.beginPacket(peer->ip, UDP_PORT);
  g_udp.write((const uint8_t *)payload, strlen(payload));
  bool ok = g_udp.endPacket() == 1;
  if (!ok) {
    Serial.printf("MNET_DEMO|node=%u|event=error|peer=%u|kind=%s|detail=send_failed\r\n",
                  (unsigned)NODE_SLOT,
                  (unsigned)peerSlot,
                  kind);
    return false;
  }

  peer->txCount++;
  logTx(peerSlot, kind, payload);
  return true;
}

static void sendToAll(const char *kind, const char *payload)
{
  for (uint8_t slot = 1U; slot <= 3U; ++slot) {
    if (slot == NODE_SLOT) {
      continue;
    }
    (void)sendPayload(slot, kind, payload);
  }
}

static void sendHello()
{
  char line[160];
  snprintf(line, sizeof(line), "HELLO|%u|%s", (unsigned)NODE_SLOT, g_nodeName);
  sendToAll("hello", line);
}

static void sendPing(uint8_t peerSlot)
{
  char line[96];
  snprintf(line, sizeof(line), "PING|%u|%lu", (unsigned)NODE_SLOT, (unsigned long)++g_localSequence);
  (void)sendPayload(peerSlot, "ping", line);
}

static void sendText(uint8_t peerSlot, const char *text)
{
  char line[160];
  snprintf(line,
           sizeof(line),
           "TEXT|%u|%lu|%s",
           (unsigned)NODE_SLOT,
           (unsigned long)++g_localSequence,
           text != NULL ? text : "");
  if (peerSlot == 0U) {
    sendToAll("text", line);
  } else {
    (void)sendPayload(peerSlot, "text", line);
  }
  setVarValue("last_text", text != NULL ? text : "");
}

static void sendVarRequest(uint8_t peerSlot, const char *key)
{
  char line[160];

  if (key == NULL) {
    return;
  }
  snprintf(line,
           sizeof(line),
           "REQ|%u|%lu|%s",
           (unsigned)NODE_SLOT,
           (unsigned long)++g_localSequence,
           key);
  (void)sendPayload(peerSlot, "request", line);
}

static void sendListRequest(uint8_t peerSlot)
{
  char line[96];

  snprintf(line, sizeof(line), "LIST|%u|%lu", (unsigned)NODE_SLOT, (unsigned long)++g_localSequence);
  (void)sendPayload(peerSlot, "list", line);
}

static void sendMetricsRequest(uint8_t peerSlot)
{
  char line[96];

  snprintf(line, sizeof(line), "METRICS|%u|%lu", (unsigned)NODE_SLOT, (unsigned long)++g_localSequence);
  (void)sendPayload(peerSlot, "metrics_req", line);
}

static void sendTelemetry()
{
  char line[160];
  g_localCounter++;
  refreshLocalVars();
  snprintf(line,
           sizeof(line),
           "TELEM|%u|%lu|%lu|%d|%lu",
           (unsigned)NODE_SLOT,
           (unsigned long)++g_localSequence,
           (unsigned long)g_localCounter,
           g_localTempCenti,
           (unsigned long)millis());
  sendToAll("telemetry", line);
}

static void printHelp()
{
  Serial.println("Commands:");
  Serial.println("  help");
  Serial.println("  status");
  Serial.println("  identity");
  Serial.println("  group");
  Serial.println("  stun");
  Serial.println("  vars");
  Serial.println("  metrics");
  Serial.println("  request <slot> <key>");
  Serial.println("  list <slot>");
  Serial.println("  metricsreq <slot>");
  Serial.println("  peers");
  Serial.println("  hello");
  Serial.println("  pingall");
  Serial.println("  ping <slot>");
  Serial.println("  send <slot> <text>");
  Serial.println("  all <text>");
  Serial.println("  temp <value>");
  Serial.println("  counter");
  Serial.println("  snapshot");
  Serial.println("  whoami");
}

static void printIdentity()
{
  char nodeIdHex[65];

  fillHex(nodeIdHex, sizeof(nodeIdHex), g_nodeId, sizeof(g_nodeId));
  Serial.printf("MNET_DEMO|node=%u|event=identity|node_id=%s\r\n",
                (unsigned)NODE_SLOT,
                nodeIdHex);
}

static void printGroup()
{
  char groupKeyHex[33];
  char groupHashHex[33];

  fillHex(groupKeyHex, sizeof(groupKeyHex), g_groupKey, sizeof(g_groupKey));
  fillHex(groupHashHex, sizeof(groupHashHex), g_groupHash, sizeof(g_groupHash));
  Serial.printf("MNET_DEMO|node=%u|event=group|group_hash=%s|group_key=%s\r\n",
                (unsigned)NODE_SLOT,
                groupHashHex,
                groupKeyHex);
}

static void printStatus()
{
  Serial.printf("Node %u (%s)\r\n", (unsigned)NODE_SLOT, g_nodeName);
  Serial.printf("  wifi=%s local_ip=%s udp=%u port=%u\r\n",
                WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                WiFi.localIP().toString().c_str(),
                g_udpReady ? 1U : 0U,
                (unsigned)UDP_PORT);
  Serial.printf("  local_counter=%lu local_temp=%.2f C\r\n",
                (unsigned long)g_localCounter,
                (double)g_localTempCenti / 100.0);

  for (uint8_t slot = 1U; slot <= 3U; ++slot) {
    PeerState *peer = peerBySlot(slot);
    if (peer == NULL) {
      continue;
    }
    Serial.printf("  peer%u ip=%s online=%s tx=%lu rx=%lu last_counter=%lu last_temp=%.2fC\r\n",
                  (unsigned)peer->slot,
                  peer->ip.toString().c_str(),
                  peer->online ? "yes" : "no",
                  (unsigned long)peer->txCount,
                  (unsigned long)peer->rxCount,
                  (unsigned long)peer->lastCounter,
                  (double)peer->lastTempCenti / 100.0);
  }
}

static void printVars()
{
  size_t i;

  refreshLocalVars();
  for (i = 0U; i < g_varCount; ++i) {
    Serial.printf("MNET_DEMO|node=%u|event=var|key=%s|value=%s\r\n",
                  (unsigned)NODE_SLOT,
                  g_vars[i].key,
                  g_vars[i].value);
  }
}

static void printMetrics()
{
  uint8_t onlinePeers = 0U;
  uint32_t txTotal = 0U;
  uint32_t rxTotal = 0U;
  uint8_t slot;

  for (slot = 1U; slot <= 3U; ++slot) {
    PeerState *peer = peerBySlot(slot);
    if (peer == NULL) {
      continue;
    }
    if (peer->online) {
      onlinePeers++;
    }
    txTotal += peer->txCount;
    rxTotal += peer->rxCount;
  }

  Serial.printf("MNET_DEMO|node=%u|event=metrics|uptime_s=%lu|connected_nodes=%u|packets_sent=%lu|packets_recv=%lu|health_score=%u\r\n",
                (unsigned)NODE_SLOT,
                (unsigned long)(millis() / 1000UL),
                (unsigned)onlinePeers,
                (unsigned long)txTotal,
                (unsigned long)rxTotal,
                100U);
}

static void printSnapshot()
{
  for (uint8_t slot = 1U; slot <= 3U; ++slot) {
    PeerState *peer = peerBySlot(slot);
    if (peer == NULL) {
      continue;
    }
    Serial.printf("MNET_DEMO|node=%u|event=snapshot|peer=%u|online=%u|tx=%lu|rx=%lu|last_counter=%lu|last_temp=%d\r\n",
                  (unsigned)NODE_SLOT,
                  (unsigned)peer->slot,
                  peer->online ? 1U : 0U,
                  (unsigned long)peer->txCount,
                  (unsigned long)peer->rxCount,
                  (unsigned long)peer->lastCounter,
                  peer->lastTempCenti);
  }
}

static uint16_t stunReadU16(const uint8_t *src)
{
  return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

static uint32_t stunReadU32(const uint8_t *src)
{
  return ((uint32_t)src[0] << 24) |
         ((uint32_t)src[1] << 16) |
         ((uint32_t)src[2] << 8) |
         (uint32_t)src[3];
}

static void stunWriteU16(uint8_t *dst, uint16_t value)
{
  dst[0] = (uint8_t)((value >> 8) & 0xFFU);
  dst[1] = (uint8_t)(value & 0xFFU);
}

static void stunWriteU32(uint8_t *dst, uint32_t value)
{
  dst[0] = (uint8_t)((value >> 24) & 0xFFU);
  dst[1] = (uint8_t)((value >> 16) & 0xFFU);
  dst[2] = (uint8_t)((value >> 8) & 0xFFU);
  dst[3] = (uint8_t)(value & 0xFFU);
}

static bool stunParseAddress(const uint8_t *attr,
                             size_t attrLen,
                             IPAddress &outIp,
                             uint16_t &outPort,
                             bool isXor)
{
  uint8_t ip[4];
  uint16_t port;

  if (attr == NULL || attrLen < 8U || attr[1] != 0x01U) {
    return false;
  }

  port = stunReadU16(&attr[2]);
  memcpy(ip, &attr[4], sizeof(ip));
  if (isXor) {
    port ^= (uint16_t)(0x2112A442UL >> 16);
    ip[0] ^= 0x21U;
    ip[1] ^= 0x12U;
    ip[2] ^= 0xA4U;
    ip[3] ^= 0x42U;
  }

  outIp = IPAddress(ip[0], ip[1], ip[2], ip[3]);
  outPort = port;
  return true;
}

static bool stunParseResponse(const uint8_t *resp,
                              size_t respLen,
                              const uint8_t txid[12],
                              IPAddress &outIp,
                              uint16_t &outPort)
{
  size_t offset = 20U;

  if (resp == NULL || txid == NULL || respLen < 20U) {
    return false;
  }

  if (stunReadU16(resp) != 0x0101U ||
      stunReadU32(resp + 4) != 0x2112A442UL ||
      memcmp(resp + 8, txid, 12U) != 0) {
    return false;
  }

  while ((offset + 4U) <= respLen) {
    uint16_t attrType = stunReadU16(resp + offset);
    uint16_t attrLen = stunReadU16(resp + offset + 2U);
    const uint8_t *attrData = resp + offset + 4U;
    size_t paddedLen = (size_t)((attrLen + 3U) & ~3U);

    if ((offset + 4U + paddedLen) > respLen) {
      return false;
    }

    if (attrType == 0x0020U && stunParseAddress(attrData, attrLen, outIp, outPort, true)) {
      return true;
    }
    if (attrType == 0x0001U && stunParseAddress(attrData, attrLen, outIp, outPort, false)) {
      return true;
    }

    offset += 4U + paddedLen;
  }

  return false;
}

static void runStunProbe()
{
  WiFiUDP probeUdp;
  IPAddress stunIp;
  IPAddress mappedIp;
  uint16_t mappedPort = 0U;
  uint8_t request[20];
  uint8_t response[512];
  uint8_t txid[12];
  uint32_t started;
  int packetLen;
  size_t i;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("MNET_DEMO|node=%u|event=stun_fail|detail=wifi_not_connected\r\n", (unsigned)NODE_SLOT);
    return;
  }
  if (!WiFi.hostByName(STUN_HOST, stunIp)) {
    Serial.printf("MNET_DEMO|node=%u|event=stun_fail|detail=dns_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }
  if (probeUdp.begin((uint16_t)(STUN_LOCAL_PORT + NODE_SLOT)) != 1) {
    Serial.printf("MNET_DEMO|node=%u|event=stun_fail|detail=udp_bind_fail\r\n", (unsigned)NODE_SLOT);
    return;
  }

  for (i = 0U; i < sizeof(txid); ++i) {
    txid[i] = (uint8_t)random(0, 256);
  }

  memset(request, 0, sizeof(request));
  stunWriteU16(request, 0x0001U);
  stunWriteU16(request + 2, 0U);
  stunWriteU32(request + 4, 0x2112A442UL);
  memcpy(request + 8, txid, sizeof(txid));

  probeUdp.beginPacket(stunIp, STUN_PORT);
  probeUdp.write(request, sizeof(request));
  probeUdp.endPacket();

  started = millis();
  while ((millis() - started) < 3000UL) {
    packetLen = probeUdp.parsePacket();
    if (packetLen <= 0) {
      delay(20);
      continue;
    }

    if ((size_t)packetLen > sizeof(response)) {
      while (probeUdp.available() > 0) {
        (void)probeUdp.read();
      }
      break;
    }

    packetLen = probeUdp.read(response, packetLen);
    if (packetLen > 0 &&
        stunParseResponse(response, (size_t)packetLen, txid, mappedIp, mappedPort)) {
      Serial.printf("MNET_DEMO|node=%u|event=stun_ok|server=%s:%u|mapped=%s:%u\r\n",
                    (unsigned)NODE_SLOT,
                    STUN_HOST,
                    (unsigned)STUN_PORT,
                    mappedIp.toString().c_str(),
                    (unsigned)mappedPort);
      probeUdp.stop();
      return;
    }
  }

  probeUdp.stop();
  Serial.printf("MNET_DEMO|node=%u|event=stun_fail|server=%s:%u|detail=timeout\r\n",
                (unsigned)NODE_SLOT,
                STUN_HOST,
                (unsigned)STUN_PORT);
}

static void markPeerSeen(PeerState *peer)
{
  if (peer == NULL) {
    return;
  }
  peer->online = true;
  peer->lastSeenMs = millis();
}

static void handlePing(PeerState *peer)
{
  char line[96];
  if (peer == NULL) {
    return;
  }
  peer->pingCount++;
  snprintf(line, sizeof(line), "PONG|%u|%lu", (unsigned)NODE_SLOT, (unsigned long)millis());
  (void)sendPayload(peer->slot, "pong", line);
}

static void processPayload(const char *payload, const IPAddress &remoteIp)
{
  char buffer[160];
  char *saveptr = NULL;
  char *type;
  char *slotText;
  uint8_t sourceSlot;
  PeerState *peer;
  char detail[128];

  if (payload == NULL) {
    return;
  }

  snprintf(buffer, sizeof(buffer), "%s", payload);
  type = strtok_r(buffer, "|", &saveptr);
  slotText = strtok_r(NULL, "|", &saveptr);
  if (type == NULL || slotText == NULL) {
    logSimple("bad_payload");
    return;
  }

  sourceSlot = (uint8_t)strtoul(slotText, NULL, 10);
  peer = peerBySlot(sourceSlot);
  if (peer == NULL) {
    logSimple("unknown_peer_slot");
    return;
  }

  markPeerSeen(peer);
  peer->rxCount++;

  if (strcmp(type, "HELLO") == 0) {
    snprintf(detail, sizeof(detail), "ip=%s", remoteIp.toString().c_str());
    logRx(peer->slot, "hello", detail);
    return;
  }

  if (strcmp(type, "PING") == 0) {
    snprintf(detail, sizeof(detail), "ip=%s", remoteIp.toString().c_str());
    logRx(peer->slot, "ping", detail);
    handlePing(peer);
    return;
  }

  if (strcmp(type, "PONG") == 0) {
    snprintf(detail, sizeof(detail), "ip=%s", remoteIp.toString().c_str());
    logRx(peer->slot, "pong", detail);
    return;
  }

  if (strcmp(type, "TEXT") == 0) {
    (void)strtok_r(NULL, "|", &saveptr);
    const char *message = saveptr != NULL ? saveptr : "";
    snprintf(peer->lastText, sizeof(peer->lastText), "%s", message);
    snprintf(detail, sizeof(detail), "ip=%s|text=%s", remoteIp.toString().c_str(), message);
    logRx(peer->slot, "text", detail);
    return;
  }

  if (strcmp(type, "REQ") == 0) {
    char *seqText = strtok_r(NULL, "|", &saveptr);
    char *key = saveptr;
    DemoVar *var = key != NULL ? findVar(key) : NULL;
    char line[192];

    if (seqText == NULL || key == NULL) {
      logSimple("bad_request");
      return;
    }
    if (var != NULL) {
      snprintf(line, sizeof(line), "RESP|%u|%s|%s", (unsigned)NODE_SLOT, key, var->value);
    } else {
      snprintf(line, sizeof(line), "RESP|%u|%s|NOT_FOUND", (unsigned)NODE_SLOT, key);
    }
    (void)sendPayload(peer->slot, "response", line);
    snprintf(detail, sizeof(detail), "key=%s", key);
    logRx(peer->slot, "request", detail);
    return;
  }

  if (strcmp(type, "RESP") == 0) {
    char *key = strtok_r(NULL, "|", &saveptr);
    const char *value = saveptr != NULL ? saveptr : "";

    snprintf(detail, sizeof(detail), "key=%s|value=%s", key != NULL ? key : "", value);
    logRx(peer->slot, "response", detail);
    return;
  }

  if (strcmp(type, "LIST") == 0) {
    char line[256];
    size_t pos = 0U;
    size_t i;

    pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "LISTRESP|%u|", (unsigned)NODE_SLOT);
    for (i = 0U; i < g_varCount && pos < sizeof(line); ++i) {
      pos += (size_t)snprintf(line + pos,
                              sizeof(line) - pos,
                              "%s%s",
                              i == 0U ? "" : ",",
                              g_vars[i].key);
    }
    (void)sendPayload(peer->slot, "list_resp", line);
    logRx(peer->slot, "list", "");
    return;
  }

  if (strcmp(type, "LISTRESP") == 0) {
    const char *names = saveptr != NULL ? saveptr : "";
    snprintf(detail, sizeof(detail), "names=%s", names);
    logRx(peer->slot, "list_resp", detail);
    return;
  }

  if (strcmp(type, "METRICS") == 0) {
    uint8_t onlinePeers = 0U;
    uint32_t txTotal = 0U;
    uint32_t rxTotal = 0U;
    char line[192];
    uint8_t slot;

    for (slot = 1U; slot <= 3U; ++slot) {
      PeerState *candidate = peerBySlot(slot);
      if (candidate == NULL) {
        continue;
      }
      if (candidate->online) {
        onlinePeers++;
      }
      txTotal += candidate->txCount;
      rxTotal += candidate->rxCount;
    }
    snprintf(line,
             sizeof(line),
             "METRICSRESP|%u|uptime=%lu|nodes=%u|tx=%lu|rx=%lu|health=100",
             (unsigned)NODE_SLOT,
             (unsigned long)(millis() / 1000UL),
             (unsigned)onlinePeers,
             (unsigned long)txTotal,
             (unsigned long)rxTotal);
    (void)sendPayload(peer->slot, "metrics_resp", line);
    logRx(peer->slot, "metrics_req", "");
    return;
  }

  if (strcmp(type, "METRICSRESP") == 0) {
    const char *metrics = saveptr != NULL ? saveptr : "";
    snprintf(detail, sizeof(detail), "%s", metrics);
    logRx(peer->slot, "metrics_resp", detail);
    return;
  }

  if (strcmp(type, "TELEM") == 0) {
    (void)strtok_r(NULL, "|", &saveptr);
    char *counterText = strtok_r(NULL, "|", &saveptr);
    char *tempText = strtok_r(NULL, "|", &saveptr);

    if (counterText != NULL) {
      peer->lastCounter = (uint32_t)strtoul(counterText, NULL, 10);
    }
    if (tempText != NULL) {
      peer->lastTempCenti = (int)strtol(tempText, NULL, 10);
    }
    snprintf(detail,
             sizeof(detail),
             "counter=%lu|temp=%d",
             (unsigned long)peer->lastCounter,
             peer->lastTempCenti);
    logRx(peer->slot, "telemetry", detail);
    return;
  }

  snprintf(detail, sizeof(detail), "ip=%s", remoteIp.toString().c_str());
  logRx(peer->slot, "unknown", detail);
}

/* Drain every UDP packet currently queued so the serial console stays responsive. */
static void pollUdp()
{
  int packetLen;
  char packet[160];
  IPAddress remoteIp;

  for (;;) {
    packetLen = g_udp.parsePacket();
    if (packetLen <= 0) {
      return;
    }

    if (packetLen >= (int)sizeof(packet)) {
      while (g_udp.available() > 0) {
        (void)g_udp.read();
      }
      logSimple("packet_too_large");
      continue;
    }

    remoteIp = g_udp.remoteIP();
    int readLen = g_udp.read((uint8_t *)packet, packetLen);
    if (readLen <= 0) {
      logSimple("packet_read_fail");
      continue;
    }
    packet[readLen] = '\0';
    processPayload(packet, remoteIp);
  }
}

static void handleCommand(const char *line)
{
  if (strcmp(line, "help") == 0) {
    printHelp();
    return;
  }
  if (strcmp(line, "status") == 0 || strcmp(line, "peers") == 0) {
    printStatus();
    return;
  }
  if (strcmp(line, "vars") == 0) {
    printVars();
    return;
  }
  if (strcmp(line, "metrics") == 0) {
    printMetrics();
    return;
  }
  if (strcmp(line, "identity") == 0) {
    printIdentity();
    return;
  }
  if (strcmp(line, "group") == 0) {
    printGroup();
    return;
  }
  if (strcmp(line, "stun") == 0) {
    runStunProbe();
    return;
  }
  if (strcmp(line, "hello") == 0) {
    sendHello();
    return;
  }
  if (strcmp(line, "pingall") == 0) {
    for (uint8_t slot = 1U; slot <= 3U; ++slot) {
      if (slot != NODE_SLOT) {
        sendPing(slot);
      }
    }
    return;
  }
  if (strcmp(line, "counter") == 0) {
    g_localCounter++;
    refreshLocalVars();
    logSimple("counter_incremented");
    return;
  }
  if (strcmp(line, "snapshot") == 0) {
    printSnapshot();
    return;
  }
  if (strcmp(line, "whoami") == 0) {
    Serial.printf("node=%u name=%s\r\n", (unsigned)NODE_SLOT, g_nodeName);
    return;
  }
  if (strncmp(line, "ping ", 5) == 0) {
    uint8_t slot = (uint8_t)strtoul(line + 5, NULL, 10);
    sendPing(slot);
    return;
  }
  if (strncmp(line, "request ", 8) == 0) {
    char *key = strchr(line + 8, ' ');
    uint8_t slot = (uint8_t)strtoul(line + 8, NULL, 10);
    if (key != NULL) {
      while (*key == ' ') {
        key++;
      }
      sendVarRequest(slot, key);
    }
    return;
  }
  if (strncmp(line, "list ", 5) == 0) {
    uint8_t slot = (uint8_t)strtoul(line + 5, NULL, 10);
    sendListRequest(slot);
    return;
  }
  if (strncmp(line, "metricsreq ", 11) == 0) {
    uint8_t slot = (uint8_t)strtoul(line + 11, NULL, 10);
    sendMetricsRequest(slot);
    return;
  }
  if (strncmp(line, "send ", 5) == 0) {
    char *message = strchr(line + 5, ' ');
    uint8_t slot = (uint8_t)strtoul(line + 5, NULL, 10);
    if (message != NULL) {
      while (*message == ' ') {
        message++;
      }
      sendText(slot, message);
    }
    return;
  }
  if (strncmp(line, "all ", 4) == 0) {
    sendText(0U, line + 4);
    return;
  }
  if (strncmp(line, "temp ", 5) == 0) {
    double value = strtod(line + 5, NULL);
    g_localTempCenti = (int)(value * 100.0);
    refreshLocalVars();
    logSimple("temperature_updated");
    return;
  }

  Serial.println("Unknown command. Type 'help'.");
}

static void pollSerial()
{
  while (Serial.available() > 0) {
    int ch = Serial.read();

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      g_serialLine[g_serialLen] = '\0';
      if (g_serialLen > 0U) {
        handleCommand(g_serialLine);
      }
      g_serialLen = 0U;
      continue;
    }

    if (g_serialLen + 1U < sizeof(g_serialLine)) {
      g_serialLine[g_serialLen++] = (char)ch;
    }
  }
}

static bool connectWifi()
{
  uint32_t started = millis();
  IPAddress localIp;
  IPAddress netmask;
  IPAddress gateway;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (USE_STATIC_IP) {
    if (!configuredLocalIp(localIp) ||
        !parseIp(NETMASK_IP, netmask) ||
        !parseIp(GATEWAY_IP, gateway)) {
      Serial.printf("MNET_DEMO|node=%u|event=wifi_fail|detail=bad_static_ip_config\r\n",
                    (unsigned)NODE_SLOT);
      return false;
    }

    if (!WiFi.config(localIp, gateway, netmask)) {
      Serial.printf("MNET_DEMO|node=%u|event=wifi_fail|detail=wifi_config_failed\r\n",
                    (unsigned)NODE_SLOT);
      return false;
    }
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("MNET_DEMO|node=%u|event=wifi_connect|ssid=%s\r\n",
                (unsigned)NODE_SLOT,
                WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - started) > 20000UL) {
      Serial.printf("MNET_DEMO|node=%u|event=wifi_fail\r\n", (unsigned)NODE_SLOT);
      return false;
    }
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  Serial.printf("MNET_DEMO|node=%u|event=wifi_ok|ip=%s\r\n",
                (unsigned)NODE_SLOT,
                WiFi.localIP().toString().c_str());
  return true;
}

static bool startUdp()
{
  if (g_udp.begin(UDP_PORT) == 1) {
    g_udpReady = true;
    Serial.printf("MNET_DEMO|node=%u|event=udp_ok|port=%u\r\n",
                  (unsigned)NODE_SLOT,
                  (unsigned)UDP_PORT);
    return true;
  }

  g_udpReady = false;
  Serial.printf("MNET_DEMO|node=%u|event=udp_fail|port=%u\r\n",
                (unsigned)NODE_SLOT,
                (unsigned)UDP_PORT);
  return false;
}

static void initPeers()
{
  for (uint8_t slot = 1U; slot <= 3U; ++slot) {
    PeerState *peer = &g_peers[slot - 1U];

    memset(peer, 0, sizeof(*peer));
    peer->slot = slot;
    peer->configured = parseIp(configuredIpForSlot(slot), peer->ip);
  }
  snprintf(g_nodeName, sizeof(g_nodeName), "node-%u", (unsigned)NODE_SLOT);
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  initPeers();
  initDemoIdentity();
  initVars();
  printHelp();
  printIdentity();
  printGroup();

  if (!connectWifi()) {
    return;
  }
  if (!startUdp()) {
    return;
  }

  logSimple("boot");
  sendHello();
}

void loop()
{
  pollSerial();

  if (WiFi.status() != WL_CONNECTED || !g_udpReady) {
    delay(50);
    return;
  }

  pollUdp();

  uint32_t nowMs = millis();
  for (uint8_t slot = 1U; slot <= 3U; ++slot) {
    PeerState *peer = peerBySlot(slot);
    if (peer != NULL &&
        peer->online &&
        peer->lastSeenMs != 0U &&
        (nowMs - peer->lastSeenMs) > OFFLINE_TIMEOUT_MS) {
      peer->online = false;
      Serial.printf("MNET_DEMO|node=%u|event=offline|peer=%u|kind=peer|detail=telemetry_timeout\r\n",
                    (unsigned)NODE_SLOT,
                    (unsigned)peer->slot);
    }
  }

  if ((nowMs - g_lastHelloMs) >= HELLO_PERIOD_MS) {
    g_lastHelloMs = nowMs;
    sendHello();
  }
  if ((nowMs - g_lastTelemetryMs) >= TELEMETRY_PERIOD_MS) {
    g_lastTelemetryMs = nowMs;
    sendTelemetry();
  }

  delay(20);
}

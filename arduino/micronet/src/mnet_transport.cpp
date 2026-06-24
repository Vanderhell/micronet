#include "mnet_transport.h"

#include <string.h>

namespace {

constexpr uint16_t kStunBindingRequest = 0x0001U;
constexpr uint16_t kStunBindingSuccess = 0x0101U;
constexpr uint32_t kStunMagicCookie = 0x2112A442UL;
constexpr uint16_t kStunAttrMappedAddress = 0x0001U;
constexpr uint16_t kStunAttrXorMappedAddress = 0x0020U;

constexpr uint8_t kPacketMagic[4] = {'M', 'N', 'T', '1'};

}  // namespace

MNetTransport::MNetTransport()
    : ctx_(nullptr), ready_(false), local_port_(0U), stun_port_(19302U)
{
  memset(stun_host_, 0, sizeof(stun_host_));
  memset(peers_, 0, sizeof(peers_));
}

bool MNetTransport::begin(mneta_t *ctx, uint16_t local_port)
{
  if (ctx == nullptr || local_port == 0U) {
    return false;
  }

  end();
  if (udp_.begin(local_port) != 1) {
    return false;
  }

  ctx_ = ctx;
  local_port_ = local_port;
  ready_ = true;
  return true;
}

void MNetTransport::end()
{
  if (ready_) {
    udp_.stop();
  }
  ctx_ = nullptr;
  ready_ = false;
  local_port_ = 0U;
}

void MNetTransport::setStunServer(const char *host, uint16_t port)
{
  if (host != nullptr && host[0] != '\0') {
    strncpy(stun_host_, host, sizeof(stun_host_) - 1U);
    stun_host_[sizeof(stun_host_) - 1U] = '\0';
  }
  if (port != 0U) {
    stun_port_ = port;
  }
}

bool MNetTransport::resolveExternal(IPAddress &out_ip, uint16_t &out_port, uint32_t timeout_ms)
{
  WiFiUDP probe_udp;
  IPAddress stun_ip;
  uint8_t request[20];
  uint8_t response[512];
  uint8_t txid[12];
  uint32_t started;
  int packet_len;

  if (WiFi.status() != WL_CONNECTED || !ready_) {
    return false;
  }
  if (!WiFi.hostByName(stun_host_, stun_ip)) {
    return false;
  }
  if (probe_udp.begin((uint16_t)(local_port_ + 1U)) != 1) {
    return false;
  }

  for (size_t i = 0; i < sizeof(txid); ++i) {
    txid[i] = (uint8_t)random(0, 256);
  }

  memset(request, 0, sizeof(request));
  writeU16(request, kStunBindingRequest);
  writeU16(request + 2U, 0U);
  request[4] = (uint8_t)((kStunMagicCookie >> 24) & 0xFFU);
  request[5] = (uint8_t)((kStunMagicCookie >> 16) & 0xFFU);
  request[6] = (uint8_t)((kStunMagicCookie >> 8) & 0xFFU);
  request[7] = (uint8_t)(kStunMagicCookie & 0xFFU);
  memcpy(request + 8U, txid, sizeof(txid));

  probe_udp.beginPacket(stun_ip, stun_port_);
  probe_udp.write(request, sizeof(request));
  probe_udp.endPacket();

  started = millis();
  while ((millis() - started) < timeout_ms) {
    packet_len = probe_udp.parsePacket();
    if (packet_len <= 0) {
      delay(20);
      continue;
    }
    if ((size_t)packet_len > sizeof(response)) {
      while (probe_udp.available() > 0) {
        (void)probe_udp.read();
      }
      probe_udp.stop();
      return false;
    }

    packet_len = probe_udp.read(response, packet_len);
    if (packet_len > 0 && parseStunResponse(response, (size_t)packet_len, txid, out_ip, out_port)) {
      probe_udp.stop();
      return true;
    }
  }

  probe_udp.stop();
  return false;
}

bool MNetTransport::addPeer(const uint8_t peer_pubkey[32], const IPAddress &ip, uint16_t port)
{
  uint8_t i;

  if (peer_pubkey == nullptr || port == 0U) {
    return false;
  }

  if (updatePeer(peer_pubkey, ip, port)) {
    return true;
  }

  for (i = 0U; i < kMaxPeers; ++i) {
    if (!peers_[i].used) {
      peers_[i].used = true;
      memcpy(peers_[i].pubkey, peer_pubkey, sizeof(peers_[i].pubkey));
      peers_[i].ip = ip;
      peers_[i].port = port;
      return true;
    }
  }

  return false;
}

bool MNetTransport::updatePeer(const uint8_t peer_pubkey[32], const IPAddress &ip, uint16_t port)
{
  int idx = findPeer(peer_pubkey);

  if (idx < 0) {
    return false;
  }

  peers_[idx].ip = ip;
  peers_[idx].port = port;
  return true;
}

bool MNetTransport::sendTo(const uint8_t peer_pubkey[32], const uint8_t *plain, size_t plain_len)
{
  int idx;
  uint8_t frame[kHeaderSize + kMaxCipherLen];
  size_t frame_len = 0U;

  if (!ready_ || ctx_ == nullptr || peer_pubkey == nullptr) {
    return false;
  }

  idx = findPeer(peer_pubkey);
  if (idx < 0) {
    return false;
  }

  if (!buildPeerPacket(peer_pubkey, plain, plain_len, frame, frame_len)) {
    return false;
  }

  udp_.beginPacket(peers_[idx].ip, peers_[idx].port);
  udp_.write(frame, frame_len);
  return udp_.endPacket() == 1;
}

bool MNetTransport::sendToGroup(const uint8_t group_hash[16],
                                const IPAddress &ip,
                                uint16_t port,
                                const uint8_t *plain,
                                size_t plain_len)
{
  uint8_t frame[kHeaderSize + kMaxCipherLen];
  size_t frame_len = 0U;

  if (!ready_ || ctx_ == nullptr || group_hash == nullptr || port == 0U) {
    return false;
  }

  if (!buildGroupPacket(group_hash, plain, plain_len, frame, frame_len)) {
    return false;
  }

  udp_.beginPacket(ip, port);
  udp_.write(frame, frame_len);
  return udp_.endPacket() == 1;
}

bool MNetTransport::tick(MNetTransportPacket &out_packet)
{
  int packet_len;
  uint8_t frame[kHeaderSize + kMaxCipherLen];
  IPAddress remote_ip;
  uint16_t remote_port;

  out_packet = MNetTransportPacket();
  if (!ready_ || ctx_ == nullptr) {
    return false;
  }

  packet_len = udp_.parsePacket();
  if (packet_len <= 0) {
    return false;
  }
  if ((size_t)packet_len > sizeof(frame)) {
    while (udp_.available() > 0) {
      (void)udp_.read();
    }
    return false;
  }

  remote_ip = udp_.remoteIP();
  remote_port = udp_.remotePort();
  packet_len = udp_.read(frame, packet_len);
  if (packet_len <= 0) {
    return false;
  }

  return parseIncoming(frame, (size_t)packet_len, remote_ip, remote_port, out_packet);
}

bool MNetTransport::ready() const
{
  return ready_;
}

uint16_t MNetTransport::localPort() const
{
  return local_port_;
}

bool MNetTransport::buildPeerPacket(const uint8_t peer_pubkey[32],
                                    const uint8_t *plain,
                                    size_t plain_len,
                                    uint8_t *out_frame,
                                    size_t &out_frame_len)
{
  PacketHeader header;
  uint8_t local_pubkey[32];
  size_t cipher_len = kMaxCipherLen;

  if (out_frame == nullptr || peer_pubkey == nullptr) {
    return false;
  }
  if (mneta_get_pubkey(ctx_, local_pubkey) != MNETA_OK) {
    return false;
  }

  memset(&header, 0, sizeof(header));
  memcpy(header.magic, kPacketMagic, sizeof(header.magic));
  header.version = kVersion;
  header.flags = 0U;
  memcpy(header.src_pubkey, local_pubkey, sizeof(header.src_pubkey));

  if (mneta_encrypt_to(ctx_,
                       peer_pubkey,
                       plain,
                       plain_len,
                       out_frame + kHeaderSize,
                       &cipher_len) != MNETA_OK) {
    return false;
  }

  header.cipher_len = (uint16_t)cipher_len;
  memcpy(out_frame, &header.magic[0], 4U);
  out_frame[4] = header.version;
  out_frame[5] = header.flags;
  writeU16(out_frame + 6U, header.cipher_len);
  memcpy(out_frame + 8U, header.src_pubkey, sizeof(header.src_pubkey));
  memcpy(out_frame + 40U, header.group_hash, sizeof(header.group_hash));
  out_frame_len = kHeaderSize + cipher_len;
  return true;
}

bool MNetTransport::buildGroupPacket(const uint8_t group_hash[16],
                                     const uint8_t *plain,
                                     size_t plain_len,
                                     uint8_t *out_frame,
                                     size_t &out_frame_len)
{
  uint8_t local_pubkey[32];
  size_t cipher_len = kMaxCipherLen;

  if (out_frame == nullptr || group_hash == nullptr) {
    return false;
  }
  if (mneta_get_pubkey(ctx_, local_pubkey) != MNETA_OK) {
    return false;
  }
  if (mneta_encrypt_group(ctx_,
                          group_hash,
                          plain,
                          plain_len,
                          out_frame + kHeaderSize,
                          &cipher_len) != MNETA_OK) {
    return false;
  }

  memcpy(out_frame, kPacketMagic, 4U);
  out_frame[4] = kVersion;
  out_frame[5] = kFlagGroup;
  writeU16(out_frame + 6U, (uint16_t)cipher_len);
  memcpy(out_frame + 8U, local_pubkey, 32U);
  memcpy(out_frame + 40U, group_hash, 16U);
  out_frame_len = kHeaderSize + cipher_len;
  return true;
}

bool MNetTransport::parseIncoming(const uint8_t *frame,
                                  size_t frame_len,
                                  const IPAddress &remote_ip,
                                  uint16_t remote_port,
                                  MNetTransportPacket &out_packet)
{
  uint8_t plain[sizeof(out_packet.payload)];
  size_t plain_len = sizeof(plain);
  uint16_t cipher_len;
  bool is_group;
  mneta_err_t err;

  if (frame == nullptr || frame_len < kHeaderSize) {
    return false;
  }
  if (memcmp(frame, kPacketMagic, 4U) != 0 || frame[4] != kVersion) {
    return false;
  }

  is_group = (frame[5] & kFlagGroup) != 0U;
  cipher_len = readU16(frame + 6U);
  if (frame_len != (kHeaderSize + (size_t)cipher_len)) {
    return false;
  }

  if (is_group) {
    err = mneta_decrypt_group(ctx_,
                              frame + 40U,
                              frame + kHeaderSize,
                              cipher_len,
                              plain,
                              &plain_len);
  } else {
    err = mneta_decrypt_from(ctx_,
                             frame + 8U,
                             frame + kHeaderSize,
                             cipher_len,
                             plain,
                             &plain_len);
  }
  if (err != MNETA_OK || plain_len > sizeof(out_packet.payload)) {
    return false;
  }

  out_packet.valid = true;
  out_packet.is_group = is_group;
  out_packet.remote_ip = remote_ip;
  out_packet.remote_port = remote_port;
  memcpy(out_packet.src_pubkey, frame + 8U, sizeof(out_packet.src_pubkey));
  memcpy(out_packet.group_hash, frame + 40U, sizeof(out_packet.group_hash));
  memcpy(out_packet.payload, plain, plain_len);
  out_packet.payload_len = plain_len;
  return true;
}

int MNetTransport::findPeer(const uint8_t peer_pubkey[32]) const
{
  uint8_t i;

  if (peer_pubkey == nullptr) {
    return -1;
  }

  for (i = 0U; i < kMaxPeers; ++i) {
    if (peers_[i].used && memcmp(peers_[i].pubkey, peer_pubkey, sizeof(peers_[i].pubkey)) == 0) {
      return (int)i;
    }
  }

  return -1;
}

uint16_t MNetTransport::readU16(const uint8_t *src)
{
  return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

void MNetTransport::writeU16(uint8_t *dst, uint16_t value)
{
  dst[0] = (uint8_t)((value >> 8) & 0xFFU);
  dst[1] = (uint8_t)(value & 0xFFU);
}

bool MNetTransport::parseAddressAttr(const uint8_t *attr,
                                     size_t attr_len,
                                     IPAddress &out_ip,
                                     uint16_t &out_port,
                                     bool is_xor)
{
  uint8_t ip[4];
  uint16_t port;

  if (attr == nullptr || attr_len < 8U || attr[1] != 0x01U) {
    return false;
  }

  port = readU16(&attr[2]);
  memcpy(ip, &attr[4], sizeof(ip));
  if (is_xor) {
    port ^= (uint16_t)(kStunMagicCookie >> 16);
    ip[0] ^= (uint8_t)((kStunMagicCookie >> 24) & 0xFFU);
    ip[1] ^= (uint8_t)((kStunMagicCookie >> 16) & 0xFFU);
    ip[2] ^= (uint8_t)((kStunMagicCookie >> 8) & 0xFFU);
    ip[3] ^= (uint8_t)(kStunMagicCookie & 0xFFU);
  }

  out_ip = IPAddress(ip[0], ip[1], ip[2], ip[3]);
  out_port = port;
  return true;
}

bool MNetTransport::parseStunResponse(const uint8_t *resp,
                                      size_t resp_len,
                                      const uint8_t txid[12],
                                      IPAddress &out_ip,
                                      uint16_t &out_port)
{
  size_t offset = 20U;

  if (resp == nullptr || txid == nullptr || resp_len < 20U) {
    return false;
  }
  if (readU16(resp) != kStunBindingSuccess ||
      (((uint32_t)resp[4] << 24) | ((uint32_t)resp[5] << 16) | ((uint32_t)resp[6] << 8) | (uint32_t)resp[7]) != kStunMagicCookie ||
      memcmp(resp + 8U, txid, 12U) != 0) {
    return false;
  }

  while ((offset + 4U) <= resp_len) {
    uint16_t attr_type = readU16(resp + offset);
    uint16_t attr_len = readU16(resp + offset + 2U);
    const uint8_t *attr_data = resp + offset + 4U;
    size_t padded_len = (size_t)((attr_len + 3U) & ~3U);

    if ((offset + 4U + padded_len) > resp_len) {
      return false;
    }
    if (attr_type == kStunAttrXorMappedAddress &&
        parseAddressAttr(attr_data, attr_len, out_ip, out_port, true)) {
      return true;
    }
    if (attr_type == kStunAttrMappedAddress &&
        parseAddressAttr(attr_data, attr_len, out_ip, out_port, false)) {
      return true;
    }

    offset += 4U + padded_len;
  }

  return false;
}

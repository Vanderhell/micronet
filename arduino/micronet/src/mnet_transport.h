#ifndef MNET_TRANSPORT_H
#define MNET_TRANSPORT_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "mnet_arduino.h"

class MNetTransportPacket {
 public:
  bool valid = false;
  bool is_group = false;
  IPAddress remote_ip;
  uint16_t remote_port = 0U;
  uint8_t src_pubkey[32] = {0};
  uint8_t group_hash[16] = {0};
  uint8_t payload[512] = {0};
  size_t payload_len = 0U;
};

class MNetTransport {
 public:
  static constexpr uint8_t kMaxPeers = 8U;

  MNetTransport();

  bool begin(mneta_t *ctx, uint16_t local_port);
  void end();

  void setStunServer(const char *host, uint16_t port);
  bool resolveExternal(IPAddress &out_ip, uint16_t &out_port, uint32_t timeout_ms = 3000UL);

  bool addPeer(const uint8_t peer_pubkey[32], const IPAddress &ip, uint16_t port);
  bool updatePeer(const uint8_t peer_pubkey[32], const IPAddress &ip, uint16_t port);

  bool sendTo(const uint8_t peer_pubkey[32], const uint8_t *plain, size_t plain_len);
  bool sendToGroup(const uint8_t group_hash[16],
                   const IPAddress &ip,
                   uint16_t port,
                   const uint8_t *plain,
                   size_t plain_len);

  bool tick(MNetTransportPacket &out_packet);

  bool ready() const;
  uint16_t localPort() const;

 private:
  struct PeerEntry {
    bool used;
    uint8_t pubkey[32];
    IPAddress ip;
    uint16_t port;
  };

  struct PacketHeader {
    uint8_t magic[4];
    uint8_t version;
    uint8_t flags;
    uint16_t cipher_len;
    uint8_t src_pubkey[32];
    uint8_t group_hash[16];
  };

  static constexpr uint8_t kFlagGroup = 0x01U;
  static constexpr uint8_t kVersion = 1U;
  static constexpr size_t kHeaderSize = 56U;
  static constexpr size_t kMaxCipherLen = 576U;

  bool buildPeerPacket(const uint8_t peer_pubkey[32],
                       const uint8_t *plain,
                       size_t plain_len,
                       uint8_t *out_frame,
                       size_t &out_frame_len);
  bool buildGroupPacket(const uint8_t group_hash[16],
                        const uint8_t *plain,
                        size_t plain_len,
                        uint8_t *out_frame,
                        size_t &out_frame_len);
  bool parseIncoming(const uint8_t *frame,
                     size_t frame_len,
                     const IPAddress &remote_ip,
                     uint16_t remote_port,
                     MNetTransportPacket &out_packet);
  int findPeer(const uint8_t peer_pubkey[32]) const;

  static uint16_t readU16(const uint8_t *src);
  static void writeU16(uint8_t *dst, uint16_t value);
  static bool parseAddressAttr(const uint8_t *attr,
                               size_t attr_len,
                               IPAddress &out_ip,
                               uint16_t &out_port,
                               bool is_xor);
  static bool parseStunResponse(const uint8_t *resp,
                                size_t resp_len,
                                const uint8_t txid[12],
                                IPAddress &out_ip,
                                uint16_t &out_port);

  mneta_t *ctx_;
  WiFiUDP udp_;
  bool ready_;
  uint16_t local_port_;
  char stun_host_[64];
  uint16_t stun_port_;
  PeerEntry peers_[kMaxPeers];
};

#endif

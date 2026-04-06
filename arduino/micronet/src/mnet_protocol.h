#ifndef MNET_PROTOCOL_H
#define MNET_PROTOCOL_H

#include <Arduino.h>

#include "mnet_transport.h"

class MNetProtocolMessage {
 public:
  bool valid = false;
  bool is_group = false;
  uint8_t msg_type = 0U;
  uint16_t msg_id = 0U;
  IPAddress remote_ip;
  uint16_t remote_port = 0U;
  uint8_t src_pubkey[32] = {0};
  uint8_t group_hash[16] = {0};
  uint8_t payload[480] = {0};
  size_t payload_len = 0U;
};

typedef void (*MNetProtocolHandlerFn)(const MNetProtocolMessage &msg, void *user);

class MNetProtocol {
 public:
  static constexpr uint8_t kMaxHandlers = 16U;
  static constexpr size_t kMaxPayloadSize = 480U;

  MNetProtocol();

  bool begin(MNetTransport *transport);
  void end();

  bool registerHandler(uint8_t msg_type, MNetProtocolHandlerFn handler, void *user = nullptr);
  bool sendCustomTo(const uint8_t peer_pubkey[32], uint8_t msg_type, const uint8_t *payload, size_t payload_len);
  bool sendCustomTextTo(const uint8_t peer_pubkey[32], uint8_t msg_type, const char *text);
  bool sendCustomToGroup(const uint8_t group_hash[16],
                         const IPAddress &ip,
                         uint16_t port,
                         uint8_t msg_type,
                         const uint8_t *payload,
                         size_t payload_len);
  bool tick(MNetProtocolMessage &out_msg);

 private:
  struct HandlerEntry {
    bool used;
    uint8_t msg_type;
    MNetProtocolHandlerFn handler;
    void *user;
  };

  struct ProtocolHeader {
    uint8_t magic[4];
    uint8_t version;
    uint8_t msg_type;
    uint16_t msg_id;
    uint16_t payload_len;
  };

  static constexpr size_t kHeaderSize = 10U;
  static constexpr uint8_t kVersion = 1U;

  bool buildFrame(uint8_t msg_type,
                  const uint8_t *payload,
                  size_t payload_len,
                  uint8_t *out_frame,
                  size_t &out_frame_len);
  bool parseFrame(const MNetTransportPacket &packet, MNetProtocolMessage &out_msg);
  void dispatch(const MNetProtocolMessage &msg);

  MNetTransport *transport_;
  HandlerEntry handlers_[kMaxHandlers];
  uint16_t next_msg_id_;
};

#endif

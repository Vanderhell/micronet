#include "mnet_protocol.h"

#include <string.h>

namespace {

constexpr uint8_t kProtocolMagic[4] = {'M', 'N', 'P', '1'};

uint16_t read_u16(const uint8_t *src)
{
  return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

void write_u16(uint8_t *dst, uint16_t value)
{
  dst[0] = (uint8_t)((value >> 8) & 0xFFU);
  dst[1] = (uint8_t)(value & 0xFFU);
}

}  // namespace

MNetProtocol::MNetProtocol()
    : transport_(nullptr), next_msg_id_(1U)
{
  memset(handlers_, 0, sizeof(handlers_));
}

bool MNetProtocol::begin(MNetTransport *transport)
{
  if (transport == nullptr || !transport->ready()) {
    return false;
  }

  transport_ = transport;
  next_msg_id_ = 1U;
  memset(handlers_, 0, sizeof(handlers_));
  return true;
}

void MNetProtocol::end()
{
  transport_ = nullptr;
  memset(handlers_, 0, sizeof(handlers_));
  next_msg_id_ = 1U;
}

bool MNetProtocol::registerHandler(uint8_t msg_type, MNetProtocolHandlerFn handler, void *user)
{
  uint8_t i;

  if (handler == nullptr) {
    return false;
  }

  for (i = 0U; i < kMaxHandlers; ++i) {
    if (handlers_[i].used && handlers_[i].msg_type == msg_type) {
      handlers_[i].handler = handler;
      handlers_[i].user = user;
      return true;
    }
  }

  for (i = 0U; i < kMaxHandlers; ++i) {
    if (!handlers_[i].used) {
      handlers_[i].used = true;
      handlers_[i].msg_type = msg_type;
      handlers_[i].handler = handler;
      handlers_[i].user = user;
      return true;
    }
  }

  return false;
}

bool MNetProtocol::sendCustomTo(const uint8_t peer_pubkey[32], uint8_t msg_type, const uint8_t *payload, size_t payload_len)
{
  uint8_t frame[512];
  size_t frame_len = 0U;

  if (transport_ == nullptr || peer_pubkey == nullptr) {
    return false;
  }
  if (!buildFrame(msg_type, payload, payload_len, frame, frame_len)) {
    return false;
  }

  return transport_->sendTo(peer_pubkey, frame, frame_len);
}

bool MNetProtocol::sendCustomTextTo(const uint8_t peer_pubkey[32], uint8_t msg_type, const char *text)
{
  return sendCustomTo(peer_pubkey,
                      msg_type,
                      reinterpret_cast<const uint8_t *>(text),
                      text != nullptr ? strlen(text) : 0U);
}

bool MNetProtocol::sendCustomToGroup(const uint8_t group_hash[16],
                                     const IPAddress &ip,
                                     uint16_t port,
                                     uint8_t msg_type,
                                     const uint8_t *payload,
                                     size_t payload_len)
{
  uint8_t frame[512];
  size_t frame_len = 0U;

  if (transport_ == nullptr || group_hash == nullptr) {
    return false;
  }
  if (!buildFrame(msg_type, payload, payload_len, frame, frame_len)) {
    return false;
  }

  return transport_->sendToGroup(group_hash, ip, port, frame, frame_len);
}

bool MNetProtocol::tick(MNetProtocolMessage &out_msg)
{
  MNetTransportPacket packet;

  out_msg = MNetProtocolMessage();
  if (transport_ == nullptr) {
    return false;
  }
  if (!transport_->tick(packet) || !packet.valid) {
    return false;
  }
  if (!parseFrame(packet, out_msg)) {
    return false;
  }

  dispatch(out_msg);
  return true;
}

bool MNetProtocol::buildFrame(uint8_t msg_type,
                              const uint8_t *payload,
                              size_t payload_len,
                              uint8_t *out_frame,
                              size_t &out_frame_len)
{
  if (out_frame == nullptr || payload_len > kMaxPayloadSize) {
    return false;
  }
  if (payload == nullptr && payload_len > 0U) {
    return false;
  }

  memcpy(out_frame, kProtocolMagic, 4U);
  out_frame[4] = kVersion;
  out_frame[5] = msg_type;
  write_u16(out_frame + 6U, next_msg_id_++);
  write_u16(out_frame + 8U, (uint16_t)payload_len);
  if (payload_len > 0U) {
    memcpy(out_frame + kHeaderSize, payload, payload_len);
  }
  out_frame_len = kHeaderSize + payload_len;
  return true;
}

bool MNetProtocol::parseFrame(const MNetTransportPacket &packet, MNetProtocolMessage &out_msg)
{
  uint16_t payload_len;

  if (packet.payload_len < kHeaderSize) {
    return false;
  }
  if (memcmp(packet.payload, kProtocolMagic, 4U) != 0 || packet.payload[4] != kVersion) {
    return false;
  }

  payload_len = read_u16(packet.payload + 8U);
  if (packet.payload_len != (kHeaderSize + (size_t)payload_len) || payload_len > kMaxPayloadSize) {
    return false;
  }

  out_msg.valid = true;
  out_msg.is_group = packet.is_group;
  out_msg.msg_type = packet.payload[5];
  out_msg.msg_id = read_u16(packet.payload + 6U);
  out_msg.remote_ip = packet.remote_ip;
  out_msg.remote_port = packet.remote_port;
  memcpy(out_msg.src_pubkey, packet.src_pubkey, sizeof(out_msg.src_pubkey));
  memcpy(out_msg.group_hash, packet.group_hash, sizeof(out_msg.group_hash));
  out_msg.payload_len = payload_len;
  if (payload_len > 0U) {
    memcpy(out_msg.payload, packet.payload + kHeaderSize, payload_len);
  }
  return true;
}

void MNetProtocol::dispatch(const MNetProtocolMessage &msg)
{
  uint8_t i;

  for (i = 0U; i < kMaxHandlers; ++i) {
    if (handlers_[i].used &&
        handlers_[i].msg_type == msg.msg_type &&
        handlers_[i].handler != nullptr) {
      handlers_[i].handler(msg, handlers_[i].user);
    }
  }
}

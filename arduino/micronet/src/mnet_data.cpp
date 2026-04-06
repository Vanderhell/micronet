#include "mnet_data.h"

#include <ESP.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

void bounded_copy(char *dst, size_t dst_len, const char *src)
{
  if (dst == nullptr || dst_len == 0U) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dst_len - 1U);
  dst[dst_len - 1U] = '\0';
}

}  // namespace

MNetData::MNetData()
    : protocol_(nullptr),
      started_ms_(0U),
      packets_sent_(0U),
      packets_recv_(0U),
      errors_(0U),
      request_cb_(nullptr),
      request_user_(nullptr),
      list_cb_(nullptr),
      list_user_(nullptr),
      metrics_cb_(nullptr),
      metrics_user_(nullptr)
{
  memset(vars_, 0, sizeof(vars_));
}

bool MNetData::begin(MNetProtocol *protocol)
{
  if (protocol == nullptr) {
    return false;
  }

  protocol_ = protocol;
  memset(vars_, 0, sizeof(vars_));
  started_ms_ = millis();
  packets_sent_ = 0U;
  packets_recv_ = 0U;
  errors_ = 0U;

  return protocol_->registerHandler(kMsgRequest, &MNetData::onRequest, this) &&
         protocol_->registerHandler(kMsgResponse, &MNetData::onResponse, this) &&
         protocol_->registerHandler(kMsgListRequest, &MNetData::onListRequest, this) &&
         protocol_->registerHandler(kMsgListResponse, &MNetData::onListResponse, this) &&
         protocol_->registerHandler(kMsgMetricsRequest, &MNetData::onMetricsRequest, this) &&
         protocol_->registerHandler(kMsgMetricsResponse, &MNetData::onMetricsResponse, this);
}

void MNetData::end()
{
  protocol_ = nullptr;
  memset(vars_, 0, sizeof(vars_));
  packets_sent_ = 0U;
  packets_recv_ = 0U;
  errors_ = 0U;
}

bool MNetData::publish(const char *key, const char *value)
{
  return update(key, value);
}

bool MNetData::update(const char *key, const char *value)
{
  int idx;
  uint8_t i;

  if (key == nullptr || key[0] == '\0' || value == nullptr) {
    return false;
  }

  idx = findVar(key);
  if (idx >= 0) {
    bounded_copy(vars_[idx].value, sizeof(vars_[idx].value), value);
    return true;
  }

  for (i = 0U; i < kMaxVars; ++i) {
    if (!vars_[i].used) {
      vars_[i].used = true;
      bounded_copy(vars_[i].key, sizeof(vars_[i].key), key);
      bounded_copy(vars_[i].value, sizeof(vars_[i].value), value);
      return true;
    }
  }

  return false;
}

bool MNetData::getLocal(const char *key, char *out_value, size_t out_value_len) const
{
  int idx = findVar(key);

  if (idx < 0 || out_value == nullptr || out_value_len == 0U) {
    return false;
  }

  bounded_copy(out_value, out_value_len, vars_[idx].value);
  return true;
}

uint8_t MNetData::varCount() const
{
  uint8_t i;
  uint8_t count = 0U;

  for (i = 0U; i < kMaxVars; ++i) {
    if (vars_[i].used) {
      count++;
    }
  }
  return count;
}

bool MNetData::request(const uint8_t peer_pubkey[32], const char *key)
{
  bool ok;

  if (protocol_ == nullptr || peer_pubkey == nullptr || key == nullptr) {
    return false;
  }

  ok = protocol_->sendCustomTextTo(peer_pubkey, kMsgRequest, key);
  if (ok) {
    packets_sent_++;
  } else {
    errors_++;
  }
  return ok;
}

bool MNetData::listVars(const uint8_t peer_pubkey[32])
{
  bool ok;

  if (protocol_ == nullptr || peer_pubkey == nullptr) {
    return false;
  }

  ok = protocol_->sendCustomTo(peer_pubkey, kMsgListRequest, nullptr, 0U);
  if (ok) {
    packets_sent_++;
  } else {
    errors_++;
  }
  return ok;
}

bool MNetData::getMetrics(const uint8_t peer_pubkey[32])
{
  bool ok;

  if (protocol_ == nullptr || peer_pubkey == nullptr) {
    return false;
  }

  ok = protocol_->sendCustomTo(peer_pubkey, kMsgMetricsRequest, nullptr, 0U);
  if (ok) {
    packets_sent_++;
  } else {
    errors_++;
  }
  return ok;
}

void MNetData::setRequestCallback(MNetDataRequestCallback cb, void *user)
{
  request_cb_ = cb;
  request_user_ = user;
}

void MNetData::setListCallback(MNetDataListCallback cb, void *user)
{
  list_cb_ = cb;
  list_user_ = user;
}

void MNetData::setMetricsCallback(MNetDataMetricsCallback cb, void *user)
{
  metrics_cb_ = cb;
  metrics_user_ = user;
}

void MNetData::onRequest(const MNetProtocolMessage &msg, void *user)
{
  MNetData *self = static_cast<MNetData *>(user);
  char key[kMaxKeyLen + 1U];
  char value[kMaxValueLen + 1U];
  size_t copy_len;

  if (self == nullptr) {
    return;
  }

  self->packets_recv_++;
  copy_len = msg.payload_len < kMaxKeyLen ? msg.payload_len : kMaxKeyLen;
  memcpy(key, msg.payload, copy_len);
  key[copy_len] = '\0';

  if (!self->getLocal(key, value, sizeof(value))) {
    bounded_copy(value, sizeof(value), "NOT_FOUND");
  }
  if (!self->sendResponse(msg.src_pubkey, key, value)) {
    self->errors_++;
  }
}

void MNetData::onResponse(const MNetProtocolMessage &msg, void *user)
{
  MNetData *self = static_cast<MNetData *>(user);
  char body[kMaxKeyLen + kMaxValueLen + 8U];
  size_t copy_len;
  char *sep;

  if (self == nullptr) {
    return;
  }

  self->packets_recv_++;
  copy_len = msg.payload_len < (sizeof(body) - 1U) ? msg.payload_len : (sizeof(body) - 1U);
  memcpy(body, msg.payload, copy_len);
  body[copy_len] = '\0';
  sep = strchr(body, '=');
  if (sep == nullptr) {
    self->errors_++;
    return;
  }

  *sep = '\0';
  if (self->request_cb_ != nullptr) {
    self->request_cb_(msg.src_pubkey, body, sep + 1, self->request_user_);
  }
}

void MNetData::onListRequest(const MNetProtocolMessage &msg, void *user)
{
  MNetData *self = static_cast<MNetData *>(user);

  (void)msg;
  if (self == nullptr) {
    return;
  }

  self->packets_recv_++;
  if (!self->sendListResponse(msg.src_pubkey)) {
    self->errors_++;
  }
}

void MNetData::onListResponse(const MNetProtocolMessage &msg, void *user)
{
  MNetData *self = static_cast<MNetData *>(user);
  char csv[256];
  size_t copy_len;

  if (self == nullptr) {
    return;
  }

  self->packets_recv_++;
  copy_len = msg.payload_len < (sizeof(csv) - 1U) ? msg.payload_len : (sizeof(csv) - 1U);
  memcpy(csv, msg.payload, copy_len);
  csv[copy_len] = '\0';
  if (self->list_cb_ != nullptr) {
    self->list_cb_(msg.src_pubkey, csv, self->list_user_);
  }
}

void MNetData::onMetricsRequest(const MNetProtocolMessage &msg, void *user)
{
  MNetData *self = static_cast<MNetData *>(user);

  if (self == nullptr) {
    return;
  }

  self->packets_recv_++;
  if (!self->sendMetricsResponse(msg.src_pubkey)) {
    self->errors_++;
  }
}

void MNetData::onMetricsResponse(const MNetProtocolMessage &msg, void *user)
{
  MNetData *self = static_cast<MNetData *>(user);
  MNetDataMetrics metrics;
  char body[200];
  size_t copy_len;

  if (self == nullptr) {
    return;
  }

  self->packets_recv_++;
  copy_len = msg.payload_len < (sizeof(body) - 1U) ? msg.payload_len : (sizeof(body) - 1U);
  memcpy(body, msg.payload, copy_len);
  body[copy_len] = '\0';

  if (sscanf(body,
             "uptime=%lu;heap=%lu;nodes=%hhu;groups=%hhu;tx=%lu;rx=%lu;err=%lu;health=%hhu",
             &metrics.uptime_s,
             &metrics.free_heap,
             &metrics.connected_nodes,
             &metrics.group_count,
             &metrics.packets_sent,
             &metrics.packets_recv,
             &metrics.errors,
             &metrics.health_score) != 8) {
    self->errors_++;
    return;
  }

  if (self->metrics_cb_ != nullptr) {
    self->metrics_cb_(msg.src_pubkey, metrics, self->metrics_user_);
  }
}

bool MNetData::sendResponse(const uint8_t peer_pubkey[32], const char *key, const char *value)
{
  char body[140];
  bool ok;

  snprintf(body, sizeof(body), "%s=%s", key != nullptr ? key : "", value != nullptr ? value : "");
  ok = protocol_->sendCustomTextTo(peer_pubkey, kMsgResponse, body);
  if (ok) {
    packets_sent_++;
  }
  return ok;
}

bool MNetData::sendListResponse(const uint8_t peer_pubkey[32])
{
  char body[256];
  size_t pos = 0U;
  uint8_t i;
  bool ok;

  body[0] = '\0';
  for (i = 0U; i < kMaxVars && pos < sizeof(body); ++i) {
    if (!vars_[i].used) {
      continue;
    }
    pos += (size_t)snprintf(body + pos,
                            sizeof(body) - pos,
                            "%s%s",
                            pos == 0U ? "" : ",",
                            vars_[i].key);
  }

  ok = protocol_->sendCustomTextTo(peer_pubkey, kMsgListResponse, body);
  if (ok) {
    packets_sent_++;
  }
  return ok;
}

bool MNetData::sendMetricsResponse(const uint8_t peer_pubkey[32])
{
  char body[180];
  bool ok;

  snprintf(body,
           sizeof(body),
           "uptime=%lu;heap=%lu;nodes=%u;groups=%u;tx=%lu;rx=%lu;err=%lu;health=%u",
           (unsigned long)((millis() - started_ms_) / 1000UL),
           (unsigned long)ESP.getFreeHeap(),
           1U,
           0U,
           (unsigned long)packets_sent_,
           (unsigned long)packets_recv_,
           (unsigned long)errors_,
           100U);

  ok = protocol_->sendCustomTextTo(peer_pubkey, kMsgMetricsResponse, body);
  if (ok) {
    packets_sent_++;
  }
  return ok;
}

int MNetData::findVar(const char *key) const
{
  uint8_t i;

  if (key == nullptr) {
    return -1;
  }

  for (i = 0U; i < kMaxVars; ++i) {
    if (vars_[i].used && strcmp(vars_[i].key, key) == 0) {
      return (int)i;
    }
  }

  return -1;
}

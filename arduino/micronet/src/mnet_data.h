#ifndef MNET_DATA_H
#define MNET_DATA_H

#include <Arduino.h>

#include "mnet_contract.h"
#include "mnet_protocol.h"

class MNetDataMetrics {
 public:
  uint32_t uptime_s = 0U;
  uint32_t free_heap = 0U;
  uint8_t connected_nodes = 0U;
  uint8_t group_count = 0U;
  uint32_t packets_sent = 0U;
  uint32_t packets_recv = 0U;
  uint32_t errors = 0U;
  uint8_t health_score = 0U;
};

typedef void (*MNetDataRequestCallback)(const uint8_t src_pubkey[32],
                                        const char *key,
                                        const char *value,
                                        void *user);
typedef void (*MNetDataListCallback)(const uint8_t src_pubkey[32],
                                     const char *csv_keys,
                                     void *user);
typedef void (*MNetDataMetricsCallback)(const uint8_t src_pubkey[32],
                                        const MNetDataMetrics &metrics,
                                        void *user);
typedef void (*MNetDataNotifyCallback)(const uint8_t src_pubkey[32],
                                       const char *key,
                                       const char *value,
                                       void *user);
typedef void (*MNetDataQueryCallback)(const uint8_t src_pubkey[32],
                                      const char *rows,
                                      void *user);

class MNetData {
 public:
  static constexpr uint8_t kMaxVars = 16U;
  static constexpr uint8_t kMaxSubscriptions = 16U;
  static constexpr size_t kMaxKeyLen = 31U;
  static constexpr size_t kMaxValueLen = 95U;

  MNetData();

  bool begin(MNetProtocol *protocol);
  void end();

  bool publish(const char *key, const char *value);
  bool update(const char *key, const char *value);
  bool getLocal(const char *key, char *out_value, size_t out_value_len) const;
  uint8_t varCount() const;

  bool request(const uint8_t peer_pubkey[32], const char *key);
  bool listVars(const uint8_t peer_pubkey[32]);
  bool getMetrics(const uint8_t peer_pubkey[32]);
  bool subscribe(const uint8_t peer_pubkey[32], const char *key);
  bool unsubscribe(const uint8_t peer_pubkey[32], const char *key);
  bool query(const uint8_t peer_pubkey[32], const char *filter);

  void setRequestCallback(MNetDataRequestCallback cb, void *user = nullptr);
  void setListCallback(MNetDataListCallback cb, void *user = nullptr);
  void setMetricsCallback(MNetDataMetricsCallback cb, void *user = nullptr);
  void setNotifyCallback(MNetDataNotifyCallback cb, void *user = nullptr);
  void setQueryCallback(MNetDataQueryCallback cb, void *user = nullptr);

 private:
  struct VarEntry {
    bool used;
    char key[kMaxKeyLen + 1U];
    char value[kMaxValueLen + 1U];
  };
  struct SubscriptionEntry {
    bool used;
    uint8_t peer_pubkey[32];
    char key[kMaxKeyLen + 1U];
  };

  static constexpr uint8_t kMsgRequest = (uint8_t)MNETA_DATA_MSG_REQUEST;
  static constexpr uint8_t kMsgResponse = (uint8_t)MNETA_DATA_MSG_RESPONSE;
  static constexpr uint8_t kMsgListRequest = (uint8_t)MNETA_DATA_MSG_LIST_REQUEST;
  static constexpr uint8_t kMsgListResponse = (uint8_t)MNETA_DATA_MSG_LIST_RESPONSE;
  static constexpr uint8_t kMsgMetricsRequest = (uint8_t)MNETA_DATA_MSG_METRICS_REQUEST;
  static constexpr uint8_t kMsgMetricsResponse = (uint8_t)MNETA_DATA_MSG_METRICS_RESPONSE;
  static constexpr uint8_t kMsgSubscribe = (uint8_t)MNETA_DATA_MSG_SUBSCRIBE;
  static constexpr uint8_t kMsgUnsubscribe = (uint8_t)MNETA_DATA_MSG_UNSUBSCRIBE;
  static constexpr uint8_t kMsgNotify = (uint8_t)MNETA_DATA_MSG_NOTIFY;
  static constexpr uint8_t kMsgQueryRequest = (uint8_t)MNETA_DATA_MSG_QUERY_REQUEST;
  static constexpr uint8_t kMsgQueryResponse = (uint8_t)MNETA_DATA_MSG_QUERY_RESPONSE;

  static void onRequest(const MNetProtocolMessage &msg, void *user);
  static void onResponse(const MNetProtocolMessage &msg, void *user);
  static void onListRequest(const MNetProtocolMessage &msg, void *user);
  static void onListResponse(const MNetProtocolMessage &msg, void *user);
  static void onMetricsRequest(const MNetProtocolMessage &msg, void *user);
  static void onMetricsResponse(const MNetProtocolMessage &msg, void *user);
  static void onSubscribe(const MNetProtocolMessage &msg, void *user);
  static void onUnsubscribe(const MNetProtocolMessage &msg, void *user);
  static void onNotify(const MNetProtocolMessage &msg, void *user);
  static void onQueryRequest(const MNetProtocolMessage &msg, void *user);
  static void onQueryResponse(const MNetProtocolMessage &msg, void *user);

  bool sendResponse(const uint8_t peer_pubkey[32], const char *key, const char *value);
  bool sendListResponse(const uint8_t peer_pubkey[32]);
  bool sendMetricsResponse(const uint8_t peer_pubkey[32]);
  bool sendNotify(const uint8_t peer_pubkey[32], const char *key, const char *value);
  bool sendQueryResponse(const uint8_t peer_pubkey[32], const char *filter);
  bool sendKeyValueMessage(uint8_t msg_type,
                           const uint8_t peer_pubkey[32],
                           const char *key,
                           const char *value);
  void publishNotify(const char *key, const char *value);
  int findVar(const char *key) const;
  int findSubscription(const uint8_t peer_pubkey[32], const char *key) const;

  MNetProtocol *protocol_;
  VarEntry vars_[kMaxVars];
  SubscriptionEntry subs_[kMaxSubscriptions];
  uint32_t started_ms_;
  uint32_t packets_sent_;
  uint32_t packets_recv_;
  uint32_t errors_;
  MNetDataRequestCallback request_cb_;
  void *request_user_;
  MNetDataListCallback list_cb_;
  void *list_user_;
  MNetDataMetricsCallback metrics_cb_;
  void *metrics_user_;
  MNetDataNotifyCallback notify_cb_;
  void *notify_user_;
  MNetDataQueryCallback query_cb_;
  void *query_user_;
};

#endif

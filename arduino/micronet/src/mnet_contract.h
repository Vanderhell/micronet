#ifndef MNET_CONTRACT_H
#define MNET_CONTRACT_H

#include "../../../include/micronet_limits.h"
#include "../../../src/protocol/p2p_protocol.h"
#include "../../../src/transport/p2p_transport.h"

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define MNET_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#else
#define MNET_STATIC_ASSERT(cond, msg) typedef char mnet_static_assert_##__LINE__[(cond) ? 1 : -1]
#endif

#define MNETA_PROTOCOL_VERSION 1U
#define MNETA_TRANSPORT_VERSION P2P_TRANSPORT_VERSION
#define MNETA_PROTOCOL_MAGIC_0 ((uint8_t)'M')
#define MNETA_PROTOCOL_MAGIC_1 ((uint8_t)'N')
#define MNETA_PROTOCOL_MAGIC_2 ((uint8_t)'P')
#define MNETA_PROTOCOL_MAGIC_3 ((uint8_t)'1')
#define MNETA_TRANSPORT_MAGIC_0 P2P_TRANSPORT_MAGIC_0
#define MNETA_TRANSPORT_MAGIC_1 P2P_TRANSPORT_MAGIC_1
#define MNETA_DATA_MSG_REQUEST P2P_MSG_DATA_REQUEST
#define MNETA_DATA_MSG_RESPONSE P2P_MSG_DATA_RESPONSE
#define MNETA_DATA_MSG_LIST_REQUEST P2P_MSG_LIST_VARS_REQ
#define MNETA_DATA_MSG_LIST_RESPONSE P2P_MSG_LIST_VARS_RESP
#define MNETA_DATA_MSG_METRICS_REQUEST P2P_MSG_METRICS_REQ
#define MNETA_DATA_MSG_METRICS_RESPONSE P2P_MSG_METRICS_RESP
#define MNETA_DATA_MSG_SUBSCRIBE P2P_MSG_DATA_SUBSCRIBE
#define MNETA_DATA_MSG_UNSUBSCRIBE P2P_MSG_DATA_UNSUBSCRIBE
#define MNETA_DATA_MSG_NOTIFY P2P_MSG_DATA_NOTIFY
#define MNETA_DATA_MSG_QUERY_REQUEST P2P_MSG_QUERY_REQ
#define MNETA_DATA_MSG_QUERY_RESPONSE P2P_MSG_QUERY_RESP
#define MNETA_PROTOCOL_MAX_PAYLOAD MNET_MAX_PUBLIC_PAYLOAD
#define MNETA_TRANSPORT_MAX_CIPHER_LEN (MNET_TRANSPORT_MAX_WIRE_PAYLOAD - MNET_TRANSPORT_HEADER_SIZE)

MNET_STATIC_ASSERT(MNET_MAX_PUBLIC_PAYLOAD == 375U, "canonical payload cap drifted");
MNET_STATIC_ASSERT(P2P_TRANSPORT_VERSION == 0x01U, "transport wire version drifted");
MNET_STATIC_ASSERT(P2P_TRANSPORT_MAGIC_0 == (uint8_t)'P', "transport magic drifted");
MNET_STATIC_ASSERT(P2P_TRANSPORT_MAGIC_1 == (uint8_t)'2', "transport magic drifted");
MNET_STATIC_ASSERT(P2P_MSG_DATA_REQUEST == 0x20U, "data request type drifted");
MNET_STATIC_ASSERT(P2P_MSG_DATA_RESPONSE == 0x21U, "data response type drifted");
MNET_STATIC_ASSERT(P2P_MSG_LIST_VARS_REQ == 0x29U, "list request type drifted");
MNET_STATIC_ASSERT(P2P_MSG_LIST_VARS_RESP == 0x2AU, "list response type drifted");
MNET_STATIC_ASSERT(P2P_MSG_METRICS_REQ == 0x27U, "metrics request type drifted");
MNET_STATIC_ASSERT(P2P_MSG_METRICS_RESP == 0x28U, "metrics response type drifted");
MNET_STATIC_ASSERT(P2P_MSG_DATA_SUBSCRIBE == 0x23U, "subscribe type drifted");
MNET_STATIC_ASSERT(P2P_MSG_DATA_UNSUBSCRIBE == 0x24U, "unsubscribe type drifted");
MNET_STATIC_ASSERT(P2P_MSG_DATA_NOTIFY == 0x22U, "notify type drifted");
MNET_STATIC_ASSERT(P2P_MSG_QUERY_REQ == 0x25U, "query request type drifted");
MNET_STATIC_ASSERT(P2P_MSG_QUERY_RESP == 0x26U, "query response type drifted");

#if defined(__cplusplus)
}
#endif

#endif

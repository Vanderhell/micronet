#ifndef MICRONET_INTERNAL_H
#define MICRONET_INTERNAL_H

#include "../include/micronet.h"

#include "data/p2p_data.h"
#include "network/p2p_network.h"
#include "protocol/p2p_protocol.h"
#include "security/p2p_security.h"
#include "transport/p2p_transport.h"

typedef struct {
    bool initialized;
    mnet_config_t cfg;
    p2p_transport_t transport;
    p2p_security_t security;
    p2p_network_t network;
    p2p_data_t data;
    p2p_protocol_t protocol;
    void (*custom_handlers[128])(const uint8_t src[32], const uint8_t *payload, size_t len);
    void (*request_cb)(mnet_err_t, const void *, size_t);
    void (*list_vars_cb)(mnet_err_t, const char **, uint8_t);
    void (*query_cb)(mnet_err_t, const mnet_row_t *, uint8_t);
    void (*metrics_cb)(mnet_err_t, const mnet_metrics_t *);
} mnet_context_t;

mnet_context_t *mnet_internal_context(void);

#endif

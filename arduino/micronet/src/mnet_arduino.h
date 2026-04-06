#ifndef MNET_ARDUINO_H
#define MNET_ARDUINO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p2p_security.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MNETA_MAX_GROUPS
#define MNETA_MAX_GROUPS 8U
#endif

typedef enum {
    MNETA_OK = 0,
    MNETA_ERR_NOT_INIT = -1,
    MNETA_ERR_INVALID_ARG = -2,
    MNETA_ERR_CRYPTO = -3,
    MNETA_ERR_FULL = -4,
    MNETA_ERR_NOT_FOUND = -5,
    MNETA_ERR_STORAGE = -6
} mneta_err_t;

typedef struct {
    uint8_t group_hash[16];
    uint8_t group_key[16];
} mneta_group_t;

typedef struct {
    bool store_group_keys;
    uint8_t node_privkey[32];
    uint8_t node_pubkey[32];
    mneta_group_t groups[MNETA_MAX_GROUPS];
    uint8_t group_count;
} mneta_config_t;

typedef struct {
    bool initialized;
    bool store_group_keys;
    p2p_security_t security;
    mneta_group_t groups[MNETA_MAX_GROUPS];
    uint8_t group_count;
} mneta_t;

mneta_err_t mneta_init(mneta_t *ctx, const mneta_config_t *cfg);
void mneta_deinit(mneta_t *ctx);

mneta_err_t mneta_get_node_id(mneta_t *ctx, uint8_t out_node_id[32]);
mneta_err_t mneta_get_pubkey(mneta_t *ctx, uint8_t out_pubkey[32]);
mneta_err_t mneta_get_privkey(mneta_t *ctx, uint8_t out_privkey[32]);

mneta_err_t mneta_group_create(mneta_t *ctx, uint8_t out_group_hash[16], uint8_t out_group_key[16]);
mneta_err_t mneta_group_join(mneta_t *ctx, const uint8_t group_hash[16], const uint8_t group_key[16]);
mneta_err_t mneta_group_get(mneta_t *ctx, uint8_t index, mneta_group_t *out_group);
uint8_t mneta_group_count(mneta_t *ctx);

mneta_err_t mneta_handshake(mneta_t *ctx, const uint8_t remote_pubkey[32]);
bool mneta_is_authenticated(mneta_t *ctx, const uint8_t remote_pubkey[32]);

mneta_err_t mneta_encrypt_to(mneta_t *ctx,
                             const uint8_t remote_pubkey[32],
                             const uint8_t *plain,
                             size_t plain_len,
                             uint8_t *out,
                             size_t *out_len);
mneta_err_t mneta_decrypt_from(mneta_t *ctx,
                               const uint8_t remote_pubkey[32],
                               const uint8_t *cipher,
                               size_t cipher_len,
                               uint8_t *out,
                               size_t *out_len);

mneta_err_t mneta_encrypt_group(mneta_t *ctx,
                                const uint8_t group_hash[16],
                                const uint8_t *plain,
                                size_t plain_len,
                                uint8_t *out,
                                size_t *out_len);
mneta_err_t mneta_decrypt_group(mneta_t *ctx,
                                const uint8_t group_hash[16],
                                const uint8_t *cipher,
                                size_t cipher_len,
                                uint8_t *out,
                                size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif

#ifndef P2P_SECURITY_H
#define P2P_SECURITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mcrypt.h"
#include "mdh.h"
#include "../../include/micronet_limits.h"

#ifndef P2P_MAX_GROUPS
#define P2P_MAX_GROUPS 8U
#endif

#ifndef P2P_MAX_SESSIONS
#define P2P_MAX_SESSIONS 8U
#endif

#define P2P_SESSION_KEY_SIZE 16U
#define P2P_NODE_KEY_SIZE 32U
#define P2P_HMAC_SIZE MNET_SECURITY_HMAC_SIZE
#define P2P_IV_SIZE MNET_SECURITY_IV_SIZE

typedef enum {
    P2P_SEC_OK = 0,
    P2P_SEC_ERR_KEYGEN = -1,
    P2P_SEC_ERR_HANDSHAKE = -2,
    P2P_SEC_ERR_HMAC = -3,
    P2P_SEC_ERR_DECRYPT = -4,
    P2P_SEC_ERR_NO_SESSION = -5,
    P2P_SEC_ERR_NO_GROUP = -6,
    P2P_SEC_ERR_BUF = -7,
} p2p_sec_err_t;

typedef struct {
    uint8_t node_privkey[P2P_NODE_KEY_SIZE];
    uint8_t node_pubkey[P2P_NODE_KEY_SIZE];
    uint8_t group_keys[P2P_MAX_GROUPS][P2P_SESSION_KEY_SIZE];
    uint8_t group_hashes[P2P_MAX_GROUPS][16];
    uint8_t group_count;
    bool store_keys;
    uint32_t (*now_ms)(void);
} p2p_security_config_t;

typedef struct {
    uint8_t group_hash[16];
    uint8_t group_key[P2P_SESSION_KEY_SIZE];
    bool valid;
} p2p_group_secret_t;

typedef struct {
    uint8_t session_key[P2P_SESSION_KEY_SIZE];
    uint8_t remote_pubkey[P2P_NODE_KEY_SIZE];
    bool inbound_verified;
    bool outbound_verified;
    bool authenticated;
    uint32_t established_at;
} p2p_session_t;

#ifndef P2P_MICROFSM_T_DEFINED
#define P2P_MICROFSM_T_DEFINED
typedef struct {
    uint8_t state;
} microfsm_t;
#endif

typedef struct {
    uint8_t node_privkey[P2P_NODE_KEY_SIZE];
    uint8_t node_pubkey[P2P_NODE_KEY_SIZE];
    p2p_session_t sessions[P2P_MAX_SESSIONS];
    p2p_group_secret_t groups[P2P_MAX_GROUPS];
    uint8_t group_count;
    uint32_t auth_failures;
    uint32_t crypto_failures;
    microfsm_t fsm;
    bool store_keys;
    uint32_t (*now_ms)(void);
} p2p_security_t;

p2p_sec_err_t p2p_security_init(p2p_security_t *ctx, const p2p_security_config_t *cfg);
p2p_sec_err_t p2p_security_get_pubkey(p2p_security_t *ctx, uint8_t pubkey[P2P_NODE_KEY_SIZE]);
p2p_sec_err_t p2p_security_handshake(p2p_security_t *ctx, const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE]);
p2p_sec_err_t p2p_security_build_hello_mac(p2p_security_t *ctx,
                                          const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE],
                                          uint8_t out_mac[P2P_HMAC_SIZE]);
p2p_sec_err_t p2p_security_verify_hello_mac(p2p_security_t *ctx,
                                            const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE],
                                            const uint8_t mac[P2P_HMAC_SIZE]);
p2p_sec_err_t p2p_security_mark_outbound_verified(p2p_security_t *ctx,
                                                 const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE]);
bool p2p_security_is_authenticated(p2p_security_t *ctx, const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE]);
p2p_sec_err_t p2p_security_encrypt(p2p_security_t *ctx, const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE],
                                   const uint8_t *plain, size_t plain_len,
                                   uint8_t *out, size_t *out_len);
p2p_sec_err_t p2p_security_decrypt(p2p_security_t *ctx, const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE],
                                   const uint8_t *cipher, size_t cipher_len,
                                   uint8_t *out, size_t *out_len);
p2p_sec_err_t p2p_security_encrypt_group(p2p_security_t *ctx, uint8_t group_idx,
                                         const uint8_t *plain, size_t plain_len,
                                         uint8_t *out, size_t *out_len);
p2p_sec_err_t p2p_security_decrypt_group(p2p_security_t *ctx, uint8_t group_idx,
                                         const uint8_t *cipher, size_t cipher_len,
                                         uint8_t *out, size_t *out_len);
p2p_sec_err_t p2p_security_add_group_key(p2p_security_t *ctx, const uint8_t group_key[P2P_SESSION_KEY_SIZE]);
 p2p_sec_err_t p2p_security_group_add(p2p_security_t *ctx,
                                     const uint8_t group_hash[16],
                                     const uint8_t group_key[P2P_SESSION_KEY_SIZE]);
p2p_sec_err_t p2p_security_group_find_key(p2p_security_t *ctx,
                                          const uint8_t group_hash[16],
                                          uint8_t out_group_key[P2P_SESSION_KEY_SIZE]);
p2p_sec_err_t p2p_security_group_remove(p2p_security_t *ctx, const uint8_t group_hash[16]);
void p2p_security_deinit(p2p_security_t *ctx);

p2p_sec_err_t p2p_security_derive_pubkey_from_privkey(const uint8_t privkey_in[P2P_NODE_KEY_SIZE],
                                                     uint8_t privkey_out[P2P_NODE_KEY_SIZE],
                                                     uint8_t pubkey_out[P2P_NODE_KEY_SIZE]);
p2p_sec_err_t p2p_security_generate_keypair(uint8_t privkey[P2P_NODE_KEY_SIZE],
                                            uint8_t pubkey[P2P_NODE_KEY_SIZE]);
p2p_sec_err_t p2p_security_random_fill(uint8_t *out, size_t len);
p2p_sec_err_t p2p_security_store_keys(const p2p_security_t *ctx);
p2p_sec_err_t p2p_security_load_keys(p2p_security_t *ctx, bool *loaded);
p2p_sec_err_t p2p_security_derive_session_key(const uint8_t local_privkey[P2P_NODE_KEY_SIZE],
                                              const uint8_t remote_pubkey[P2P_NODE_KEY_SIZE],
                                              uint8_t session_key[P2P_SESSION_KEY_SIZE]);
uint32_t p2p_security_count_authenticated(const p2p_security_t *ctx);

#endif

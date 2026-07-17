# Block 02 - Security

**Project:** micronet  
**License:** MIT  
**Target platforms:** ESP32, Linux, Windows  
**Language:** C99  
**Dependencies:** microcrypt, microdb_secure, microfsm

---

## Purpose

The security block provides end-to-end encryption, node authentication, and key management. It sits between the transport block, which handles raw packets, and the network block, which manages nodes and groups. No higher layer ever sees unencrypted network traffic.

---

## Responsibilities

- Generate the node keypair, public and private
- Run the handshake between two nodes by exchanging public keys
- Encrypt outgoing packets with AES-128-CBC
- Decrypt incoming packets
- Verify packet integrity with HMAC-SHA256
- Store keys securely with `microdb_secure`
- Manage group keys

---

## Not Handled Here

- Transport concerns such as sockets and IP addresses
- Node identity and groups, which belong to the network block
- Command routing and message semantics, which belong to the protocol block

---

## Key Architecture

Each node has:

- `node_privkey` - private key, never leaves the device
- `node_pubkey` - public key, shared with peers
- `session_key` - short-lived symmetric key for one connection
- `group_key[]` - encryption keys for the groups the node belongs to

```text
Node A                          Node B
  |                               |
  |-- [pubkey_A] ---------------->|
  |<---------------- [pubkey_B] --|
  |                               |
  |  session_key = derive(privkey_A, pubkey_B)
  |                               |
  |== [encrypted data] ==========>|
```

---

## Configuration

```c
typedef struct {
    uint8_t  node_privkey[32];     // node private key
    uint8_t  node_pubkey[32];      // node public key
    uint8_t  group_keys[P2P_MAX_GROUPS][16];
    uint8_t  group_count;          // number of groups
    bool     store_keys;           // store keys in microdb_secure
} p2p_security_config_t;
```

---

## Internal Data Structures

```c
typedef struct {
    uint8_t session_key[16];       // AES-128 session key
    uint8_t remote_pubkey[32];     // peer public key
    bool    authenticated;         // handshake complete
    uint32_t established_at;       // connection timestamp
} p2p_session_t;

typedef struct {
    uint8_t          node_privkey[32];
    uint8_t          node_pubkey[32];
    p2p_session_t    sessions[P2P_MAX_SESSIONS];
    uint8_t          group_keys[P2P_MAX_GROUPS][16];
    uint8_t          group_count;
    microfsm_t       fsm;
} p2p_security_t;
```

---

## Encrypted Packet

Every packet from the transport layer is wrapped like this:

```text
[ hmac 32B ][ iv 16B ][ encrypted_payload ... ]
```

| Field             | Size | Description |
|-------------------|------|-------------|
| hmac              | 32 B | HMAC-SHA256 over the whole packet |
| iv                | 16 B | random initialization vector |
| encrypted_payload | N B  | AES-128-CBC encrypted data |

---

## Handshake Protocol

```text
A                              B
|                              |
|---- HELLO [pubkey_A] ------->|
|<--- HELLO [pubkey_B] --------|
|                              |
|  Both derive session_key = HMAC(pubkey_A XOR pubkey_B)
|                              |
|---- VERIFY [hmac_A] -------->|
|<--- VERIFY [hmac_B] ---------|
|                              |
|========== link active =======|
```

---

## API

```c
// Initialize security, generating a keypair if needed
p2p_err_t p2p_security_init(p2p_security_t *ctx, const p2p_security_config_t *cfg);

// Read our public key
p2p_err_t p2p_security_get_pubkey(p2p_security_t *ctx, uint8_t pubkey[32]);

// Start a handshake with a peer
p2p_err_t p2p_security_handshake(p2p_security_t *ctx, const uint8_t remote_pubkey[32]);

// Check whether a peer connection is authenticated
bool p2p_security_is_authenticated(p2p_security_t *ctx, const uint8_t remote_pubkey[32]);

// Encrypt a packet before sending
p2p_err_t p2p_security_encrypt(p2p_security_t *ctx, const uint8_t remote_pubkey[32],
                                const uint8_t *plain, size_t plain_len,
                                uint8_t *out, size_t *out_len);

// Decrypt a received packet
p2p_err_t p2p_security_decrypt(p2p_security_t *ctx, const uint8_t remote_pubkey[32],
                                const uint8_t *cipher, size_t cipher_len,
                                uint8_t *out, size_t *out_len);

// Encrypt a group message
p2p_err_t p2p_security_encrypt_group(p2p_security_t *ctx, uint8_t group_idx,
                                      const uint8_t *plain, size_t plain_len,
                                      uint8_t *out, size_t *out_len);

// Decrypt a group message
p2p_err_t p2p_security_decrypt_group(p2p_security_t *ctx, uint8_t group_idx,
                                      const uint8_t *cipher, size_t cipher_len,
                                      uint8_t *out, size_t *out_len);

// Add a group key when invited to a group
p2p_err_t p2p_security_add_group_key(p2p_security_t *ctx, const uint8_t group_key[16]);

// Release resources
void p2p_security_deinit(p2p_security_t *ctx);
```

---

## State Machine

```text
IDLE
  -> HANDSHAKE_HELLO
       -> HANDSHAKE_VERIFY
            -> AUTHENTICATED -> link active
            -> FAILED        -> IDLE
```

---

## Error Codes

```c
typedef enum {
    P2P_SEC_OK              =  0,
    P2P_SEC_ERR_KEYGEN      = -1,  // key generation failed
    P2P_SEC_ERR_HANDSHAKE   = -2,  // handshake failed
    P2P_SEC_ERR_HMAC        = -3,  // HMAC check failed
    P2P_SEC_ERR_DECRYPT     = -4,  // decryption failed
    P2P_SEC_ERR_NO_SESSION  = -5,  // no session for the peer
    P2P_SEC_ERR_NO_GROUP    = -6,  // unknown group key
    P2P_SEC_ERR_BUF         = -7,  // buffer too small
} p2p_sec_err_t;
```

---

## Test Plan

### Test 01 - Keypair generation
- Initialize a security context without existing keys
- Verify that `node_pubkey` is not all zeros
- Verify that `node_privkey != node_pubkey`
- **Expected result:** `P2P_SEC_OK`, unique keys

### Test 02 - Handshake between two nodes
- Create two security contexts, A and B
- Exchange public keys and run the handshake
- Verify that both sides end up with the same `session_key`
- **Expected result:** `P2P_SEC_OK`, `is_authenticated == true`

### Test 03 - Encryption and decryption
- Encrypt a message from A to B
- Decrypt it on B
- Verify that the data matches
- **Expected result:** matching data

### Test 04 - Tamper detection
- Encrypt a message, then flip one byte in the ciphertext
- Try to decrypt it
- **Expected result:** `P2P_SEC_ERR_HMAC`

### Test 05 - Group encryption
- Add the same group key to two contexts
- Encrypt a group message
- Decrypt it on the other side
- **Expected result:** matching data

### Test 06 - Unknown group
- Try to decrypt a group message without the group key
- **Expected result:** `P2P_SEC_ERR_NO_GROUP`

### Test 07 - Persistent keys
- Initialize with `store_keys = true`
- Deinitialize and initialize again
- Verify that `node_pubkey` stayed the same
- **Expected result:** keys survive reboot

# Block 05 - Protocol

**Project:** micronet  
**License:** MIT  
**Target platforms:** ESP32, Linux, Windows  
**Language:** C99  
**Dependencies:** microfsm, microbus, microcodec

---

## Purpose

The protocol block defines the wire format for micronet messages and routes them to the correct internal handlers. It knows nothing about sockets or encryption details; it only serializes, deserializes, and dispatches protocol frames.

---

## Responsibilities

- Define message types and payload layouts
- Serialize outgoing messages
- Parse incoming messages
- Route packets to transport, security, network, and data handlers
- Keep the message state machine consistent across platforms

---

## Message Types

```c
typedef enum {
    P2P_MSG_HELLO           = 0x01,
    P2P_MSG_WELCOME         = 0x02,
    P2P_MSG_HEARTBEAT       = 0x03,
    P2P_MSG_GOODBYE         = 0x04,

    // Security
    P2P_MSG_SEC_HELLO       = 0x10,
    P2P_MSG_SEC_VERIFY      = 0x11,
    P2P_MSG_SYNC_DATA       = 0x12,

    // Data
    P2P_MSG_DATA_REQUEST    = 0x20,
    P2P_MSG_DATA_RESPONSE   = 0x21,
    P2P_MSG_DATA_NOTIFY     = 0x22,
    P2P_MSG_DATA_SUBSCRIBE  = 0x23,
    P2P_MSG_DATA_UNSUB      = 0x24,
    P2P_MSG_DATA_QUERY      = 0x25,
    P2P_MSG_DATA_QUERY_RESP = 0x26,
    P2P_MSG_DATA_METRICS    = 0x27,
    P2P_MSG_DATA_METRICS_R  = 0x28,
    P2P_MSG_DATA_LIST       = 0x29,
    P2P_MSG_DATA_LIST_R     = 0x2A,
} p2p_msg_type_t;
```

---

## Payload Format

Each message type uses a compact CBOR payload. The protocol layer only enforces the outer frame and dispatch rules.

```text
[ magic ][ version ][ type ][ flags ][ seq ][ payload_len ][ payload ]
```

- `magic` identifies micronet traffic
- `version` protects against incompatible wire changes
- `type` selects the handler
- `flags` carry routing and reliability bits
- `seq` allows retries and ordering
- `payload_len` limits the payload size

---

## Routing

The protocol block sends each decoded message to the appropriate handler:

- transport ACK and heartbeat messages
- security handshake and verify messages
- network gossip and membership messages
- data request, response, query, list, metrics, and notification messages

If a message type is unknown, the frame is rejected with an error.

---

## API

```c
// Initialize protocol state
p2p_err_t p2p_protocol_init(p2p_protocol_t *ctx);

// Build an outgoing frame
p2p_err_t p2p_protocol_pack(p2p_protocol_t *ctx, p2p_msg_type_t type,
                            const uint8_t *payload, size_t payload_len,
                            uint8_t *out, size_t *out_len);

// Decode an incoming frame
p2p_err_t p2p_protocol_unpack(p2p_protocol_t *ctx, const uint8_t *frame,
                              size_t frame_len, p2p_msg_type_t *out_type,
                              uint8_t *out_payload, size_t *out_payload_len);

// Dispatch a decoded frame to the correct subsystem
p2p_err_t p2p_protocol_dispatch(p2p_protocol_t *ctx, const uint8_t *frame,
                                size_t frame_len);

// Release resources
void p2p_protocol_deinit(p2p_protocol_t *ctx);
```

---

## State Machine

```text
IDLE
  -> PACKING
       -> SENT
       -> RECEIVED
       -> DISPATCHED
       -> IDLE
```

---

## Error Codes

```c
typedef enum {
    P2P_PROTO_OK            =  0,
    P2P_PROTO_ERR_MAGIC     = -1,  // invalid magic
    P2P_PROTO_ERR_VERSION   = -2,  // unsupported version
    P2P_PROTO_ERR_TYPE      = -3,  // unknown message type
    P2P_PROTO_ERR_FRAME     = -4,  // malformed frame
    P2P_PROTO_ERR_BUF       = -5,  // output buffer too small
} p2p_proto_err_t;
```

---

## Test Plan

### Test 01 - Serialize and parse
- Pack a message with a known payload
- Parse the frame back
- **Expected result:** payload and type match exactly

### Test 02 - Unknown type
- Feed an unsupported message type
- **Expected result:** `P2P_PROTO_ERR_TYPE`

### Test 03 - Corrupt magic
- Modify the first bytes of a valid frame
- **Expected result:** `P2P_PROTO_ERR_MAGIC`

### Test 04 - Dispatch routing
- Send a data request frame
- **Expected result:** the data handler is invoked

### Test 05 - Retry integration
- Verify that the protocol layer emits retryable frames with the expected sequence number
- **Expected result:** retry logic receives the correct metadata

### Test 06 - State transitions
- Run a normal boot sequence: boot -> init -> stun -> ready
- **Expected result:** all expected state transitions occur

---

## Notes

- Message payloads are intentionally compact so that the same frames work well on embedded devices and desktop builds
- The protocol block depends on the surrounding layers for payload meaning and security

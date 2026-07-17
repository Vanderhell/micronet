# Block 01 - Transport

**Project:** micronet  
**License:** MIT  
**Target platforms:** ESP32, Linux, Windows  
**Language:** C99  
**Dependencies:** microring, microcodec, microres, microtimer

---

## Purpose

The transport block is the lowest active layer in the library. It handles raw UDP packet delivery between two nodes, STUN-based public address discovery, and basic packet buffering. Higher layers always talk to transport through its API, never directly to sockets.

---

## Responsibilities

- Open and manage a UDP socket
- Discover the public IP and port through STUN
- Send and receive raw packets
- Buffer outgoing and incoming packets with `microring`
- Compress packets before sending with `microcodec`
- Retry lost packets with `microres`
- Run a heartbeat timer to detect link loss with `microtimer`

---

## Not Handled Here

- Encryption, which belongs to the security block
- Node identity, which belongs to the network block
- Packet semantics, which belong to the protocol block

---

## Configuration

```c
typedef struct {
    const char *stun_host;       // e.g. "stun.l.google.com"
    uint16_t    stun_port;       // e.g. 19302
    uint16_t    local_port;      // local UDP port (0 = auto)
    uint32_t    heartbeat_ms;    // heartbeat interval in ms
    uint32_t    timeout_ms;      // connection timeout in ms
    uint8_t     retry_count;     // retry attempts for lost packets
    uint32_t    retry_delay_ms;  // delay between retry attempts
    size_t      rx_buf_size;     // receive buffer size
    size_t      tx_buf_size;     // transmit buffer size
} p2p_transport_config_t;
```

---

## Internal Data Structures

```c
typedef struct {
    uint8_t  data[P2P_MAX_PACKET_SIZE];
    size_t   len;
    uint32_t timestamp;
    uint8_t  remote_ip[4];
    uint16_t remote_port;
} p2p_packet_t;

typedef struct {
    int          sock_fd;
    uint8_t      external_ip[4];
    uint16_t     external_port;
    bool         stun_resolved;
    microring_t  rx_ring;
    microring_t  tx_ring;
    microtimer_t heartbeat_timer;
    microtimer_t timeout_timer;
    microres_t   retry_ctx;
} p2p_transport_t;
```

---

## Packet Header

Every outgoing packet starts with a small transport header:

```text
[ magic 2B ][ version 1B ][ flags 1B ][ seq 2B ][ len 2B ][ payload ... ]
```

| Field   | Size | Description |
|---------|------|-------------|
| magic   | 2 B  | `0xP2 0xLB`, protocol identifier |
| version | 1 B  | protocol version, currently `0x01` |
| flags   | 1 B  | `ACK` / `HEARTBEAT` / `COMPRESSED` / ... |
| seq     | 2 B  | packet sequence number |
| len     | 2 B  | payload length in bytes |
| payload | N B  | data, optionally compressed |

---

## API

```c
// Initialize the transport block
p2p_err_t p2p_transport_init(p2p_transport_t *ctx, const p2p_transport_config_t *cfg);

// Resolve the public address through STUN
p2p_err_t p2p_transport_stun_resolve(p2p_transport_t *ctx);

// Read the public IP and port
p2p_err_t p2p_transport_get_external_addr(p2p_transport_t *ctx, uint8_t ip[4], uint16_t *port);

// Send a packet to a destination address
p2p_err_t p2p_transport_send(p2p_transport_t *ctx, const uint8_t ip[4], uint16_t port,
                              const uint8_t *data, size_t len);

// Receive a packet in non-blocking mode
p2p_err_t p2p_transport_recv(p2p_transport_t *ctx, p2p_packet_t *out_packet);

// Tick function, call periodically for heartbeat and retries
p2p_err_t p2p_transport_tick(p2p_transport_t *ctx);

// Release resources
void p2p_transport_deinit(p2p_transport_t *ctx);
```

---

## State Machine

```text
IDLE
  -> STUN_RESOLVING -> STUN_DONE
                         -> LISTENING
                               -> SENDING
                               -> RECEIVING
                               -> TIMEOUT -> IDLE
```

---

## Error Codes

```c
typedef enum {
    P2P_OK              =  0,
    P2P_ERR_SOCK        = -1,  // socket error
    P2P_ERR_STUN        = -2,  // STUN failed
    P2P_ERR_TIMEOUT     = -3,  // connection timeout
    P2P_ERR_RETRY       = -4,  // retry attempts exhausted
    P2P_ERR_BUF_FULL    = -5,  // buffer full
    P2P_ERR_BAD_PACKET  = -6,  // invalid header
    P2P_ERR_INVALID_ARG = -7,  // invalid argument
} p2p_err_t;
```

---

## Test Plan

### Test 01 - STUN resolve
- Initialize transport with `stun.l.google.com:19302`
- Call `p2p_transport_stun_resolve()`
- Verify that `external_ip` is not `0.0.0.0` and the port is not `0`
- **Expected result:** `P2P_OK`, valid public address

### Test 02 - Packet send and receive (loopback)
- Open two transport contexts on `127.0.0.1`
- Send a packet from A to B
- Verify that B received the correct data, the correct length, and a valid sequence number
- **Expected result:** `P2P_OK`, matching data

### Test 03 - Compression
- Send a packet with repetitive data that benefits from RLE
- Verify that the `COMPRESSED` flag is set
- Verify that the receiver decrypted it correctly
- **Expected result:** matching data, smaller packet

### Test 04 - Retry on loss
- Simulate packet loss by dropping every second packet
- Verify that the retry mechanism delivers the packet
- **Expected result:** `P2P_OK` after retry

### Test 05 - Heartbeat timeout
- Initialize with `timeout_ms = 1000`
- Do not call tick for 2 seconds
- Verify that the state transitions to `TIMEOUT`
- **Expected result:** `P2P_ERR_TIMEOUT`

### Test 06 - Buffer full
- Fill the RX ring buffer
- Try to receive another packet
- **Expected result:** `P2P_ERR_BUF_FULL`

---

## Platform HAL

To keep the code portable across ESP32 and Linux/Windows, socket operations are isolated behind a HAL layer:

```c
typedef struct {
    int  (*sock_open)(uint16_t port);
    void (*sock_close)(int fd);
    int  (*sock_send)(int fd, const uint8_t *ip, uint16_t port,
                      const uint8_t *data, size_t len);
    int  (*sock_recv)(int fd, uint8_t *ip, uint16_t *port,
                      uint8_t *buf, size_t buf_len);
    uint32_t (*now_ms)(void);
} p2p_hal_t;
```

ESP32 and Linux each provide their own implementation of this structure. The rest of the code is shared.

---

## Files

```text
micronet/
`-- src/
    `-- transport/
        |-- p2p_transport.h
        |-- p2p_transport.c
        |-- p2p_transport_stun.c
        |-- p2p_hal.h
        |-- p2p_hal_linux.c
        |-- p2p_hal_esp32.c
        `-- tests/
            `-- test_transport.c
```

---

## Notes

- `p2p_transport_tick()` must be called regularly
- On ESP32, we recommend calling tick from a FreeRTOS task or timer loop
- Maximum packet size is constrained by `P2P_MAX_PACKET_SIZE`
- STUN resolution runs at startup and periodically after that

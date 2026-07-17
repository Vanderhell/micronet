# Block 04 - Data

**Project:** micronet  
**License:** MIT  
**Target platforms:** ESP32, Linux, Windows  
**Language:** C99  
**Dependencies:** microdb, microbus, microcodec, microfsm

---

## Purpose

The data block stores application variables and exposes them to other nodes. It is responsible for publish/subscribe behavior, request/response lookups, metrics, list operations, and the basic replicated data model used by micronet.

---

## Responsibilities

- Store local variables in a small replicated database
- Publish updates to connected peers
- Serve point queries through request/response
- Return variable lists
- Publish metrics and counters
- Handle subscriptions and notifications
- Compress payloads when it helps reduce bandwidth

---

## Not Handled Here

- Physical transport, which belongs to the transport block
- Encryption, which belongs to the security block
- Node membership, which belongs to the network block
- Packet framing, which belongs to the protocol block

---

## Data Model

The data layer works with named keys and typed values:

- strings
- integers
- booleans
- binary blobs
- arrays and compact structured payloads where needed

Each item has a key, a value, a version counter, and metadata that allows replication and synchronization.

---

## Configuration

```c
typedef struct {
    uint8_t  max_vars;             // maximum number of variables
    uint8_t  max_metrics;          // maximum number of metric entries
    uint8_t  max_subscribers;      // maximum number of subscriptions
    bool     compress_data;        // compress payloads when possible
    bool     store_data;            // persist data locally
} p2p_data_config_t;
```

---

## Internal Data Structures

```c
typedef struct {
    char     key[P2P_MAX_KEY_LEN];
    uint8_t  type;
    uint8_t  value[P2P_MAX_VALUE_LEN];
    size_t   value_len;
    uint32_t version;
    uint32_t updated_at;
} p2p_var_t;

typedef struct {
    char     key[P2P_MAX_KEY_LEN];
    uint32_t count;
    uint32_t min;
    uint32_t max;
    uint32_t sum;
} p2p_metric_t;
```

---

## Message Flow

The data layer uses the protocol block for message routing:

- `publish` updates a key locally and sends the change to peers
- `request` asks another node for one key
- `list` returns the available keys
- `metrics` returns counters and aggregates
- `subscribe` registers interest in a key
- `query` provides a direct read-only lookup path

Subscriptions trigger notifications whenever a key changes.

---

## API

```c
// Initialize the data block
p2p_err_t p2p_data_init(p2p_data_t *ctx, const p2p_data_config_t *cfg);

// Publish or update a variable
p2p_err_t p2p_data_publish(p2p_data_t *ctx, const char *key,
                            const uint8_t *value, size_t len);

// Read a local variable
p2p_err_t p2p_data_get(p2p_data_t *ctx, const char *key,
                        uint8_t *out, size_t *out_len);

// Handle a request from another node
p2p_err_t p2p_data_handle_request(p2p_data_t *ctx, const char *key,
                                  uint8_t *out, size_t *out_len);

// Return the list of known keys
p2p_err_t p2p_data_list(p2p_data_t *ctx, char out_keys[][P2P_MAX_KEY_LEN],
                        uint8_t *count);

// Return metrics
p2p_err_t p2p_data_metrics(p2p_data_t *ctx, p2p_metric_t *out_metrics,
                           uint8_t *count);

// Subscribe to changes for a key
p2p_err_t p2p_data_subscribe(p2p_data_t *ctx, const char *key);

// Handle an incoming notification
p2p_err_t p2p_data_on_notify(p2p_data_t *ctx, const char *key,
                             const uint8_t *value, size_t len);

// Process periodic maintenance
p2p_err_t p2p_data_tick(p2p_data_t *ctx);

// Release resources
void p2p_data_deinit(p2p_data_t *ctx);
```

---

## Events

```c
P2P_EVENT_DATA_PUBLISHED    // local key updated
P2P_EVENT_DATA_REQUEST_IN   // another node requested a value
P2P_EVENT_DATA_REQUEST_OUT  // we requested a value from another node
P2P_EVENT_DATA_NOTIFY       // subscription notification received
P2P_EVENT_DATA_METRICS      // metrics snapshot generated
```

---

## State Machine

```text
IDLE
  -> SYNCING
       -> ACTIVE
            -> PUBLISHING
            -> REQUESTING
            -> NOTIFYING
            -> ACTIVE
```

---

## Error Codes

```c
typedef enum {
    P2P_DATA_OK               =  0,
    P2P_DATA_ERR_KEY_NOTFOUND = -1,  // key does not exist
    P2P_DATA_ERR_FULL         = -2,  // storage full
    P2P_DATA_ERR_TYPE         = -3,  // type mismatch
    P2P_DATA_ERR_BUF          = -4,  // buffer too small
    P2P_DATA_ERR_SYNC         = -5,  // synchronization failed
} p2p_data_err_t;
```

---

## Test Plan

### Test 01 - Publish and read back
- Initialize the data layer
- Publish `temperature = 25`
- Read it back locally
- **Expected result:** same value

### Test 02 - Request/response
- Node A requests `temperature` from node B
- B returns the value
- **Expected result:** request succeeds and the payload matches

### Test 03 - List keys
- Publish several keys
- Call the list API
- **Expected result:** the returned key list contains all published keys

### Test 04 - Metrics
- Increment a metric multiple times
- Query the metric snapshot
- **Expected result:** count, min, max, and sum are correct

### Test 05 - Subscribe and notify
- Subscribe to a key
- Update the key
- **Expected result:** notification event emitted

### Test 06 - Compression
- Publish a repetitive payload
- Verify that compression activates when it is beneficial
- **Expected result:** successful publish, smaller encoded message

---

## Files

```text
micronet/
`-- src/
    `-- data/
        |-- p2p_data.h
        |-- p2p_data.c
        |-- p2p_data_vars.c
        |-- p2p_data_query.c
        |-- p2p_data_metrics.c
        `-- tests/
            `-- test_data.c
```

---

## Notes

- Compression through `microcodec` is enabled automatically when it reduces payload size
- Variable and metric counts are intentionally small to keep the embedded footprint predictable
- The data model is designed to work with the protocol and network layers without exposing transport details

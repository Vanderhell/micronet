# Block 06 - Public API

**Project:** micronet  
**License:** MIT  
**Target platforms:** ESP32, Linux, Windows  
**Language:** C99  
**Dependencies:** transport, security, network, data, protocol

---

## Purpose

The public API block is the stable entry point for applications. It glues the internal subsystems together, exposes initialization and tick functions, and provides a small set of node, group, and data operations for users of the library.

---

## Initialization

The minimum setup sequence is:

1. Configure the transport layer
2. Initialize security and identity
3. Initialize the network layer
4. Initialize the data layer
5. Initialize the protocol layer
6. Call the top-level `micronet` API

### Minimal ESP32 Example

```c
micronet_config_t cfg = {
    .device_name = "esp32-node",
    .wifi_ssid = "your-ssid",
    .wifi_password = "your-password",
};

micronet_init(&cfg);
```

---

## Main API Areas

### Node and identity

- read the node ID
- read the public key
- inspect local identity metadata
- regenerate identity when onboarding needs to be reset

### Groups

- create a group
- join or leave a group
- list group members
- inspect trust chains and group metadata

### Transport and connectivity

- start and stop the stack
- resolve the public address
- send packets to a peer
- query link state and peer availability

### Data

- publish variables
- query a single key
- list keys
- request metrics
- subscribe to updates

### Debug and diagnostics

- print state summaries
- inspect local tables
- expose event hooks for higher-level tooling

---

## Example Usage

```c
// Initialize the stack
p2p_err_t err = micronet_init(&cfg);

// Read the node identity
uint8_t node_id[32];
micronet_get_node_id(node_id);

// Publish a value
micronet_data_publish("temperature", (const uint8_t *)"25", 2);

// Send a packet to a peer
micronet_send_to_peer(peer_id, payload, payload_len);
```

---

## Error Handling

Public APIs return one of the shared `P2P_*` error families. Callers should check every result and treat transport, security, and protocol errors separately when they need more detail.

---

## Callback Model

Applications can register callbacks for:

- peer online/offline changes
- data updates
- group membership changes
- message reception
- diagnostics output

Callbacks are intentionally small and synchronous so they can run on embedded targets without extra infrastructure.

---

## Test Plan

### Test 01 - Node ID
- Initialize the stack
- Read the node ID
- **Expected result:** `micronet_get_node_id()` returns a valid ID

### Test 02 - Group membership
- Create a group
- Join it from a second node
- **Expected result:** the member list contains both nodes

### Test 03 - Peer online callback
- Bring a peer online
- **Expected result:** `on_node_online` is called

### Test 04 - Data publish
- Publish a value through the public API
- **Expected result:** the value is visible through `get` and `list`

### Test 05 - Full stack tick
- Run the main tick loop for a while
- **Expected result:** transport, security, network, and data all stay in sync

---

## Notes

- The public API is the compatibility boundary for the rest of the project
- Internal files may change over time, but this layer should remain stable for application code
- ESP32 and desktop builds use the same API names so tests and demos stay portable

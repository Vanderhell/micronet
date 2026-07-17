# Block 03 - Network

**Project:** micronet  
**License:** MIT  
**Target platforms:** ESP32, Linux, Windows  
**Language:** C99  
**Dependencies:** microdb, microdb_secure, microbus, microtimer, microfsm

---

## Purpose

The network block manages node identity, group membership, gossip propagation, and the web of trust. It acts as the directory for the whole mesh: it knows who exists, which groups are active, and who invited whom.

---

## Responsibilities

- Manage the local node database with `microdb`
- Manage groups, including hash, key, and membership lists
- Propagate node discovery through a gossip protocol
- Track the web of trust chain
- Synchronize databases between nodes
- Track online and offline state
- Handle group invitations

---

## Not Handled Here

- Physical transport, which belongs to the transport block
- Encryption, which belongs to the security block
- Message and command semantics, which belong to the protocol block

---

## Data Structures

```c
// Node in the network
typedef struct {
    uint8_t  node_id[32];          // public key = node identity
    uint8_t  external_ip[4];       // last known public IP
    uint16_t external_port;        // last known public port
    uint8_t  invited_by[32];       // who invited this node (0 = founder)
    uint32_t first_seen;           // first contact timestamp
    uint32_t last_seen;            // last heartbeat timestamp
    uint8_t  group_hashes[P2P_MAX_GROUPS][16];
    uint8_t  group_count;          // number of groups
    bool     is_online;            // current state
    uint32_t db_version;           // record version for sync
} p2p_node_t;

// Group
typedef struct {
    uint8_t  group_hash[16];       // public group identifier
    uint8_t  group_key[16];        // secret group encryption key
    uint8_t  created_by[32];       // founder node_id
    uint32_t created_at;           // creation timestamp
    uint8_t  members[P2P_MAX_MEMBERS][32];
    uint8_t  member_count;         // number of members
    uint32_t db_version;           // record version for sync
} p2p_group_t;

// Main network context
typedef struct {
    p2p_node_t   self;
    p2p_node_t   nodes[P2P_MAX_NODES];
    uint8_t      node_count;
    p2p_group_t  groups[P2P_MAX_GROUPS];
    uint8_t      group_count;
    microfsm_t   fsm;
    microtimer_t gossip_timer;
    microtimer_t sync_timer;
} p2p_network_t;
```

---

## Configuration

```c
typedef struct {
    uint32_t gossip_interval_ms;   // how often to propagate gossip
    uint32_t sync_interval_ms;     // how often to synchronize DB state
    uint32_t offline_timeout_ms;   // node is offline after this many ms
    uint8_t  max_nodes;            // maximum number of nodes
    uint8_t  max_groups;           // maximum number of groups
} p2p_network_config_t;
```

---

## Gossip Protocol

Each node periodically sends known peers information about itself and any newly discovered nodes.

```text
Node A discovers node G:
  1. Add G to the local DB
  2. Send a GOSSIP message to B, C, D, E, F:
     { type: GOSSIP_NEW_NODE, node: G, invited_by: A, version: X }
  3. B, C, D, E, F add G to their DBs
  4. G receives gossip about the rest of the mesh
```

The gossip message carries only deltas, never the full database.

---

## Group Invitation

```text
Node A invites node B to a group:
  1. A calls p2p_network_group_invite(node_B_id, group_hash)
  2. The library encrypts group_key with B's public key
  3. It sends an INVITE message:
     { type: GROUP_INVITE, group_hash, encrypted_group_key, invited_by: A }
  4. B decrypts group_key with its private key
  5. B calls p2p_network_group_join(group_hash) to accept the invite
  6. Gossip tells the other group members that B joined
```

---

## Database Synchronization

Every record has a `db_version` value, which is a monotonically increasing number plus an author signature.

When two nodes meet:

1. They exchange version lists
2. They request the records they are missing
3. Newer versions win using last-write-wins
4. The security block verifies signatures, so no node can forge someone else's records

---

## API

```c
// Initialize the network block
p2p_err_t p2p_network_init(p2p_network_t *ctx, const p2p_network_config_t *cfg,
                            const uint8_t self_node_id[32]);

// Add a node to the local DB on first contact
p2p_err_t p2p_network_add_node(p2p_network_t *ctx, const p2p_node_t *node);

// Find a node by node_id
p2p_err_t p2p_network_find_node(p2p_network_t *ctx, const uint8_t node_id[32],
                                 p2p_node_t *out);

// Update a node's online state
p2p_err_t p2p_network_set_online(p2p_network_t *ctx, const uint8_t node_id[32], bool online);

// Create a new group
p2p_err_t p2p_network_group_create(p2p_network_t *ctx, uint8_t out_group_hash[16]);

// Invite a node to a group
p2p_err_t p2p_network_group_invite(p2p_network_t *ctx, const uint8_t node_id[32],
                                    const uint8_t group_hash[16]);

// Accept a group invitation
p2p_err_t p2p_network_group_join(p2p_network_t *ctx, const uint8_t group_hash[16],
                                  const uint8_t group_key[16]);

// Leave a group
p2p_err_t p2p_network_group_leave(p2p_network_t *ctx, const uint8_t group_hash[16]);

// List members of a group
p2p_err_t p2p_network_group_members(p2p_network_t *ctx, const uint8_t group_hash[16],
                                     uint8_t out_members[][32], uint8_t *count);

// Tick function for gossip, sync, and online/offline tracking
p2p_err_t p2p_network_tick(p2p_network_t *ctx);

// Handle an incoming gossip message
p2p_err_t p2p_network_on_gossip(p2p_network_t *ctx, const uint8_t *msg, size_t len);

// Release resources
void p2p_network_deinit(p2p_network_t *ctx);
```

---

## Events

The network block publishes events that higher layers can subscribe to:

```c
P2P_EVENT_NODE_ONLINE      // node came online
P2P_EVENT_NODE_OFFLINE     // node went offline
P2P_EVENT_NODE_NEW         // new node discovered through gossip
P2P_EVENT_GROUP_INVITE     // group invitation
P2P_EVENT_GROUP_JOINED     // node joined a group
P2P_EVENT_GROUP_LEFT       // node left a group
P2P_EVENT_DB_SYNCED        // database synchronization finished
```

---

## State Machine

```text
IDLE
  -> JOINING          (first contact, node exchange)
       -> ACTIVE
            -> GOSSIPING   (periodic propagation)
            -> SYNCING     (database synchronization)
            -> ISOLATED    (no peer available)
                 -> ACTIVE (peer found again)
```

---

## Error Codes

```c
typedef enum {
    P2P_NET_OK               =  0,
    P2P_NET_ERR_NODE_EXISTS  = -1,  // node already exists in DB
    P2P_NET_ERR_NODE_FULL    = -2,  // DB full, max_nodes reached
    P2P_NET_ERR_NOT_FOUND    = -3,  // node not found
    P2P_NET_ERR_NOT_MEMBER   = -4,  // not a group member
    P2P_NET_ERR_GROUP_FULL   = -5,  // group full
    P2P_NET_ERR_NO_INVITE    = -6,  // no permission to invite
    P2P_NET_ERR_SYNC         = -7,  // synchronization failed
} p2p_net_err_t;
```

---

## Test Plan

### Test 01 - Add node
- Create a network context
- Add a node with a valid `node_id`
- Find it again by `node_id`
- **Expected result:** `P2P_NET_OK`, record found

### Test 02 - Online/offline detection
- Add a node and set `is_online = true`
- Wait longer than `offline_timeout_ms` without a heartbeat
- Call tick
- **Expected result:** `P2P_EVENT_NODE_OFFLINE` published

### Test 03 - Create group
- Call `p2p_network_group_create()`
- Verify that `group_hash` is not all zeros
- **Expected result:** `P2P_NET_OK`, valid hash

### Test 04 - Invite and join
- Node A creates a group
- A invites B with `p2p_network_group_invite`
- B accepts with `p2p_network_group_join`
- Verify that B is in the member list
- **Expected result:** `P2P_NET_OK`, B in the group

### Test 05 - Gossip propagation
- Nodes A and B are connected
- A discovers a new node C
- A runs gossip
- Verify that B now knows C
- **Expected result:** C appears in B's DB

### Test 06 - Database sync
- A and B have different record versions
- Run sync
- Verify that both sides end with matching records
- **Expected result:** matching DB state

### Test 07 - Web of trust
- A invited B, B invited C
- Verify that the `invited_by` chain is A -> B -> C
- **Expected result:** correct trust chain

### Test 08 - Leave group
- B is a member of a group
- B calls `p2p_network_group_leave()`
- Verify that B is no longer in the member list
- Verify that other nodes received `P2P_EVENT_GROUP_LEFT`
- **Expected result:** B removed, event published

---

## Files

```text
micronet/
`-- src/
    `-- network/
        |-- p2p_network.h
        |-- p2p_network.c
        |-- p2p_network_gossip.c
        |-- p2p_network_group.c
        |-- p2p_network_sync.c
        `-- tests/
            `-- test_network.c
```

---

## Notes

- Gossip deltas contain only records newer than the last synchronization, which saves bandwidth
- On ESP32, `P2P_MAX_NODES` is limited by available RAM, so 32 nodes is a practical upper bound
- `group_hash` is `HMAC-SHA256(group_key + created_at)[0..15]`, so it is deterministic and derived from the key
- A member can invite another node only to a group that the inviter already belongs to
- The database version is a `uint32_t`; on overflow it resets and a full synchronization runs

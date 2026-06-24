# Architecture

Micronet is organized as a small C99 stack:

- `transport`: UDP packet handling, retries, heartbeat, optional STUN
- `security`: node identity and session/authentication helpers
- `network`: peer table, groups, online/offline tracking, gossip/sync state
- `data`: replicated variables, requests, subscriptions, metrics
- `protocol`: packet serialization, dispatch, routing, fanout
- `micronet.c`: public API glue and error mapping

## Runtime Model

- `mnet_init()` creates one process-global context.
- Peer state lives in the network table, not in a single last-peer slot.
- Group membership is stored per peer and in group membership lists.
- Broadcast and group fanout iterate eligible peers.

## Constraints

- Fixed-size tables are used for peers, groups, pending messages, and payload buffers.
- LAN mode does not require DNS, cloud services, public IPs, or port forwarding.
- STUN is optional and disabled by default.

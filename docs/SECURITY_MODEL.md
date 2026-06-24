# Security Model

Micronet currently uses per-node identity and session authentication in the existing security layer.

## What Exists

- 32-byte node identities
- session authentication and encrypted packet handling for authenticated peers
- LAN discovery packets are parsed as untrusted input and do not authorize peers by themselves
- group membership used for routing, not as a security boundary by itself

## What Is Not Verified

- production-grade threat resistance
- formal key management review
- complete replay protection proof across every path

## Operational Rules

- authenticated peers can use the existing encrypted protocol path
- LAN mode does not require internet reachability
- STUN is not part of the security baseline and is disabled by default

## Limitations

- the implementation is not a full access-control system
- group membership determines fanout selection, not cryptographic trust by itself
- placeholder ids from `mnet_peer_add_ip(NULL, ...)` are not device identities
- any stronger security claim would need additional verification and tests

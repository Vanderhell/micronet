# Keys and Hashes

**Project:** micronet  
**License:** MIT  
**Target platforms:** ESP32, Linux, Windows  
**Language:** C99

---

## Purpose

This document explains the identity and group identifiers used by micronet, how they are derived, and how to inspect them during debugging and testing.

---

## Node Identity

Every device has a persistent 32-byte node identity:

- the node ID is derived from the node public key
- the private key stays local to the device
- the public key is shared with peers and used as the node identity

Typical debug output prints a shortened node ID:

```text
node_id: 4a7b3c8d2e9f1a6b...
```

---

## Group Hashes

Groups use a short public identifier called `group_hash`.

Properties:

- deterministic for the same inputs
- derived from the group key and creation timestamp
- short enough for logs, QR codes, and serial output

Example:

```text
group_hash: 3b2a1f0e...
```

---

## Group Keys

The `group_key` is the secret value used to encrypt group traffic.

Rules:

- only group members should know it
- it is exchanged through the invitation flow
- it must not be printed in production logs

Debug output may show a masked or shortened version when explicitly enabled for local testing.

---

## Public Address Data

Micronet may also display the public IP address and port discovered through STUN. These values are not secrets, but they are important when debugging NAT traversal and peer discovery.

---

## Verification Checklist

- confirm that the node ID is stable across reboot
- confirm that `group_hash` is stable for the same group definition
- confirm that `group_key` is identical across members
- confirm that no private key material is written to normal logs

---

## File References

Relevant code lives in:

- `include/micronet.h`
- `src/security/`
- `src/network/`
- `src/data/`
- `arduino/micronet/src/`


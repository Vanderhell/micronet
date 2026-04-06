# Arduino Micronet Port

Minimal ESP32-focused Arduino port of the `micronet` identity, security, transport, and wrapper layers.

## What Is Here

- `src/mcrypt.*`
  - portable crypto primitives copied for Arduino build use
- `src/mdh.*`
  - X25519 key exchange copied for Arduino build use
- `src/mnet_identity.*`
  - persistent node identity backed by ESP32 NVS
- `src/p2p_security*`
  - session and group crypto adapted for Arduino
- `src/mnet_arduino.*`
  - small Arduino-facing wrapper over identity and security
- `src/mnet_transport.*`
  - poll-based UDP transport over `WiFiUDP` with simple STUN probing

## Current Scope

This is not yet full parity with the desktop C library.

Implemented now:

- persistent 32-byte node identity
- real X25519 keypair generation
- session handshake and peer encryption
- group encryption helpers
- UDP send/receive with encrypted payloads
- simple STUN external address probe
- minimal custom message protocol framing and dispatch
- minimal RAM-backed data layer with `publish`, `request`, `list`, `metrics`, and `subscribe`

Still missing for full parity:

- full mesh membership and gossip
- richer replicated data API
- `query` runtime
- full protocol/state machine parity with desktop `micronet`

## How To Use In Arduino IDE

Two ways work:

1. Open examples directly from this repo.
   - the examples use relative includes like `../../src/...`
   - this works even if the library is not installed into Arduino `libraries/`

2. Install `arduino/micronet` as a normal Arduino library.
   - then you can switch examples back to angle-bracket includes if desired

## Example Sketches

- `examples/identity_test`
  - prints persistent `node_id` and checks it survives reboot
- `examples/security_test`
  - local self-test for handshake, peer crypto, and group crypto
- `examples/mneta_wrapper_test`
  - wrapper-level test for identity, handshake, and group helpers
- `examples/transport_test`
  - two-node encrypted UDP send/receive over Wi-Fi
- `examples/protocol_test`
  - two-node custom message dispatch over encrypted transport
- `examples/data_test`
  - two-node `publish/request/list/metrics/subscribe` test over encrypted transport

## Notes

- Target platform is `ESP32` / `ESP32-S3`
- Storage uses `Preferences` NVS
- RNG uses `esp_fill_random()`
- Runtime is poll-based, not FreeRTOS-task based

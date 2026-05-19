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
- minimal RAM-backed data layer with `publish`, `request`, `list`, `metrics`, `subscribe`, and `query`

Still missing for full parity:

- full mesh membership and gossip
- richer replicated data API
- `query` runtime
- full protocol/state machine parity with desktop `micronet`

## How To Use In Arduino IDE

Two ways work:

1. Open examples directly from this repo.
   - the examples use `mnet_bundle.h` with `MNET_ARDUINO_IMPLEMENTATION`
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
  - two-node `publish/request/list/metrics/subscribe/query` test over encrypted transport
- `examples/all_in_one_port_test`
  - one sketch that exercises identity, protocol, transport, STUN, and data commands over the new Arduino port
- `examples/wpf_bridge_mesh`
  - ESP32 module node (LED/relay/button/analog) paired directly with MicronetViz WPF app

## Hardware Test Notes

- `mneta_wrapper_test` already passed on hardware
  - sample successful output included valid `local_handshake=0`, `peer_handshake=0`, `peer_crypto_ok`, and `group_crypto_ok`
  - `group_count` may be greater than `1` on repeated runs because group state is persisted in NVS

- `transport_test`, `protocol_test`, `data_test`, and `all_in_one_port_test` still need real two-board testing
  - these sketches are not self-tests; they need the real peer `node_id` from the other board
  - first flash both boards with placeholder `PEER_NODE_ID_HEX`
  - read the printed `node_id` on both boards
  - reflash board 1 with board 2 `node_id` in `PEER_NODE_ID_HEX`
  - reflash board 2 with board 1 `node_id` in `PEER_NODE_ID_HEX`
  - after that the handshake should pass and the sketch-specific UART commands can be tested

- current known real `node_id` captured from hardware:
  - transport test node 1: `9e72dd2cf08210fcff5dfdff5033b9ffe47465af947d2a6a41b0b7589cff2304`

- suggested test order for tomorrow:
  - `transport_test`
  - `protocol_test`
  - `data_test`
  - `all_in_one_port_test`

## Notes

- Target platform is `ESP32` / `ESP32-S3`
- Storage uses `Preferences` NVS
- RNG uses `esp_fill_random()`
- Runtime is poll-based, not FreeRTOS-task based

## Pairing With MicronetViz (WPF)

1. Run `tools/MicronetViz` and wait until bridge is active.
2. In the Bridge control card, copy `WPF Node ID` and note UDP port (`33477`).
3. Open `examples/wpf_bridge_mesh/wpf_bridge_mesh.ino` and set:
   - `WIFI_SSID`, `WIFI_PASSWORD`
   - `WPF_HOST_IP` (IP of your PC)
   - `APP_NODE_ID_HEX` (copied from app UI)
   - network mode:
     - DHCP: `USE_STATIC_IP = false`
     - Static IP: `USE_STATIC_IP = true` + set `LOCAL_IP/NETMASK/GATEWAY/DNS`
4. Flash ESP32 and open Serial Monitor (`115200`).
5. Optional onboarding reset: send serial command `regen_nodeid` and after reboot use `whoami` to read new Node ID.
6. Register this Node ID in MicronetViz, then add/join the same group for all devices.
7. The app should show the ESP32 node and its published keys (`module.*`, `sensor.analog`).

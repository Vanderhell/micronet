# Hardware Test Report 01

Date: 2026-04-06
Timezone: Europe/Bratislava
Title: First successful 3-node ESP32 hardware test

## Setup

- Hardware: 3x ESP32-S3 Zero Mini
- Firmware under test: `examples/arduino_mesh_3nodes/arduino_mesh_3nodes.ino`
- Transport: UDP over Wi-Fi LAN with static IP addressing
- STUN server used during test: `stun.l.google.com:19302`

## Node Mapping

- `COM8` -> `node-1` -> `192.168.1.101`
- `COM11` -> `node-2` -> `192.168.1.102`
- `COM14` -> `node-3` -> `192.168.1.103`

## Tested Commands

- `whoami`
- `identity`
- `group`
- `vars`
- `metrics`
- `request <slot> <key>`
- `list <slot>`
- `metricsreq <slot>`
- `stun`
- `pingall`
- `all <text>`

## Results

- All three nodes booted, joined Wi-Fi, and bound UDP port `33333`
- All three nodes exchanged periodic telemetry successfully
- `identity` returned a stable `node_id` on each node
- `group` returned the same `group_hash` and `group_key` on all three nodes
- `vars` returned `temperature_c`, `counter`, `last_text`, and `node_name`
- `request 1 temperature_c` from `node-2` succeeded and returned `21.50`
- `list 1` from `node-3` succeeded and returned `temperature_c,counter,last_text,node_name`
- `metricsreq 2` from `node-1` succeeded and returned uptime, node count, TX/RX counters, and health
- `stun` on `node-2` succeeded and returned mapped endpoint `46.34.228.160:7522`
- `pingall` succeeded and produced `pong` responses
- Broadcast text succeeded: `node-1` sent `hello-allinone` and both `node-2` and `node-3` received it

## Node IDs

- `node-1`: `42a506991434e527b4af3cd4f5a2692b42b56c02f2c728ef8eea19a1f18c4210`
- `node-2`: `7dcb32a1c21c24bcedd840e2df2c847045c3721b45fbaaebe5ee7cae2da10c99`
- `node-3`: `bb4a192d37979cdb5059c5764921332cf9b04eb51fc702670292e3ea33cfb034`

## Group Data

- `group_hash`: `1c1f9a9c5024f7693abf8779977d6807`
- `group_key`: `fd352f66c864a6180cd1bf86f274f65c`

## Notes

- The current Arduino monolith uses deterministic demo identities and demo group material for inspectability
- This test validates the all-in-one `.ino` integration path on real hardware
- It does not yet prove full cryptographic parity with the native C library security/session stack

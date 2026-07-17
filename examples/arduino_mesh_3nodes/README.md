# Arduino 3-Node Mesh Demo

Universal `.ino` sketch for three ESP32 boards on one LAN.

File:

- `arduino_mesh_3nodes.ino`

## Setup

Use the same sketch on all three boards. Change only:

- `NODE_SLOT`

Board mapping:

- board 1: `NODE_SLOT = 1`
- board 2: `NODE_SLOT = 2`
- board 3: `NODE_SLOT = 3`

Also set:

- local `secrets.h` copied from `secrets.h.example`
- `NODE1_IP`
- `NODE2_IP`
- `NODE3_IP`

Each board should have a stable IP on the same LAN.

## Serial Commands

- `help`
- `status`
- `identity`
- `group`
- `stun`
- `vars`
- `metrics`
- `request <slot> <key>`
- `list <slot>`
- `metricsreq <slot>`
- `peers`
- `hello`
- `pingall`
- `ping <slot>`
- `send <slot> <text>`
- `all <text>`
- `temp <value>`
- `counter`
- `snapshot`
- `whoami`

## Serial Output

Structured log lines start with:

```text
MNET_DEMO|
```

Examples:

```text
MNET_DEMO|node=1|event=wifi_ok|ip=192.168.1.101
MNET_DEMO|node=1|event=tx|peer=2|kind=hello|HELLO|1|node-1
MNET_DEMO|node=2|event=rx|peer=1|kind=telemetry|counter=4|temp=2150
```

This format matches the Python multi-port UART helper added in `examples/esp32_sensor/tools/uart_mesh_terminal.py`.

On boot the sketch also prints:

- a deterministic demo `node_id`
- a shared demo `group_hash`
- a shared demo `group_key`

These are diagnostic demo values so you can inspect identity/group metadata from one `.ino` without committing personal Wi-Fi credentials.

The sketch now also includes a small in-sketch data layer for practical testing:

- local vars: `temperature_c`, `counter`, `last_text`, `node_name`
- remote request/response for a single key
- remote variable listing
- remote metrics request

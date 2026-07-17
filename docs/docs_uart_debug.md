# UART Debug

**Project:** micronet  
**License:** MIT  
**Target platforms:** ESP32  
**Dependencies:** `microsh`, `microlog`

---

## Purpose

The UART debug interface provides a small command shell over the serial port. It is intended for bring-up, inspection, testing, and local diagnostics on ESP32 hardware.

---

## Wiring

Typical wiring:

- ESP32 TX -> USB-UART RX -> PC
- ESP32 RX -> USB-UART TX -> PC
- common ground between the board and the adapter

---

## PC Tools

Recommended tools on the host side:

- Arduino Serial Monitor
- `picocom`
- `screen`
- VS Code serial monitor extensions

---

## Initialization

Enable the UART shell in firmware initialization, then call the debug tick function regularly from the main loop or task.

---

## Command Overview

| Category | Command | Description |
|----------|---------|-------------|
| System | `p2p status` | print the current stack state |
| System | `p2p version` | print the firmware and protocol version |
| System | `p2p reboot` | reboot the device |
| Nodes | `p2p nodes` | list known nodes |
| Nodes | `p2p node trust <id>` | print the trust chain for a node |
| Groups | `p2p groups` | list known groups |
| Groups | `p2p group create` | create a new group |
| Groups | `p2p group leave <hash>` | leave a group |
| Data | `p2p vars` | list local variables |
| Data | `p2p vars <id>` | inspect a variable |
| Data | `p2p set <key> <value>` | set a local variable |
| Data | `p2p metrics` | list metrics |
| Data | `p2p metrics <id>` | inspect one metric |
| Network | `p2p ping <id>` | ping a peer |
| Network | `p2p send <id> <msg>` | send a text message to a peer |
| Storage | `p2p db dump` | dump the local database |
| Storage | `p2p db sync` | request a database sync |
| Storage | `p2p keys` | inspect key state |
| Diagnostics | `p2p packet log on/off` | enable or disable packet logging |

---

## Command Details

### `p2p status`

Prints a summary of the local stack state:

- node ID
- group count
- peer count
- transport status
- security status
- data block status

### `p2p nodes`

Lists all known nodes together with their short IDs, online state, and last-seen time.

### `p2p node trust <id>`

Shows the trust chain for a node, including who invited whom.

### `p2p groups`

Prints all known groups and their member counts.

### `p2p group create`

Creates a new group locally and prints the new group hash.

### `p2p vars`

Lists local data keys. Use `p2p vars <id>` to print the full value for one key.

### `p2p set <key> <value>`

Updates or creates a local variable and publishes the change to peers if the data layer is configured to do so.

### `p2p metrics`

Prints metrics snapshots such as counters, sums, minimums, and maximums.

### `p2p ping <id>`

Sends a small packet to the selected node and prints the response time.

### `p2p db dump`

Dumps the local database in a human-readable form for debugging.

### `p2p db sync`

Requests a database synchronization with peers.

### `p2p keys`

Prints key state for debugging. In production builds this command should be limited or disabled.

### `p2p packet log on/off`

Turns packet logging on or off. When enabled, each packet is printed with direction, peer, type, and message ID.

---

## Example Output

```text
node_id: 4a7b3c8d2e9f1a6b
group_count: 2
peer_count: 5
```

```text
[TX] -> 9f8e7d6c...  DATA_REQUEST (0x20)  msg_id=104
[RX] <- 9f8e7d6c...  DATA_RESPONSE (0x21) msg_id=104
```

---

## Production Build

When `P2P_UART_DEBUG=0`, all UART shell code is removed from the build.

---

## Files

```text
micronet/
`-- src/
    `-- debug/
        |-- p2p_uart.h
        |-- p2p_uart.c
        `-- p2p_uart_cmds.c
```


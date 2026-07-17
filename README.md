# Micronet

Micronet is a C99 LAN messaging library for embedded nodes and desktop diagnostics.

[![CI](https://github.com/Vanderhell/micronet/actions/workflows/ci.yml/badge.svg)](https://github.com/Vanderhell/micronet/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Vanderhell/micronet)](https://github.com/Vanderhell/micronet/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Language](https://img.shields.io/badge/language-C99-blue.svg)](include/micronet.h)

LAN mode is the default:

- no DNS required
- no cloud required
- no public IP required
- no port forwarding required
- no STUN required

STUN support, if used, is explicit and experimental.

## Layout

- `include/` public API
- `src/` C implementation
- `hal/` platform adapters
- `tests/` unit tests
- `examples/` ESP32, Arduino, and desktop examples
- `docs/` technical notes

## Build

```powershell
cmake -S . -B build -DP2P_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

For single-config generators:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DP2P_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Quick Start

1. Read [Quick Start](docs/QUICK_START.md) for a first build and first node bring-up.
2. Read [API Reference](docs/API.md) for the public entry points.
3. Read [Cookbook](docs/COOKBOOK.md) for common patterns and example flows.
4. Pick an example from `examples/` or `arduino/micronet/examples/` and configure its local overrides.

## Public API

The main entry points are declared in `include/micronet.h`.

- lifecycle: `mnet_init`, `mnet_tick`, `mnet_get_node_id`, `mnet_deinit`
- peers: `mnet_peer_add_ip`, `mnet_peer_remove`, `mnet_peer_list`
- groups: `mnet_group_create`, `mnet_group_join`, `mnet_group_leave`, `mnet_peer_join_group`, `mnet_peer_leave_group`
- data: `mnet_publish`, `mnet_update`, `mnet_request`, `mnet_subscribe`, `mnet_query`, `mnet_get_metrics`
- custom traffic: `mnet_send_custom`, `mnet_send_group_custom`, `mnet_broadcast_custom`, `mnet_register_handler`

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [LAN Mode](docs/LAN_MODE.md)
- [Groups](docs/GROUPS.md)
- [API Reference](docs/API.md)
- [Quick Start](docs/QUICK_START.md)
- [Cookbook](docs/COOKBOOK.md)
- [Security Model](docs/SECURITY_MODEL.md)
- [ESP32 Notes](docs/ESP32.md)

## Contributing

Please open an issue first for large changes, protocol changes, or behavior changes that affect interoperability.

- Report bugs and regressions in [Issues](https://github.com/Vanderhell/micronet/issues)
- Keep pull requests focused and test-backed
- Run the relevant build and test targets before asking for review

## Release Notes

Tagged releases are created from the `release.yml` workflow when a tag like `v1.0.0` is pushed.

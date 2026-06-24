# Micronet

Micronet is a C99 LAN messaging library for embedded nodes and desktop diagnostics.

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

## Public API

The main entry points are declared in `include/micronet.h`.

- lifecycle: `mnet_init`, `mnet_tick`, `mnet_get_node_id`, `mnet_deinit`
- peers: `mnet_peer_add_ip`, `mnet_peer_remove`, `mnet_peer_list`
- groups: `mnet_group_create`, `mnet_group_join`, `mnet_group_leave`, `mnet_peer_join_group`, `mnet_peer_leave_group`
- data: `mnet_publish`, `mnet_update`, `mnet_request`, `mnet_subscribe`, `mnet_query`, `mnet_get_metrics`
- custom traffic: `mnet_send_custom`, `mnet_send_group_custom`, `mnet_broadcast_custom`, `mnet_register_handler`

## Documentation

- [docs/ARCHITECTURE.md](/C:/Users/vande/Desktop/micronet/docs/ARCHITECTURE.md)
- [docs/LAN_MODE.md](/C:/Users/vande/Desktop/micronet/docs/LAN_MODE.md)
- [docs/GROUPS.md](/C:/Users/vande/Desktop/micronet/docs/GROUPS.md)
- [docs/API.md](/C:/Users/vande/Desktop/micronet/docs/API.md)
- [docs/SECURITY_MODEL.md](/C:/Users/vande/Desktop/micronet/docs/SECURITY_MODEL.md)
- [docs/ESP32.md](/C:/Users/vande/Desktop/micronet/docs/ESP32.md)

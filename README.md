# micronet

`micronet` is a C99 static library for small peer-to-peer node meshes. It wraps transport, security, network membership, replicated data, and protocol handling behind one public header: `include/micronet.h`.

## Scope

- UDP-based transport with STUN-assisted external address discovery
- Peer identity and session security
- Node and group management
- Shared variables, table/query access, subscriptions, and metrics
- Custom message handlers on top of the core protocol

## Repository Layout

- `include/` public API
- `src/` implementation of transport, security, network, data, protocol, and top-level API glue
- `hal/` platform abstraction layer
- `tests/` unit and integration-style tests
- `docs/` block-level design notes
- `examples/` example skeletons

## Build

This project uses CMake and currently builds as a static library target named `micronet`.

### Windows

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

### Linux / single-config generators

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Tests

Tests are enabled by default with `P2P_BUILD_TESTS=ON`.

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Verified in this repository on April 6, 2026:

- `cmake --build build --config Debug`
- `ctest -C Debug --output-on-failure`
- Result: `1/1` test target passed (`micronet_tests`)

## CMake Options

- `P2P_BUILD_TESTS=ON` builds `micronet_tests`
- `P2P_BUILD_EXAMPLES=OFF` enables example subdirectories
- `P2P_UART_DEBUG=OFF` adds optional UART debug sources when present

## Public API Sketch

```c
#include "micronet.h"

mnet_config_t cfg = {0};
cfg.stun_host = "stun.l.google.com";
cfg.stun_port = 19302;
cfg.heartbeat_ms = 5000;
cfg.offline_timeout_ms = 15000;

if (mnet_init(&cfg) == MNET_OK) {
    for (;;) {
        (void)mnet_tick();
    }
}
```

Main entry points are:

- lifecycle: `mnet_init`, `mnet_tick`, `mnet_get_node_id`, `mnet_deinit`
- nodes and groups: `mnet_node_*`, `mnet_group_*`
- shared data: `mnet_publish`, `mnet_update`, `mnet_request`, `mnet_subscribe`, `mnet_query`, `mnet_get_metrics`
- custom protocol traffic: `mnet_send_custom`, `mnet_broadcast_custom`, `mnet_register_handler`

## Dependencies

The root `CMakeLists.txt` vendors only the dependencies the current code actually consumes:

- `microcrypt`
- `microdh`
- `microtest` for the test target

These are pinned to immutable commit SHAs through `FetchContent`.

## Current Status

- The library target and test target build successfully on the current Windows workspace
- Internal module coverage is decent for transport, security, network, data, protocol, and API smoke paths
- Example directories are still skeletons and are not yet end-user runnable
- The detailed design notes live under `docs/`

## Known Technical Risks

- The public API is implemented as a process-global singleton rather than an explicit instance handle
- The API test suite is stronger than before, but the singleton design still limits true multi-node public-surface testing in-process
- The top-level build always compiles `hal/p2p_hal_linux.c`, so cross-platform portability is not yet cleanly expressed in CMake

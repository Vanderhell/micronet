# Štruktúra repozitára a CI/CD

**Projekt:** micronet
**Dokument:** docs/repo_structure.md

---

## Adresárová štruktúra

```
micronet/
│
├── .github/
│   └── workflows/
│       ├── ci.yml              ← testy pri každom push / PR
│       └── release.yml         ← automatický release pri tagu
│
├── docs/
│   ├── 01_transport.md
│   ├── 02_security.md
│   ├── 03_network.md
│   ├── 04_data.md
│   ├── 05_protocol.md
│   ├── 06_api.md
│   ├── keys_and_hashes.md
│   ├── uart_debug.md
│   └── repo_structure.md       ← tento dokument
│
├── include/
│   └── micronet.h                ← jediný public header
│
├── src/
│   ├── transport/
│   │   ├── p2p_transport.h
│   │   ├── p2p_transport.c
│   │   ├── p2p_transport_stun.c
│   │   └── p2p_hal.h
│   ├── security/
│   │   ├── p2p_security.h
│   │   ├── p2p_security.c
│   │   ├── p2p_security_handshake.c
│   │   └── p2p_security_keys.c
│   ├── network/
│   │   ├── p2p_network.h
│   │   ├── p2p_network.c
│   │   ├── p2p_network_gossip.c
│   │   ├── p2p_network_group.c
│   │   └── p2p_network_sync.c
│   ├── data/
│   │   ├── p2p_data.h
│   │   ├── p2p_data.c
│   │   ├── p2p_data_vars.c
│   │   ├── p2p_data_query.c
│   │   └── p2p_data_metrics.c
│   ├── protocol/
│   │   ├── p2p_protocol.h
│   │   ├── p2p_protocol.c
│   │   ├── p2p_protocol_dispatch.c
│   │   └── p2p_protocol_serialize.c
│   ├── debug/
│   │   ├── p2p_uart.h
│   │   ├── p2p_uart.c
│   │   └── p2p_uart_cmds.c
│   └── micronet.c                ← Public API implementácia
│
├── hal/
│   ├── p2p_hal_linux.c         ← Linux sockety, RNG, čas
│   └── p2p_hal_esp32.c         ← ESP-IDF sockety, HW RNG, čas
│
├── tests/
│   ├── test_runner.c           ← hlavný runner (microtest)
│   ├── test_transport.c
│   ├── test_security.c
│   ├── test_network.c
│   ├── test_data.c
│   ├── test_protocol.c
│   └── test_api.c              ← end-to-end testy
│
├── examples/
│   ├── linux_chat/
│   │   ├── main.c
│   │   └── CMakeLists.txt
│   └── esp32_sensor/
│       ├── main/
│       │   └── main.c
│       └── CMakeLists.txt
│
├── CMakeLists.txt              ← hlavný build systém (Linux)
├── idf_component.yml           ← ESP-IDF komponent
├── .gitignore
├── LICENSE                     ← MIT
└── README.md
```

---

## .gitignore

```gitignore
# Build výstupy
build/
dist/
*.o
*.a
*.so
*.elf
*.bin
*.map

# ESP-IDF
managed_components/
dependencies.lock
sdkconfig
sdkconfig.old

# IDE
.vscode/
.idea/
*.swp
*.swo
*.DS_Store

# Testy
tests/build/
coverage/
*.gcda
*.gcno

# Temp
*.tmp
*.log
*.bak
```

---

## CMakeLists.txt (Linux)

```cmake
cmake_minimum_required(VERSION 3.16)
project(micronet VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Kompilačné flagy
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# Knižnica
add_library(micronet STATIC
    src/micronet.c
    src/transport/p2p_transport.c
    src/transport/p2p_transport_stun.c
    src/security/p2p_security.c
    src/security/p2p_security_handshake.c
    src/security/p2p_security_keys.c
    src/network/p2p_network.c
    src/network/p2p_network_gossip.c
    src/network/p2p_network_group.c
    src/network/p2p_network_sync.c
    src/data/p2p_data.c
    src/data/p2p_data_vars.c
    src/data/p2p_data_query.c
    src/data/p2p_data_metrics.c
    src/protocol/p2p_protocol.c
    src/protocol/p2p_protocol_dispatch.c
    src/protocol/p2p_protocol_serialize.c
    hal/p2p_hal_linux.c
)

target_include_directories(micronet PUBLIC include src)

# UART debug (voliteľné)
option(P2P_UART_DEBUG "Enable UART debug shell" ON)
if(P2P_UART_DEBUG)
    target_sources(micronet PRIVATE
        src/debug/p2p_uart.c
        src/debug/p2p_uart_cmds.c
    )
    target_compile_definitions(micronet PRIVATE P2P_UART_DEBUG=1)
endif()

# Testy
option(P2P_BUILD_TESTS "Build tests" ON)
if(P2P_BUILD_TESTS)
    enable_testing()
    add_executable(micronet_tests
        tests/test_runner.c
        tests/test_transport.c
        tests/test_security.c
        tests/test_network.c
        tests/test_data.c
        tests/test_protocol.c
        tests/test_api.c
    )
    target_link_libraries(micronet_tests micronet)
    add_test(NAME micronet_tests COMMAND micronet_tests)
endif()

# Príklady
option(P2P_BUILD_EXAMPLES "Build examples" OFF)
if(P2P_BUILD_EXAMPLES)
    add_subdirectory(examples/linux_chat)
endif()
```

---

## idf_component.yml (ESP-IDF)

```yaml
version: "0.1.0"
description: "Lightweight P2P networking library for ESP32"
url: "https://github.com/Vanderhell/micronet"
license: "MIT"

targets:
  - esp32
  - esp32s3
  - esp32c3

dependencies:
  idf: ">=5.0.0"
```

---

## .github/workflows/ci.yml

```yaml
name: CI

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  build-and-test:
    name: Build & Test (Linux)
    runs-on: ubuntu-latest

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake gcc build-essential

      - name: Configure
        run: |
          cmake -B build \
            -DCMAKE_BUILD_TYPE=Debug \
            -DP2P_BUILD_TESTS=ON \
            -DP2P_UART_DEBUG=OFF

      - name: Build
        run: cmake --build build --parallel

      - name: Run tests
        run: |
          cd build
          ctest --output-on-failure

      - name: Check warnings
        run: |
          cmake -B build_strict \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_FLAGS="-Werror"
          cmake --build build_strict --parallel
```

---

## .github/workflows/release.yml

```yaml
name: Release

on:
  push:
    tags:
      - 'v*.*.*'

jobs:
  release:
    name: Create Release
    runs-on: ubuntu-latest

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake gcc build-essential

      - name: Build & Test
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Release -DP2P_BUILD_TESTS=ON
          cmake --build build --parallel
          cd build && ctest --output-on-failure

      - name: Create release archive
        run: |
          mkdir -p release/micronet
          cp -r include src hal docs LICENSE README.md CMakeLists.txt idf_component.yml release/micronet/
          cd release && tar -czf micronet-${{ github.ref_name }}.tar.gz micronet/
          cd release && zip -r micronet-${{ github.ref_name }}.zip micronet/

      - name: GitHub Release
        uses: softprops/action-gh-release@v1
        with:
          files: |
            release/micronet-${{ github.ref_name }}.tar.gz
            release/micronet-${{ github.ref_name }}.zip
          generate_release_notes: true
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

---

## Pravidlá pre čistotu repa

1. **Žiadne binárky** – len zdrojový kód a dokumentácia
2. **Žiadne IDE súbory** – `.vscode/`, `.idea/` sú v `.gitignore`
3. **Žiadne vygenerované súbory** – `build/` nikdy do repa
4. **Každý blok = samostatný adresár** v `src/`
5. **Každý blok má testy** v `tests/`
6. **Každý blok má dokumentáciu** v `docs/`
7. **Commit správy** vo formáte: `feat:`, `fix:`, `test:`, `docs:`

---

## Verzonovanie

```
v0.1.0  – transport blok hotový a otestovaný
v0.2.0  – security blok
v0.3.0  – network blok
v0.4.0  – data blok
v0.5.0  – protocol blok
v0.6.0  – public API
v1.0.0  – prvý stabilný release, ESP32 support
```

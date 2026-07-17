# Quick Start

This guide shows the shortest path from a fresh checkout to a working Micronet build.

## 1. Build the library

```powershell
cmake -S . -B build -DP2P_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

For single-config generators:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DP2P_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 2. Read the API

The public API is declared in `include/micronet.h` and summarized in [API Reference](API.md).

## 3. Run a desktop example

- `examples/linux_chat`
- `examples/esp32_sensor`

## 4. Run an ESP32 example

- read [ESP32 Notes](ESP32.md)
- configure the example's local Wi-Fi and peer settings
- build and flash the selected example

## 5. Run an Arduino example

- open `arduino/micronet/examples/all_in_one_port_test`
- copy the local `secrets.h.example` files to `secrets.h`
- set the board-specific IP and peer values
- compile against `esp32:esp32:esp32`

## 6. Verify the build

- run the host test suite
- compile the Arduino examples
- check the relevant hardware note or test report before flashing to devices

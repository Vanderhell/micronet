# ESP32 3-Node UART Demo

This example turns three ESP32-S3 boards into a small UDP mesh demo that you can inspect over three serial ports at once.

What it does:

- each node joins the same Wi-Fi network in station mode
- each node opens the same UDP port through `micronet`'s transport layer
- each node sends periodic telemetry to the other two nodes
- each node accepts simple commands from the serial console
- every important event is printed as a structured `MNET_DEMO|...` line so the PC terminal script can verify the traffic

This demo is intentionally static:

- peer IPs are configured ahead of time
- node slot `1/2/3` is selected per firmware build
- it exercises the transport layer in a way that is easy to test on real hardware today

## Files

- `main/main.c` firmware for all three boards
- `tools/uart_mesh_terminal.py` multi-port serial monitor and command sender
- `sdkconfig.node1.defaults`
- `sdkconfig.node2.defaults`
- `sdkconfig.node3.defaults`

## Build

From `examples/esp32_sensor`:

```powershell
idf.py set-target esp32s3
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.node1.defaults" build flash
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.node2.defaults" build flash
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.node3.defaults" build flash
```

Before building, edit the three `sdkconfig.node*.defaults` files:

- set your real Wi-Fi SSID and password
- set the static IPv4 address of each board on your LAN

You can also copy `main/secrets.h.example` to `main/secrets.h` for local overrides.

The boards must be reachable from each other on the same network. DHCP reservations on the router are the easiest setup.

## Serial Commands

Open one serial monitor per node or use the Python helper below.

Commands accepted by the firmware:

- `help`
- `status`
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

## Multi-port Terminal

Install `pyserial`:

```powershell
pip install pyserial
```

Run the helper:

```powershell
python tools\uart_mesh_terminal.py --ports COM10 COM11 COM12 --baud 115200
```

Terminal commands:

- `/1 status`
- `/2 ping 1`
- `/3 send 1 ahoj`
- `/all hello`
- `/summary`
- `/quit`

If a firmware line starts with `MNET_DEMO|`, the terminal parses it and keeps per-node RX/TX counters so you can see whether the mesh is alive.

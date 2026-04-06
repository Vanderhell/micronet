# ESP32 STUN Probe

This is a minimal one-board ESP32 test for `micronet` STUN resolution.

What it does:

- joins Wi-Fi in station mode
- opens one UDP socket through `p2p_transport`
- sends a STUN binding request to the configured server
- prints the mapped public IPv4 address and port over UART

It is meant for quick bring-up on a single ESP32-S3 before testing multi-node demos.

## Build

From `examples/esp32_stun_probe`:

```powershell
idf.py set-target esp32s3
idf.py build flash monitor
```

Before building, edit `sdkconfig.defaults`:

- `CONFIG_MICRONET_STUN_WIFI_SSID`
- `CONFIG_MICRONET_STUN_WIFI_PASSWORD`
- optionally `CONFIG_MICRONET_STUN_HOST`
- optionally `CONFIG_MICRONET_STUN_PORT`

## UART Commands

- `probe` runs the STUN request again
- `status` prints the current configuration
- `help` prints the command list

Expected success output:

```text
STUN_PROBE|event=ok|server=stun.l.google.com:19302|mapped=1.2.3.4:54321
```

Failure output looks like:

```text
STUN_PROBE|event=fail|server=stun.l.google.com:19302|err=-2
```

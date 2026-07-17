# ESP32 STUN Probe

This is a minimal one-board ESP32 test for `micronet` STUN resolution.
STUN is disabled by default until you set a host in local config.

What it does:

- joins Wi-Fi in station mode
- opens one UDP socket through `p2p_transport`
- sends a STUN binding request only when the host is configured
- prints the mapped public IPv4 address and port over UART

It is meant for quick bring-up on a single ESP32 before testing multi-node demos.

## Build

From `examples/esp32_stun_probe`:

```powershell
idf.py set-target esp32
idf.py build flash monitor
```

Before building, edit `sdkconfig.defaults`:

- `CONFIG_MICRONET_STUN_WIFI_SSID`
- `CONFIG_MICRONET_STUN_WIFI_PASSWORD`
- optionally `CONFIG_MICRONET_STUN_HOST` - leave empty to keep STUN disabled
- optionally `CONFIG_MICRONET_STUN_PORT`

## UART Commands

- `probe` runs the STUN request again
- `status` prints the current configuration
- `help` prints the command list

Expected success output:

```text
STUN_PROBE|event=ok|server=<configured-host>:19302|mapped=1.2.3.4:54321
```

Failure output looks like:

```text
STUN_PROBE|event=skip|reason=stun_disabled
```

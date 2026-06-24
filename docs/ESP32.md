# ESP32

Micronet ships ESP32 examples for LAN operation.

## ESP32 Sensor Demo

- uses the LAN transport path
- supports a local ignored `secrets.h` override file
- keeps Wi-Fi credentials out of the repository when configured locally
- uses fixed LAN peer IPs for the demo topology

## ESP32 STUN Probe

- STUN is optional and disabled by default
- `CONFIG_MICRONET_STUN_HOST` can be left empty
- the example reports `stun_disabled` when no host is configured

## Local Secret Files

Example secret files are provided as `*.example` templates and are ignored when copied to `secrets.h`.

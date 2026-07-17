# Arduino STUN Probe

This is the Arduino IDE version of the one-board STUN test for ESP32.

Files:

- `arduino_stun_probe.ino`

## Use

1. Open `arduino_stun_probe.ino` in Arduino IDE.
2. Copy `secrets.h.example` to `secrets.h` and fill in local Wi-Fi details.
3. Select your ESP32 board.
4. Upload.
5. Open Serial Monitor at `115200`.

On boot it automatically:

- connects to Wi-Fi
- opens a UDP socket
- sends a STUN binding request only when `secrets.h` enables STUN
- prints the mapped public IP and port if the server replies

## Serial commands

- `help`
- `status`
- `probe`

## Expected output

Success:

```text
STUN_PROBE|event=ok|server=<configured-host>:19302|mapped=1.2.3.4:54321
```

Timeout or failure:

```text
STUN_PROBE|event=skip|reason=stun_disabled
```

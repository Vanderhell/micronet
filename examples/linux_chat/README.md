# Micronet Chat

Small LAN chat/demo utility for desktop nodes.

## Build

Configure the project with `P2P_BUILD_EXAMPLES=ON` and build target `micronet_chat`.

## Usage

```text
micronet_chat --name pc1 --port 33333 --group lights --discover
micronet_chat --name pc2 --port 33333 --group lights --peer 192.168.1.10:33333 --send hello
```

Input typed on stdin is sent to the configured group.
Use `peers` to print the current peer table and `discover` to send a LAN discovery beacon.

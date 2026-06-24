# LAN Mode

LAN mode is the default configuration for Micronet.

## Properties

- local UDP transport on a fixed LAN port
- no DNS lookup required
- no cloud service required
- no public IP required
- no port forwarding required
- no STUN required

## Peer Entry

Peers can be added manually by IP and port. Discovery traffic, when used, is limited to the local network.

## STUN

STUN is experimental only.

- disabled by default
- no default public STUN hostname is baked into the LAN configuration
- explicit configuration is required to enable it

## Operational Notes

- manual peer configuration remains available if broadcast is blocked
- group fanout still applies in LAN mode
- diagnostics report peer online/offline state and group membership

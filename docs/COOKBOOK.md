# Cookbook

Small, practical recipes for common Micronet tasks.

## Create a node and publish data

```c
mnet_context_t ctx;
mnet_config_t cfg = {0};

if (mnet_init(&ctx, &cfg) == MNET_OK) {
    mnet_publish(&ctx, "temperature_c", "21.5");
}
```

## Send a custom message to one peer

```c
const uint8_t payload[] = "hello";
mnet_send_custom(&ctx, peer_id, 0x80U, payload, sizeof(payload) - 1U);
```

## Join a group and send to it

```c
uint8_t group_hash[16];
uint8_t group_key[16];

mnet_group_create(&ctx, group_hash, group_key);
mnet_send_group_custom(&ctx, group_hash, 0x80U, payload, sizeof(payload) - 1U, NULL);
```

## Inspect connected peers

```c
mnet_peer_info_t peers[8];
uint8_t peer_count = 0U;

if (mnet_peer_list(peers, 8U, &peer_count) == MNET_OK) {
    for (uint8_t i = 0; i < peer_count; ++i) {
        /* inspect peers[i] */
    }
}
```

## Arduino workflow

- use the `arduino/micronet/examples/*` sketches for board testing
- keep local credentials in the ignored `secrets.h` files
- use the bundled wrapper examples to validate transport, protocol, and data paths

## Release workflow

- push a tag like `v1.0.0`
- the `release.yml` workflow builds the release archive and creates a GitHub Release

## Issue triage

- if behavior changed, add a test or a repro sketch
- if the change spans protocol boundaries, update the relevant docs in `docs/`

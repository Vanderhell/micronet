# API

Public declarations live in `include/micronet.h`.

## Lifecycle

- `mnet_init()`
- `mnet_tick()`
- `mnet_get_node_id()`
- `mnet_deinit()`

## Peer Management

- `mnet_peer_add_ip()`
- `mnet_peer_remove()`
- `mnet_peer_list()`
- `mnet_peer_join_group()`
- `mnet_peer_leave_group()`

`mnet_peer_add_ip(NULL, ip, port)` creates a deterministic placeholder id from the address tuple. That id is for routing and diagnostics only, not device identity.

## Groups

- `mnet_group_create()`
- `mnet_group_invite()`
- `mnet_group_join()`
- `mnet_group_leave()`
- `mnet_group_members()`
- `mnet_group_is_member()`

## Data and Messages

- `mnet_publish()`
- `mnet_update()`
- `mnet_request()`
- `mnet_subscribe()`
- `mnet_unsubscribe()`
- `mnet_list_vars()`
- `mnet_query()`
- `mnet_get_metrics()`
- `mnet_send_custom()`
- `mnet_send_group_custom()`
- `mnet_broadcast_custom()`
- `mnet_register_handler()`

`mnet_broadcast_custom(NULL, ...)` is the all-peers broadcast path.

## Configuration

`mnet_config_t` defaults to LAN-only mode when zero-initialized:

- `network_mode = MNET_MODE_LAN_ONLY`
- `stun_enabled = false`
- no default public STUN hostname

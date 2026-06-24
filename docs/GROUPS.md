# Groups

A group is a fixed-size membership set used to scope fanout.

## Model

- group identity is stored as a 16-byte group hash
- each node can belong to multiple groups
- each peer stores its own group membership list
- the network table stores group membership lists for routing and diagnostics

## Operations

- `mnet_group_create()` creates a local group and joins the local node
- `mnet_group_join()` joins the local node to an existing group
- `mnet_group_leave()` removes the local node from a group
- `mnet_peer_join_group()` assigns a peer to a group
- `mnet_peer_leave_group()` removes a peer from a group
- `mnet_send_group_custom()` fans out a custom message to matching peers only
- `mnet_broadcast_custom(NULL, ...)` broadcasts to all eligible peers

## Isolation Guarantee

Messages sent to one group are routed only to peers that are members of that group.
Peers outside the group are not selected for fanout.

## Limits

- maximum peers: `MNET_MAX_NODES`
- maximum groups per node: `MNET_MAX_GROUPS`
- maximum members per group: `P2P_MAX_MEMBERS`

## Empty or Unknown Groups

- unknown group: `MNET_ERR_NOT_FOUND`
- empty known group: send count 0 and `MNET_OK`
- placeholder peer ids created from `mnet_peer_add_ip(NULL, ...)` are routing placeholders only

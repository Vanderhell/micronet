#ifndef MNET_IDENTITY_H
#define MNET_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool mnet_identity_init(void);
bool mnet_identity_get_node_id(uint8_t out_node_id[32]);
bool mnet_identity_get_pubkey(uint8_t out_pubkey[32]);
bool mnet_identity_get_privkey(uint8_t out_privkey[32]);

#ifdef __cplusplus
}
#endif

#endif

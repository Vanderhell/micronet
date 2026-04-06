#ifndef MDH_H
#define MDH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MDH_OK = 0,
    MDH_ERR_INVALID_KEY = -1,
    MDH_ERR_WEAK_KEY = -2,
    MDH_ERR_RNG = -3
} mdh_err_t;

typedef struct {
    uint8_t privkey[32];
    uint8_t pubkey[32];
} mdh_keypair_t;

typedef mdh_err_t (*mdh_rng_fn)(uint8_t *buf, size_t len);

/*
 * Generate an X25519 keypair using the caller-provided RNG.
 * The private key is clamped per RFC 7748 section 5 before the public key
 * is derived.
 */
mdh_err_t mdh_generate_keypair(mdh_keypair_t *kp, mdh_rng_fn rng);

/*
 * Compute an X25519 shared secret from a local private key and a remote
 * public key. Returns MDH_ERR_WEAK_KEY when the peer key maps into a small
 * subgroup and yields an all-zero shared secret.
 */
mdh_err_t mdh_shared_secret(const uint8_t privkey[32],
                            const uint8_t remote_pub[32],
                            uint8_t out_secret[32]);

#ifdef MDH_TESTING
typedef enum {
    MDH_TEST_WIPE_SCALAR = 0,
    MDH_TEST_WIPE_X1,
    MDH_TEST_WIPE_A,
    MDH_TEST_WIPE_B,
    MDH_TEST_WIPE_C,
    MDH_TEST_WIPE_D,
    MDH_TEST_WIPE_E,
    MDH_TEST_WIPE_F,
    MDH_TEST_WIPE_SHARED_SECRET,
    MDH_TEST_WIPE_COUNT
} mdh_test_wipe_slot_t;

void mdh_test_reset_wipes(void);
int mdh_test_wipe_was_zeroed(mdh_test_wipe_slot_t slot, size_t expected_len);
#endif

#ifdef __cplusplus
}
#endif

#endif

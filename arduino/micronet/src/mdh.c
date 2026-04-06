#include "mdh.h"

#include <stdint.h>
#include <string.h>

/*
 * Constant-time goals:
 * - Secret scalars are processed with fixed-trip loops only.
 * - Conditional swaps/selects are implemented with bit-masks, not branches.
 * - The Montgomery ladder must not branch on secret scalar bits.
 * - Validation on peer public keys happens before ladder execution and only on
 *   public inputs.
 */

typedef int64_t gf[16];

static const gf MDH_121665 = { 0xdb41, 1 };
static const uint8_t MDH_X25519_SMALL_ORDER[12][32] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0xe0, 0xeb, 0x7a, 0x7c, 0x3b, 0x41, 0xb8, 0xae,
      0x16, 0x56, 0xe3, 0xfa, 0xf1, 0x9f, 0xc4, 0x6a,
      0xda, 0x09, 0x8d, 0xeb, 0x9c, 0x32, 0xb1, 0xfd,
      0x86, 0x62, 0x05, 0x16, 0x5f, 0x49, 0xb8, 0x00 },
    { 0x5f, 0x9c, 0x95, 0xbc, 0xa3, 0x50, 0x8c, 0x24,
      0xb1, 0xd0, 0xb1, 0x55, 0x9c, 0x83, 0xef, 0x5b,
      0x04, 0x44, 0x5c, 0xc4, 0x58, 0x1c, 0x8e, 0x86,
      0xd8, 0x22, 0x4e, 0xdd, 0xd0, 0x9f, 0x11, 0x57 },
    { 0xec, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f },
    { 0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f },
    { 0xee, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f },
    { 0xcd, 0xeb, 0x7a, 0x7c, 0x3b, 0x41, 0xb8, 0xae,
      0x16, 0x56, 0xe3, 0xfa, 0xf1, 0x9f, 0xc4, 0x6a,
      0xda, 0x09, 0x8d, 0xeb, 0x9c, 0x32, 0xb1, 0xfd,
      0x86, 0x62, 0x05, 0x16, 0x5f, 0x49, 0xb8, 0x80 },
    { 0x4c, 0x9c, 0x95, 0xbc, 0xa3, 0x50, 0x8c, 0x24,
      0xb1, 0xd0, 0xb1, 0x55, 0x9c, 0x83, 0xef, 0x5b,
      0x04, 0x44, 0x5c, 0xc4, 0x58, 0x1c, 0x8e, 0x86,
      0xd8, 0x22, 0x4e, 0xdd, 0xd0, 0x9f, 0x11, 0xd7 },
    { 0xd9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0xda, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0xdb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
};

#ifdef MDH_TESTING
static unsigned char mdh_test_wipe_ok[MDH_TEST_WIPE_COUNT];
static size_t mdh_test_wipe_len[MDH_TEST_WIPE_COUNT];

void mdh_test_reset_wipes(void) {
    memset(mdh_test_wipe_ok, 0, sizeof(mdh_test_wipe_ok));
    memset(mdh_test_wipe_len, 0, sizeof(mdh_test_wipe_len));
}

int mdh_test_wipe_was_zeroed(mdh_test_wipe_slot_t slot, size_t expected_len) {
    if ((unsigned)slot >= MDH_TEST_WIPE_COUNT) {
        return 0;
    }
    return mdh_test_wipe_ok[slot] != 0 && mdh_test_wipe_len[slot] == expected_len;
}
#endif

#ifndef MDH_TESTING
enum {
    MDH_TEST_WIPE_SCALAR = 0,
    MDH_TEST_WIPE_X1 = 0,
    MDH_TEST_WIPE_A = 0,
    MDH_TEST_WIPE_B = 0,
    MDH_TEST_WIPE_C = 0,
    MDH_TEST_WIPE_D = 0,
    MDH_TEST_WIPE_E = 0,
    MDH_TEST_WIPE_F = 0,
    MDH_TEST_WIPE_SHARED_SECRET = 0
};
#endif

static void mdh_secure_zero(void *ptr, size_t len) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    size_t i;

    for (i = 0; i < len; ++i) {
        p[i] = 0U;
    }
}

static void mdh_secure_zero_gf(gf value) {
    mdh_secure_zero(value, sizeof(gf));
}

#ifdef MDH_TESTING
static void mdh_secure_zero_tagged(void *ptr,
                                   size_t len,
                                   mdh_test_wipe_slot_t slot) {
    size_t i;
    const volatile unsigned char *p;
    unsigned char ok = 1U;

    mdh_secure_zero(ptr, len);

    p = (const volatile unsigned char *)ptr;
    for (i = 0; i < len; ++i) {
        ok = (unsigned char)(ok & (unsigned char)(p[i] == 0U));
    }
    mdh_test_wipe_ok[slot] = ok;
    mdh_test_wipe_len[slot] = len;
}
#else
static void mdh_secure_zero_tagged(void *ptr, size_t len, int slot) {
    (void)slot;
    mdh_secure_zero(ptr, len);
}
#endif

static void gf_0(gf out) {
    memset(out, 0, sizeof(gf));
}

static void gf_1(gf out) {
    gf_0(out);
    out[0] = 1;
}

static void gf_copy(gf out, const gf in) {
    memcpy(out, in, sizeof(gf));
}

static void gf_add(gf out, const gf a, const gf b) {
    int i;

    for (i = 0; i < 16; ++i) {
        out[i] = a[i] + b[i];
    }
}

static void gf_sub(gf out, const gf a, const gf b) {
    int i;

    for (i = 0; i < 16; ++i) {
        out[i] = a[i] - b[i];
    }
}

static void gf_carry(gf out) {
    int i;
    int64_t carry;

    for (i = 0; i < 16; ++i) {
        out[i] += (int64_t)1 << 16;
        carry = out[i] >> 16;
        if (i < 15) {
            out[i + 1] += carry - 1;
        } else {
            out[0] += (carry - 1) * 38;
        }
        out[i] -= carry << 16;
    }
}

static void gf_mul(gf out, const gf a, const gf b) {
    int i;
    int j;
    int64_t t[31] = { 0 };

    for (i = 0; i < 16; ++i) {
        for (j = 0; j < 16; ++j) {
            t[i + j] += a[i] * b[j];
        }
    }

    for (i = 0; i < 15; ++i) {
        t[i] += 38 * t[i + 16];
    }

    for (i = 0; i < 16; ++i) {
        out[i] = t[i];
    }

    gf_carry(out);
    gf_carry(out);
    mdh_secure_zero(t, sizeof(t));
}

static void gf_sq(gf out, const gf in) {
    gf_mul(out, in, in);
}

static void gf_select(gf p, gf q, uint32_t bit) {
    int i;
    int64_t mask = ~(int64_t)(bit - 1U);

    /* Branchless conditional swap used by the Montgomery ladder. */
    for (i = 0; i < 16; ++i) {
        int64_t t = mask & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void gf_pack(uint8_t out[32], const gf in) {
    gf m;
    gf t;
    int i;
    int j;

    gf_copy(t, in);
    gf_carry(t);
    gf_carry(t);
    gf_carry(t);

    for (j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        m[14] &= 0xffff;
        gf_select(t, m, (uint32_t)(1 - ((m[15] >> 16) & 1)));
    }

    for (i = 0; i < 16; ++i) {
        out[2 * i] = (uint8_t)(t[i] & 0xff);
        out[2 * i + 1] = (uint8_t)((t[i] >> 8) & 0xff);
    }

    mdh_secure_zero_gf(m);
    mdh_secure_zero_gf(t);
}

static void gf_unpack(gf out, const uint8_t in[32]) {
    int i;

    for (i = 0; i < 16; ++i) {
        out[i] = in[2 * i] + ((int64_t)in[2 * i + 1] << 8);
    }
    out[15] &= 0x7fff;
}

static void gf_invert(gf out, const gf in) {
    gf c;
    int i;
    int a;

    gf_copy(c, in);
    for (a = 253; a >= 0; --a) {
        gf_sq(c, c);
        if (a != 2 && a != 4) {
            gf_mul(c, c, in);
        }
    }
    for (i = 0; i < 16; ++i) {
        out[i] = c[i];
    }
    mdh_secure_zero_gf(c);
}

static void mdh_clamp_scalar(uint8_t scalar[32]) {
    scalar[0] &= 248U;
    scalar[31] &= 127U;
    scalar[31] |= 64U;
}

static int mdh_is_all_zero(const uint8_t *buf, size_t len) {
    uint8_t acc = 0;
    size_t i;

    for (i = 0; i < len; ++i) {
        acc |= buf[i];
    }

    return acc == 0;
}

static int mdh_has_small_order_point(const uint8_t point[32]) {
    uint8_t diff[12] = { 0 };
    uint32_t match = 0;
    size_t i;
    size_t j;

    for (i = 0; i < 12U; ++i) {
        for (j = 0; j < 32U; ++j) {
            diff[i] |= (uint8_t)(point[j] ^ MDH_X25519_SMALL_ORDER[i][j]);
        }
    }

    for (i = 0; i < 12U; ++i) {
        match |= (uint32_t)((uint32_t)(diff[i] - 1U) >> 8);
    }

    mdh_secure_zero(diff, sizeof(diff));
    return (int)(match & 1U);
}

static void mdh_x25519(uint8_t out[32],
                       const uint8_t scalar_in[32],
                       const uint8_t point[32]) {
    uint8_t scalar[32];
    uint8_t shared_secret[32];
    gf x1;
    gf a;
    gf b;
    gf c;
    gf d;
    gf e;
    gf f;
    int i;

    memcpy(scalar, scalar_in, sizeof(scalar));
    mdh_clamp_scalar(scalar);
    gf_unpack(x1, point);

    gf_1(a);
    gf_0(b);
    gf_0(c);
    gf_1(d);
    gf_copy(b, x1);

    for (i = 254; i >= 0; --i) {
        uint32_t bit = (uint32_t)((scalar[i >> 3] >> (i & 7)) & 1U);

        gf_select(a, b, bit);
        gf_select(c, d, bit);

        gf_add(e, a, c);
        gf_sub(a, a, c);
        gf_add(c, b, d);
        gf_sub(b, b, d);
        gf_sq(d, e);
        gf_sq(f, a);
        gf_mul(a, c, a);
        gf_mul(c, b, e);
        gf_add(e, a, c);
        gf_sub(a, a, c);
        gf_sq(b, a);
        gf_sub(c, d, f);
        gf_mul(a, c, MDH_121665);
        gf_add(a, a, d);
        gf_mul(c, c, a);
        gf_mul(a, d, f);
        gf_mul(d, b, x1);
        gf_sq(b, e);

        gf_select(a, b, bit);
        gf_select(c, d, bit);
    }

    gf_invert(c, c);
    gf_mul(a, a, c);
    gf_pack(shared_secret, a);
    memcpy(out, shared_secret, sizeof(shared_secret));

    mdh_secure_zero_tagged(shared_secret, sizeof(shared_secret), MDH_TEST_WIPE_SHARED_SECRET);
    mdh_secure_zero_tagged(scalar, sizeof(scalar), MDH_TEST_WIPE_SCALAR);
    mdh_secure_zero_tagged(x1, sizeof(x1), MDH_TEST_WIPE_X1);
    mdh_secure_zero_tagged(a, sizeof(a), MDH_TEST_WIPE_A);
    mdh_secure_zero_tagged(b, sizeof(b), MDH_TEST_WIPE_B);
    mdh_secure_zero_tagged(c, sizeof(c), MDH_TEST_WIPE_C);
    mdh_secure_zero_tagged(d, sizeof(d), MDH_TEST_WIPE_D);
    mdh_secure_zero_tagged(e, sizeof(e), MDH_TEST_WIPE_E);
    mdh_secure_zero_tagged(f, sizeof(f), MDH_TEST_WIPE_F);
}

mdh_err_t mdh_generate_keypair(mdh_keypair_t *kp, mdh_rng_fn rng) {
    static const uint8_t basepoint[32] = { 9 };
    mdh_err_t rng_err;

    if (kp == NULL || rng == NULL) {
        return MDH_ERR_RNG;
    }

    rng_err = rng(kp->privkey, sizeof(kp->privkey));
    if (rng_err != MDH_OK) {
        mdh_secure_zero(kp->privkey, sizeof(kp->privkey));
        mdh_secure_zero(kp->pubkey, sizeof(kp->pubkey));
        return MDH_ERR_RNG;
    }
    if (mdh_is_all_zero(kp->privkey, sizeof(kp->privkey))) {
        mdh_secure_zero(kp->privkey, sizeof(kp->privkey));
        mdh_secure_zero(kp->pubkey, sizeof(kp->pubkey));
        return MDH_ERR_RNG;
    }

    mdh_clamp_scalar(kp->privkey);
    mdh_x25519(kp->pubkey, kp->privkey, basepoint);
    if (mdh_is_all_zero(kp->pubkey, sizeof(kp->pubkey))) {
        mdh_secure_zero(kp->privkey, sizeof(kp->privkey));
        mdh_secure_zero(kp->pubkey, sizeof(kp->pubkey));
        return MDH_ERR_RNG;
    }

    return MDH_OK;
}

mdh_err_t mdh_shared_secret(const uint8_t privkey[32],
                            const uint8_t remote_pub[32],
                            uint8_t out_secret[32]) {
    uint8_t normalized_remote[32];

    if (privkey == NULL || remote_pub == NULL || out_secret == NULL) {
        return MDH_ERR_INVALID_KEY;
    }

    memcpy(normalized_remote, remote_pub, sizeof(normalized_remote));
    normalized_remote[31] &= 0x7fU;

    if (mdh_is_all_zero(privkey, 32U) || mdh_is_all_zero(normalized_remote, 32U)) {
        mdh_secure_zero(normalized_remote, sizeof(normalized_remote));
        return MDH_ERR_INVALID_KEY;
    }

    if (mdh_has_small_order_point(normalized_remote)) {
        mdh_secure_zero(normalized_remote, sizeof(normalized_remote));
        return MDH_ERR_WEAK_KEY;
    }

    mdh_x25519(out_secret, privkey, normalized_remote);
    mdh_secure_zero(normalized_remote, sizeof(normalized_remote));
    if (mdh_is_all_zero(out_secret, 32U)) {
        return MDH_ERR_WEAK_KEY;
    }

    return MDH_OK;
}

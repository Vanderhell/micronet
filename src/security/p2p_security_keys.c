#include "p2p_security.h"

#if defined(_WIN32)
#define _CRT_RAND_S
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_system.h"
#include "esp_random.h"
#elif defined(_WIN32)
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#define P2P_SEC_STORE_FILE "p2p_security_store.bin"
#define P2P_SEC_STORE_MAGIC 0x31534350UL
#define P2P_SEC_STORE_PLAIN_BYTES (32U + 32U + (P2P_MAX_GROUPS * (16U + 16U + 1U)))
#define P2P_SEC_STORE_ENCRYPTED_BYTES \
    (((P2P_SEC_STORE_PLAIN_BYTES + P2P_IV_SIZE - 1U) / P2P_IV_SIZE) * P2P_IV_SIZE)

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t group_count;
    uint8_t iv[P2P_IV_SIZE];
    uint8_t encrypted[P2P_SEC_STORE_ENCRYPTED_BYTES];
    uint8_t hmac[P2P_HMAC_SIZE];
} p2p_security_store_blob_t;

static const uint8_t p2p_security_store_key[P2P_SESSION_KEY_SIZE] = {
    0x70, 0x32, 0x70, 0x6c, 0x69, 0x62, 0x2d, 0x73,
    0x74, 0x6f, 0x72, 0x65, 0x2d, 0x6b, 0x65, 0x79
};

static FILE *p2p_security_fopen(const char *path, const char *mode)
{
#if defined(_WIN32) && defined(_MSC_VER)
    FILE *fp = NULL;
    return fopen_s(&fp, path, mode) == 0 ? fp : NULL;
#else
    return fopen(path, mode);
#endif
}

p2p_sec_err_t p2p_security_random_fill(uint8_t *out, size_t len)
{
#if defined(ESP_PLATFORM)
    esp_fill_random(out, len);
    return P2P_SEC_OK;
#elif defined(_WIN32)
    size_t i;
    for (i = 0U; i < len; ++i) {
        unsigned int value = 0U;
        if (rand_s(&value) != 0) {
            return P2P_SEC_ERR_KEYGEN;
        }
        out[i] = (uint8_t)(value & 0xFFU);
    }
    return P2P_SEC_OK;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t got;
    if (fd < 0) {
        return P2P_SEC_ERR_KEYGEN;
    }
    got = read(fd, out, len);
    close(fd);
    return got == (ssize_t)len ? P2P_SEC_OK : P2P_SEC_ERR_KEYGEN;
#endif
}

static mdh_err_t p2p_security_mdh_rng(uint8_t *buf, size_t len)
{
    return p2p_security_random_fill(buf, len) == P2P_SEC_OK ? MDH_OK : MDH_ERR_RNG;
}

static int p2p_security_secure_eq(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0U;
    size_t i;

    for (i = 0U; i < len; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }

    return diff == 0U;
}

p2p_sec_err_t p2p_security_generate_keypair(uint8_t privkey[P2P_NODE_KEY_SIZE],
                                            uint8_t pubkey[P2P_NODE_KEY_SIZE])
{
    if (privkey == NULL || pubkey == NULL) {
        return P2P_SEC_ERR_KEYGEN;
    }

    mdh_keypair_t kp;

    if (mdh_generate_keypair(&kp, p2p_security_mdh_rng) != MDH_OK) {
        return P2P_SEC_ERR_KEYGEN;
    }
    memcpy(privkey, kp.privkey, sizeof(kp.privkey));
    memcpy(pubkey, kp.pubkey, sizeof(kp.pubkey));
    return P2P_SEC_OK;
}

static const uint8_t *p2p_sec_seed_ptr;
static size_t p2p_sec_seed_off;

static mdh_err_t p2p_security_mdh_seed_rng(uint8_t *buf, size_t len)
{
    size_t i;

    if (buf == NULL || p2p_sec_seed_ptr == NULL || len != 32U || p2p_sec_seed_off != 0U) {
        return MDH_ERR_RNG;
    }

    for (i = 0U; i < len; ++i) {
        buf[i] = p2p_sec_seed_ptr[p2p_sec_seed_off + i];
    }
    p2p_sec_seed_off += len;
    return MDH_OK;
}

p2p_sec_err_t p2p_security_derive_pubkey_from_privkey(const uint8_t privkey_in[P2P_NODE_KEY_SIZE],
                                                     uint8_t privkey_out[P2P_NODE_KEY_SIZE],
                                                     uint8_t pubkey_out[P2P_NODE_KEY_SIZE])
{
    mdh_keypair_t kp;

    if (privkey_in == NULL || privkey_out == NULL || pubkey_out == NULL) {
        return P2P_SEC_ERR_KEYGEN;
    }

    p2p_sec_seed_ptr = privkey_in;
    p2p_sec_seed_off = 0U;
    if (mdh_generate_keypair(&kp, p2p_security_mdh_seed_rng) != MDH_OK) {
        p2p_sec_seed_ptr = NULL;
        return P2P_SEC_ERR_KEYGEN;
    }
    p2p_sec_seed_ptr = NULL;

    memcpy(privkey_out, kp.privkey, sizeof(kp.privkey));
    memcpy(pubkey_out, kp.pubkey, sizeof(kp.pubkey));
    return P2P_SEC_OK;
}


p2p_sec_err_t p2p_security_store_keys(const p2p_security_t *ctx)
{
    p2p_security_store_blob_t blob;
    mcrypt_aes128_t aes;
    uint8_t plain[sizeof(blob.encrypted)];
    uint8_t iv[P2P_IV_SIZE];
    FILE *fp;

    if (ctx == NULL || !ctx->store_keys) {
        return P2P_SEC_OK;
    }

    memset(&blob, 0, sizeof(blob));
    memset(plain, 0, sizeof(plain));
    blob.magic = P2P_SEC_STORE_MAGIC;
    blob.version = 3U;
    blob.group_count = ctx->group_count;
    memcpy(plain, ctx->node_privkey, P2P_NODE_KEY_SIZE);
    memcpy(plain + 32U, ctx->node_pubkey, P2P_NODE_KEY_SIZE);
    for (uint8_t i = 0U; i < P2P_MAX_GROUPS; ++i) {
        size_t offset = 64U + (size_t)i * (16U + 16U + 1U);
        memcpy(plain + offset, ctx->groups[i].group_hash, 16U);
        memcpy(plain + offset + 16U, ctx->groups[i].group_key, 16U);
        plain[offset + 32U] = ctx->groups[i].valid ? 1U : 0U;
    }

    if (p2p_security_random_fill(blob.iv, sizeof(blob.iv)) != P2P_SEC_OK) {
        return P2P_SEC_ERR_KEYGEN;
    }

    memcpy(iv, blob.iv, sizeof(iv));
    mcrypt_aes128_init(&aes, p2p_security_store_key);
    mcrypt_aes128_cbc_encrypt(&aes, iv, plain, blob.encrypted, (uint32_t)sizeof(blob.encrypted));
    mcrypt_hmac_sha256(p2p_security_store_key,
                       sizeof(p2p_security_store_key),
                       &blob,
                       (uint32_t)(offsetof(p2p_security_store_blob_t, hmac)),
                       blob.hmac);

    fp = p2p_security_fopen(P2P_SEC_STORE_FILE, "wb");
    if (fp == NULL) {
        return P2P_SEC_ERR_KEYGEN;
    }
    if (fwrite(&blob, sizeof(blob), 1U, fp) != 1U) {
        fclose(fp);
        return P2P_SEC_ERR_KEYGEN;
    }
    fclose(fp);
    return P2P_SEC_OK;
}

p2p_sec_err_t p2p_security_load_keys(p2p_security_t *ctx, bool *loaded)
{
    p2p_security_store_blob_t blob;
    mcrypt_aes128_t aes;
    uint8_t mac[P2P_HMAC_SIZE];
    uint8_t plain[sizeof(blob.encrypted)];
    uint8_t iv[P2P_IV_SIZE];
    FILE *fp;

    if (ctx == NULL || loaded == NULL) {
        return P2P_SEC_ERR_KEYGEN;
    }

    *loaded = false;
    fp = p2p_security_fopen(P2P_SEC_STORE_FILE, "rb");
    if (fp == NULL) {
        return P2P_SEC_OK;
    }
    if (fread(&blob, sizeof(blob), 1U, fp) != 1U) {
        fclose(fp);
        return P2P_SEC_ERR_KEYGEN;
    }
    fclose(fp);

    mcrypt_hmac_sha256(p2p_security_store_key,
                       sizeof(p2p_security_store_key),
                       &blob,
                       (uint32_t)(offsetof(p2p_security_store_blob_t, hmac)),
                       mac);
    if (blob.magic != P2P_SEC_STORE_MAGIC ||
        !p2p_security_secure_eq(mac, blob.hmac, sizeof(mac))) {
        return P2P_SEC_ERR_KEYGEN;
    }

    if (blob.version != 3U) {
        return P2P_SEC_OK;
    }

    memcpy(iv, blob.iv, sizeof(iv));
    mcrypt_aes128_init(&aes, p2p_security_store_key);
    mcrypt_aes128_cbc_decrypt(&aes, iv, blob.encrypted, plain, (uint32_t)sizeof(plain));

    memcpy(ctx->node_privkey, plain, P2P_NODE_KEY_SIZE);
    memcpy(ctx->node_pubkey, plain + 32U, P2P_NODE_KEY_SIZE);
    memset(ctx->groups, 0, sizeof(ctx->groups));
    ctx->group_count = 0U;
    for (uint8_t i = 0U; i < P2P_MAX_GROUPS; ++i) {
        size_t offset = 64U + (size_t)i * (16U + 16U + 1U);
        if (plain[offset + 32U] == 0U) {
            continue;
        }
        memcpy(ctx->groups[i].group_hash, plain + offset, 16U);
        memcpy(ctx->groups[i].group_key, plain + offset + 16U, 16U);
        ctx->groups[i].valid = true;
        ctx->group_count++;
    }
    *loaded = true;
    return P2P_SEC_OK;
}

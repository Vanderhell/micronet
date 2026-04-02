#include "mtest.h"

#include "security/p2p_security.h"

#include <stdio.h>
#include <string.h>

static void remove_key_store(void)
{
    (void)remove("p2p_security_store.bin");
}

static void init_sec(p2p_security_t *ctx, p2p_security_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(ctx, cfg));
}

static p2p_session_t *find_session(p2p_security_t *ctx, const uint8_t remote_pubkey[32])
{
    uint8_t i;
    for (i = 0U; i < P2P_MAX_SESSIONS; ++i) {
        if (memcmp(ctx->sessions[i].remote_pubkey, remote_pubkey, 32U) == 0) {
            return &ctx->sessions[i];
        }
    }
    return NULL;
}

MTEST(test_security_keypair_generation)
{
    p2p_security_t ctx;
    p2p_security_config_t cfg;
    static const uint8_t zero32[32] = {0};

    init_sec(&ctx, &cfg);
    MTEST_ASSERT_TRUE(memcmp(ctx.node_pubkey, zero32, sizeof(zero32)) != 0);
    MTEST_ASSERT_TRUE(memcmp(ctx.node_privkey, ctx.node_pubkey, sizeof(zero32)) != 0);
    p2p_security_deinit(&ctx);
}

MTEST(test_security_handshake_between_nodes)
{
    p2p_security_t a;
    p2p_security_t b;
    p2p_security_config_t cfg_a;
    p2p_security_config_t cfg_b;
    p2p_session_t *sa;
    p2p_session_t *sb;

    init_sec(&a, &cfg_a);
    init_sec(&b, &cfg_b);
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_handshake(&a, b.node_pubkey));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_handshake(&b, a.node_pubkey));
    MTEST_ASSERT_TRUE(p2p_security_is_authenticated(&a, b.node_pubkey));
    MTEST_ASSERT_TRUE(p2p_security_is_authenticated(&b, a.node_pubkey));
    sa = find_session(&a, b.node_pubkey);
    sb = find_session(&b, a.node_pubkey);
    MTEST_ASSERT_NOT_NULL(sa);
    MTEST_ASSERT_NOT_NULL(sb);
    MTEST_ASSERT_MEM_EQ(sa->session_key, sb->session_key, P2P_SESSION_KEY_SIZE);
    p2p_security_deinit(&a);
    p2p_security_deinit(&b);
}

MTEST(test_security_encrypt_decrypt)
{
    p2p_security_t a;
    p2p_security_t b;
    p2p_security_config_t cfg_a;
    p2p_security_config_t cfg_b;
    uint8_t cipher[256];
    uint8_t plain[256];
    size_t cipher_len = sizeof(cipher);
    size_t plain_len = sizeof(plain);
    static const uint8_t msg[] = "secret-message";

    init_sec(&a, &cfg_a);
    init_sec(&b, &cfg_b);
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_handshake(&a, b.node_pubkey));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_handshake(&b, a.node_pubkey));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt(&a, b.node_pubkey, msg, sizeof(msg), cipher, &cipher_len));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_decrypt(&b, a.node_pubkey, cipher, cipher_len, plain, &plain_len));
    MTEST_ASSERT_EQ((int)sizeof(msg), (int)plain_len);
    MTEST_ASSERT_MEM_EQ(msg, plain, sizeof(msg));
    p2p_security_deinit(&a);
    p2p_security_deinit(&b);
}

MTEST(test_security_hmac_forgery_detection)
{
    p2p_security_t a;
    p2p_security_t b;
    p2p_security_config_t cfg_a;
    p2p_security_config_t cfg_b;
    uint8_t cipher[256];
    uint8_t plain[256];
    size_t cipher_len = sizeof(cipher);
    size_t plain_len = sizeof(plain);
    static const uint8_t msg[] = "tamper";

    init_sec(&a, &cfg_a);
    init_sec(&b, &cfg_b);
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_handshake(&a, b.node_pubkey));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_handshake(&b, a.node_pubkey));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt(&a, b.node_pubkey, msg, sizeof(msg), cipher, &cipher_len));
    cipher[cipher_len - 1U] ^= 0x55U;
    MTEST_ASSERT_EQ(P2P_SEC_ERR_HMAC, p2p_security_decrypt(&b, a.node_pubkey, cipher, cipher_len, plain, &plain_len));
    p2p_security_deinit(&a);
    p2p_security_deinit(&b);
}

MTEST(test_security_group_encryption)
{
    p2p_security_t a;
    p2p_security_t b;
    p2p_security_config_t cfg_a;
    p2p_security_config_t cfg_b;
    uint8_t cipher[256];
    uint8_t plain[256];
    size_t cipher_len = sizeof(cipher);
    size_t plain_len = sizeof(plain);
    uint8_t group_key[16];
    static const uint8_t msg[] = "group-secret";

    memset(group_key, 0xAB, sizeof(group_key));
    init_sec(&a, &cfg_a);
    init_sec(&b, &cfg_b);
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_add_group_key(&a, group_key));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_add_group_key(&b, group_key));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_encrypt_group(&a, 0U, msg, sizeof(msg), cipher, &cipher_len));
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_decrypt_group(&b, 0U, cipher, cipher_len, plain, &plain_len));
    MTEST_ASSERT_MEM_EQ(msg, plain, sizeof(msg));
    p2p_security_deinit(&a);
    p2p_security_deinit(&b);
}

MTEST(test_security_unknown_group)
{
    p2p_security_t ctx;
    p2p_security_config_t cfg;
    uint8_t plain[64];
    size_t plain_len = sizeof(plain);
    uint8_t cipher[64];

    init_sec(&ctx, &cfg);
    memset(cipher, 0x11, sizeof(cipher));
    MTEST_ASSERT_EQ(P2P_SEC_ERR_NO_GROUP, p2p_security_decrypt_group(&ctx, 0U, cipher, sizeof(cipher), plain, &plain_len));
    p2p_security_deinit(&ctx);
}

MTEST(test_security_persistent_keys)
{
    p2p_security_t a;
    p2p_security_t b;
    p2p_security_config_t cfg;
    uint8_t pub_a[32];
    uint8_t pub_b[32];

    remove_key_store();
    memset(&cfg, 0, sizeof(cfg));
    cfg.store_keys = true;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&a, &cfg));
    memcpy(pub_a, a.node_pubkey, sizeof(pub_a));
    p2p_security_deinit(&a);

    memset(&cfg, 0, sizeof(cfg));
    cfg.store_keys = true;
    MTEST_ASSERT_EQ(P2P_SEC_OK, p2p_security_init(&b, &cfg));
    memcpy(pub_b, b.node_pubkey, sizeof(pub_b));
    MTEST_ASSERT_MEM_EQ(pub_a, pub_b, sizeof(pub_a));
    p2p_security_deinit(&b);
    remove_key_store();
}

MTEST_SUITE(security)
{
    MTEST_RUN(test_security_keypair_generation);
    MTEST_RUN(test_security_handshake_between_nodes);
    MTEST_RUN(test_security_encrypt_decrypt);
    MTEST_RUN(test_security_hmac_forgery_detection);
    MTEST_RUN(test_security_group_encryption);
    MTEST_RUN(test_security_unknown_group);
    MTEST_RUN(test_security_persistent_keys);
}

void run_security_suite(void)
{
    MTEST_SUITE_RUN(security);
}

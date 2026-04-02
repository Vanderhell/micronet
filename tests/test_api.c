#include "mtest.h"

#include "micronet.h"

#include <string.h>

static int api_online_count;
static int api_offline_count;
static int api_custom_count;
static mnet_err_t api_request_err;
static int api_request_value;
static int api_subscribe_count;
static int api_custom_handler_count;
static uint8_t api_self_id[32];

static void api_on_online(const uint8_t node_id[32])
{
    api_online_count++;
    memcpy(api_self_id, node_id, 32U);
}

static void api_on_offline(const uint8_t node_id[32])
{
    api_offline_count++;
    memcpy(api_self_id, node_id, 32U);
}

static void api_on_custom_msg(const mnet_message_t *msg)
{
    if (msg != NULL) {
        api_custom_count++;
    }
}

static void api_request_cb(mnet_err_t err, const void *value, size_t len)
{
    api_request_err = err;
    if (value != NULL && len == sizeof(api_request_value)) {
        memcpy(&api_request_value, value, len);
    }
}

static void api_subscribe_cb(const char *key, const void *value, size_t len)
{
    (void)key;
    if (value != NULL && len > 0U) {
        api_subscribe_count++;
    }
}

static void api_custom_handler(const uint8_t src[32], const uint8_t *payload, size_t len)
{
    (void)src;
    if (payload != NULL && len > 0U) {
        api_custom_handler_count++;
    }
}

static mnet_config_t api_config(void)
{
    mnet_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.stun_host = "stun.l.google.com";
    cfg.stun_port = 19302U;
    cfg.heartbeat_ms = 5000U;
    cfg.offline_timeout_ms = 15000U;
    cfg.retry_interval_ms = 2000U;
    cfg.retry_count = 3U;
    cfg.max_nodes = 16U;
    cfg.max_vars = 16U;
    cfg.max_pending = 8U;
    cfg.on_node_online = api_on_online;
    cfg.on_node_offline = api_on_offline;
    cfg.on_custom_msg = api_on_custom_msg;
    return cfg;
}

MTEST(test_api_init_deinit)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];
    static const uint8_t zero32[32] = {0};

    api_online_count = 0;
    api_offline_count = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));
    MTEST_ASSERT_TRUE(memcmp(node_id, zero32, sizeof(node_id)) != 0);
    MTEST_ASSERT_EQ(1, api_online_count);
    mnet_deinit();
}

MTEST(test_api_publish_request_end_to_end)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];
    int value = 1234;

    api_request_err = MNET_ERR_INTERNAL;
    api_request_value = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));
    MTEST_ASSERT_EQ(MNET_OK, mnet_publish("val", &value, sizeof(value)));
    MTEST_ASSERT_EQ(MNET_OK, mnet_request(node_id, "val", api_request_cb));
    MTEST_ASSERT_EQ(MNET_OK, api_request_err);
    MTEST_ASSERT_EQ(value, api_request_value);
    mnet_deinit();
}

MTEST(test_api_subscribe_end_to_end)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];
    int value = 1;

    api_subscribe_count = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));
    MTEST_ASSERT_EQ(MNET_OK, mnet_publish("val", &value, sizeof(value)));
    MTEST_ASSERT_EQ(MNET_OK, mnet_subscribe(node_id, "val", api_subscribe_cb));
    value = 2;
    MTEST_ASSERT_EQ(MNET_OK, mnet_update("val", &value, sizeof(value)));
    MTEST_ASSERT_EQ(1, api_subscribe_count);
    mnet_deinit();
}

MTEST(test_api_group_end_to_end)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];
    uint8_t group_hash[16];
    uint8_t group_key[16];

    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));
    MTEST_ASSERT_EQ(MNET_OK, mnet_group_create(group_hash, group_key));
    MTEST_ASSERT_TRUE(mnet_group_is_member(node_id, group_hash));
    mnet_deinit();
}

MTEST(test_api_custom_message_end_to_end)
{
    mnet_config_t cfg = api_config();
    uint8_t node_id[32];
    static const uint8_t payload[] = "fire";

    api_custom_count = 0;
    api_custom_handler_count = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(MNET_OK, mnet_get_node_id(node_id));
    MTEST_ASSERT_EQ(MNET_OK, mnet_register_handler(0x80U, api_custom_handler));
    MTEST_ASSERT_EQ(MNET_OK, mnet_send_custom(node_id, 0x80U, payload, sizeof(payload) - 1U));
    MTEST_ASSERT_EQ(1, api_custom_count);
    MTEST_ASSERT_EQ(1, api_custom_handler_count);
    mnet_deinit();
}

MTEST(test_api_node_online_offline_callbacks)
{
    mnet_config_t cfg = api_config();

    api_online_count = 0;
    api_offline_count = 0;
    MTEST_ASSERT_EQ(MNET_OK, mnet_init(&cfg));
    MTEST_ASSERT_EQ(1, api_online_count);
    mnet_deinit();
    MTEST_ASSERT_EQ(1, api_offline_count);
}

MTEST_SUITE(api)
{
    MTEST_RUN(test_api_init_deinit);
    MTEST_RUN(test_api_publish_request_end_to_end);
    MTEST_RUN(test_api_subscribe_end_to_end);
    MTEST_RUN(test_api_group_end_to_end);
    MTEST_RUN(test_api_custom_message_end_to_end);
    MTEST_RUN(test_api_node_online_offline_callbacks);
}

void run_api_suite(void)
{
    MTEST_SUITE_RUN(api);
}

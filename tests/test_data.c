#include "mtest.h"

#include "data/p2p_data.h"

#include <string.h>

static uint32_t data_fake_now_ms;
static int data_request_err;
static uint8_t data_request_buf[P2P_MAX_VAL_LEN];
static size_t data_request_len;
static int data_notify_count;
static uint8_t data_notify_buf[P2P_MAX_VAL_LEN];
static size_t data_notify_len;
static int data_query_err;
static uint8_t data_query_buf[P2P_MAX_VAL_LEN];
static size_t data_query_len;
static int data_metrics_err;
static p2p_metrics_t data_metrics_value;
static int data_list_err;
static uint8_t data_list_count;
static const char *data_list_names[P2P_MAX_VARS];

static uint32_t test_data_now_ms(void)
{
    return data_fake_now_ms;
}

static void data_request_cb(int err, const void *value, size_t len)
{
    data_request_err = err;
    data_request_len = len;
    if (value != NULL && len > 0U) {
        memcpy(data_request_buf, value, len);
    }
}

static void data_notify_cb(const char *key, const void *value, size_t len)
{
    (void)key;
    data_notify_count++;
    data_notify_len = len;
    if (value != NULL && len > 0U) {
        memcpy(data_notify_buf, value, len);
    }
}

static void data_query_cb(int err, const p2p_row_t *rows, uint8_t count)
{
    data_query_err = err;
    if (rows != NULL && count > 0U) {
        data_query_len = rows[0].len;
        memcpy(data_query_buf, rows[0].bytes, rows[0].len);
    }
}

static void data_metrics_cb(int err, const p2p_metrics_t *metrics)
{
    data_metrics_err = err;
    if (metrics != NULL) {
        data_metrics_value = *metrics;
    }
}

static void data_list_cb(int err, const char **names, uint8_t count)
{
    uint8_t i;
    data_list_err = err;
    data_list_count = count;
    for (i = 0U; i < count; ++i) {
        data_list_names[i] = names[i];
    }
}

static void init_data(p2p_data_t *ctx)
{
    p2p_data_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.max_vars = P2P_MAX_VARS;
    cfg.max_subs = P2P_MAX_SUBS;
    cfg.notify_min_interval_ms = 1000U;
    cfg.compress_data = true;
    cfg.spool_size = 1U;
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_init(ctx, &cfg));
    ctx->now_ms = test_data_now_ms;
    ctx->started_ms = data_fake_now_ms;
}

MTEST(test_data_publish_request)
{
    p2p_data_t ctx;
    uint8_t node_id[32] = {1U};
    float temp = 23.5f;

    data_request_err = 0;
    data_request_len = 0U;
    data_fake_now_ms = 1000U;
    init_data(&ctx);
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_publish(&ctx, "temperature", P2P_DATA_VAR, &temp, sizeof(temp)));
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_request(&ctx, node_id, "temperature", data_request_cb));
    MTEST_ASSERT_EQ(P2P_DATA_OK, data_request_err);
    MTEST_ASSERT_EQ((int)sizeof(temp), (int)data_request_len);
    MTEST_ASSERT_MEM_EQ(&temp, data_request_buf, sizeof(temp));
    p2p_data_deinit(&ctx);
}

MTEST(test_data_subscribe_notify)
{
    p2p_data_t ctx;
    uint8_t node_id[32] = {2U};
    float temp = 24.0f;

    data_fake_now_ms = 2000U;
    data_notify_count = 0;
    init_data(&ctx);
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_publish(&ctx, "temperature", P2P_DATA_VAR, &temp, sizeof(temp)));
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_subscribe(&ctx, node_id, "temperature", data_notify_cb));
    temp = 25.0f;
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_update(&ctx, "temperature", &temp, sizeof(temp)));
    MTEST_ASSERT_EQ(1, data_notify_count);
    MTEST_ASSERT_MEM_EQ(&temp, data_notify_buf, sizeof(temp));
    p2p_data_deinit(&ctx);
}

MTEST(test_data_throttle_notifications)
{
    p2p_data_t ctx;
    uint8_t node_id[32] = {3U};
    int value = 1;

    data_fake_now_ms = 3000U;
    data_notify_count = 0;
    init_data(&ctx);
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_publish(&ctx, "counter", P2P_DATA_VAR, &value, sizeof(value)));
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_subscribe(&ctx, node_id, "counter", data_notify_cb));
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_update(&ctx, "counter", &value, sizeof(value)));
    value = 2;
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_update(&ctx, "counter", &value, sizeof(value)));
    MTEST_ASSERT_EQ(1, data_notify_count);
    data_fake_now_ms += 1000U;
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_update(&ctx, "counter", &value, sizeof(value)));
    MTEST_ASSERT_EQ(2, data_notify_count);
    p2p_data_deinit(&ctx);
}

MTEST(test_data_query_table)
{
    p2p_data_t ctx;
    uint8_t node_id[32] = {4U};
    static const uint8_t row[] = "sensor-row";

    data_fake_now_ms = 4000U;
    data_query_err = 0;
    data_query_len = 0U;
    init_data(&ctx);
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_publish(&ctx, "sensors", P2P_DATA_TABLE, row, sizeof(row)));
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_query(&ctx, node_id, "sensors", "id > 0", data_query_cb));
    MTEST_ASSERT_EQ(P2P_DATA_OK, data_query_err);
    MTEST_ASSERT_EQ((int)sizeof(row), (int)data_query_len);
    MTEST_ASSERT_MEM_EQ(row, data_query_buf, sizeof(row));
    p2p_data_deinit(&ctx);
}

MTEST(test_data_metrics)
{
    p2p_data_t ctx;
    uint8_t node_id[32] = {5U};

    data_fake_now_ms = 5000U;
    memset(&data_metrics_value, 0, sizeof(data_metrics_value));
    data_metrics_err = 0;
    init_data(&ctx);
    data_fake_now_ms += 2500U;
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_get_metrics(&ctx, node_id, data_metrics_cb));
    MTEST_ASSERT_EQ(P2P_DATA_OK, data_metrics_err);
    MTEST_ASSERT_GE(data_metrics_value.uptime_s, 2);
    MTEST_ASSERT_GT(data_metrics_value.free_heap, 0);
    p2p_data_deinit(&ctx);
}

MTEST(test_data_offline_spool)
{
    p2p_data_t ctx;
    uint8_t node_id[32] = {6U};
    int value = 77;

    data_fake_now_ms = 6000U;
    data_notify_count = 0;
    init_data(&ctx);
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_publish(&ctx, "offline", P2P_DATA_VAR, &value, sizeof(value)));
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_subscribe(&ctx, node_id, "offline", NULL));
    value = 88;
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_update(&ctx, "offline", &value, sizeof(value)));
    MTEST_ASSERT_TRUE(ctx.spool.in_use);
    ctx.subs[0].cb = data_notify_cb;
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_tick(&ctx));
    MTEST_ASSERT_EQ(1, data_notify_count);
    MTEST_ASSERT_FALSE(ctx.spool.in_use);
    MTEST_ASSERT_MEM_EQ(&value, data_notify_buf, sizeof(value));
    p2p_data_deinit(&ctx);
}

MTEST(test_data_access_permissions)
{
    p2p_data_t ctx;
    uint8_t node_id[32] = {7U};
    int value = 5;

    data_fake_now_ms = 7000U;
    data_request_err = 0;
    init_data(&ctx);
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_publish(&ctx, "private", P2P_DATA_VAR, &value, sizeof(value)));
    ctx.vars[0].is_public = false;
    ctx.vars[0].access = P2P_ACCESS_GROUP;
    MTEST_ASSERT_EQ(P2P_DATA_ERR_ACCESS, p2p_data_request(&ctx, node_id, "private", data_request_cb));
    MTEST_ASSERT_EQ(P2P_DATA_ERR_ACCESS, data_request_err);
    p2p_data_deinit(&ctx);
}

MTEST(test_data_list_vars)
{
    p2p_data_t ctx;
    uint8_t node_id[32] = {8U};
    uint8_t v = 1U;

    data_fake_now_ms = 8000U;
    data_list_err = 0;
    data_list_count = 0U;
    init_data(&ctx);
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_publish(&ctx, "a", P2P_DATA_VAR, &v, sizeof(v)));
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_publish(&ctx, "b", P2P_DATA_VAR, &v, sizeof(v)));
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_publish(&ctx, "c", P2P_DATA_VAR, &v, sizeof(v)));
    MTEST_ASSERT_EQ(P2P_DATA_OK, p2p_data_list_vars(&ctx, node_id, data_list_cb));
    MTEST_ASSERT_EQ(P2P_DATA_OK, data_list_err);
    MTEST_ASSERT_EQ(3, (int)data_list_count);
    MTEST_ASSERT_STR_EQ("a", data_list_names[0]);
    MTEST_ASSERT_STR_EQ("b", data_list_names[1]);
    MTEST_ASSERT_STR_EQ("c", data_list_names[2]);
    p2p_data_deinit(&ctx);
}

MTEST_SUITE(data)
{
    MTEST_RUN(test_data_publish_request);
    MTEST_RUN(test_data_subscribe_notify);
    MTEST_RUN(test_data_throttle_notifications);
    MTEST_RUN(test_data_query_table);
    MTEST_RUN(test_data_metrics);
    MTEST_RUN(test_data_offline_spool);
    MTEST_RUN(test_data_access_permissions);
    MTEST_RUN(test_data_list_vars);
}

void run_data_suite(void)
{
    MTEST_SUITE_RUN(data);
}

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "micronet.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifdef MICRONET_DEMO_WIFI_SSID
#undef CONFIG_MICRONET_DEMO_WIFI_SSID
#define CONFIG_MICRONET_DEMO_WIFI_SSID MICRONET_DEMO_WIFI_SSID
#endif

#ifdef MICRONET_DEMO_WIFI_PASSWORD
#undef CONFIG_MICRONET_DEMO_WIFI_PASSWORD
#define CONFIG_MICRONET_DEMO_WIFI_PASSWORD MICRONET_DEMO_WIFI_PASSWORD
#endif

#ifdef MICRONET_DEMO_NODE1_IP
#undef CONFIG_MICRONET_DEMO_NODE1_IP
#define CONFIG_MICRONET_DEMO_NODE1_IP MICRONET_DEMO_NODE1_IP
#endif

#ifdef MICRONET_DEMO_NODE2_IP
#undef CONFIG_MICRONET_DEMO_NODE2_IP
#define CONFIG_MICRONET_DEMO_NODE2_IP MICRONET_DEMO_NODE2_IP
#endif

#ifdef MICRONET_DEMO_NODE3_IP
#undef CONFIG_MICRONET_DEMO_NODE3_IP
#define CONFIG_MICRONET_DEMO_NODE3_IP MICRONET_DEMO_NODE3_IP
#endif

#ifdef MICRONET_DEMO_GROUP_HASH_HEX
#undef CONFIG_MICRONET_DEMO_GROUP_HASH_HEX
#define CONFIG_MICRONET_DEMO_GROUP_HASH_HEX MICRONET_DEMO_GROUP_HASH_HEX
#endif

#ifdef MICRONET_DEMO_GROUP_KEY_HEX
#undef CONFIG_MICRONET_DEMO_GROUP_KEY_HEX
#define CONFIG_MICRONET_DEMO_GROUP_KEY_HEX MICRONET_DEMO_GROUP_KEY_HEX
#endif

#define DEMO_WIFI_CONNECTED_BIT BIT0
#define DEMO_WIFI_FAIL_BIT BIT1
#define DEMO_MAX_LINE 256
#define DEMO_CUSTOM_MSG_TYPE 0x80U
#define DEMO_LOCAL_COUNTER_KEY "counter"
#define DEMO_LOCAL_TEMP_KEY "temperature_centi"
#define DEMO_LOCAL_TEXT_KEY "last_text"

typedef struct {
    uint8_t slot;
    char name[8];
    char ip_text[16];
    uint8_t ip[4];
    uint16_t port;
    uint8_t node_id[32];
    bool node_id_set;
    bool configured;
} demo_peer_t;

typedef struct {
    uint8_t slot;
    char name[8];
    uint32_t local_counter;
    int local_temp_centi;
    uint32_t last_publish_ms;
    uint32_t last_discover_ms;
    mnet_group_seed_t group_seed;
    bool group_seed_valid;
    demo_peer_t peers[3];
} demo_app_t;

static const char *TAG = "micronet_demo";
static EventGroupHandle_t s_wifi_events;
static int s_wifi_retries;
static bool s_wifi_connected;
static demo_app_t s_demo;

static void demo_wifi_event_handler(void *arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data);
static bool demo_wifi_start(void);

static uint32_t demo_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void demo_hex_encode(const uint8_t *src, size_t len, char *out, size_t out_len)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (out == NULL || out_len == 0U) {
        return;
    }

    if (src == NULL || out_len < ((len * 2U) + 1U)) {
        out[0] = '\0';
        return;
    }

    for (i = 0U; i < len; ++i) {
        out[i * 2U] = hex[(src[i] >> 4U) & 0x0FU];
        out[i * 2U + 1U] = hex[src[i] & 0x0FU];
    }
    out[len * 2U] = '\0';
}

static bool demo_hex_decode(const char *text, uint8_t *out, size_t out_len)
{
    size_t i;

    if (text == NULL || out == NULL || strlen(text) != (out_len * 2U)) {
        return false;
    }

    for (i = 0U; i < out_len; ++i) {
        unsigned hi;
        unsigned lo;
        char c_hi = text[i * 2U];
        char c_lo = text[i * 2U + 1U];

        if (c_hi >= '0' && c_hi <= '9') {
            hi = (unsigned)(c_hi - '0');
        } else if (c_hi >= 'a' && c_hi <= 'f') {
            hi = (unsigned)(c_hi - 'a' + 10U);
        } else if (c_hi >= 'A' && c_hi <= 'F') {
            hi = (unsigned)(c_hi - 'A' + 10U);
        } else {
            return false;
        }

        if (c_lo >= '0' && c_lo <= '9') {
            lo = (unsigned)(c_lo - '0');
        } else if (c_lo >= 'a' && c_lo <= 'f') {
            lo = (unsigned)(c_lo - 'a' + 10U);
        } else if (c_lo >= 'A' && c_lo <= 'F') {
            lo = (unsigned)(c_lo - 'A' + 10U);
        } else {
            return false;
        }

        out[i] = (uint8_t)((hi << 4U) | lo);
    }

    return true;
}

static bool demo_parse_ipv4(const char *text, uint8_t out[4])
{
    unsigned a;
    unsigned b;
    unsigned c;
    unsigned d;

    if (text == NULL || out == NULL) {
        return false;
    }

    if (sscanf(text, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return false;
    }
    if (a > 255U || b > 255U || c > 255U || d > 255U) {
        return false;
    }

    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)c;
    out[3] = (uint8_t)d;
    return true;
}

static const char *demo_configured_ip(uint8_t slot)
{
    switch (slot) {
        case 1:
            return CONFIG_MICRONET_DEMO_NODE1_IP;
        case 2:
            return CONFIG_MICRONET_DEMO_NODE2_IP;
        case 3:
            return CONFIG_MICRONET_DEMO_NODE3_IP;
        default:
            return "";
    }
}

static demo_peer_t *demo_peer_by_slot(uint8_t slot)
{
    if (slot < 1U || slot > 3U) {
        return NULL;
    }
    return &s_demo.peers[slot - 1U];
}

static void demo_refresh_local_vars(void)
{
    (void)mnet_publish(DEMO_LOCAL_COUNTER_KEY, &s_demo.local_counter, sizeof(s_demo.local_counter));
    (void)mnet_publish(DEMO_LOCAL_TEMP_KEY, &s_demo.local_temp_centi, sizeof(s_demo.local_temp_centi));
}

static void demo_store_last_text(const char *text)
{
    if (text != NULL) {
        (void)mnet_update(DEMO_LOCAL_TEXT_KEY, text, strlen(text) + 1U);
    }
}

static void demo_on_online(const uint8_t node_id[32])
{
    char hex[65];

    demo_hex_encode(node_id, 32U, hex, sizeof(hex));
    printf("MNET_DEMO|node=%u|event=online|node_id=%s\n", (unsigned)s_demo.slot, hex);
}

static void demo_on_offline(const uint8_t node_id[32])
{
    char hex[65];

    demo_hex_encode(node_id, 32U, hex, sizeof(hex));
    printf("MNET_DEMO|node=%u|event=offline|node_id=%s\n", (unsigned)s_demo.slot, hex);
}

static void demo_on_custom_msg(const mnet_message_t *msg)
{
    char src_hex[65];
    char text[129];
    size_t len;

    if (msg == NULL) {
        return;
    }

    demo_hex_encode(msg->src, 32U, src_hex, sizeof(src_hex));
    len = msg->payload_len;
    if (len >= sizeof(text)) {
        len = sizeof(text) - 1U;
    }
    if (len > 0U) {
        memcpy(text, msg->payload, len);
    }
    text[len] = '\0';
    printf("MNET_DEMO|node=%u|event=custom|src=%s|type=%u|text=%s\n",
           (unsigned)s_demo.slot,
           src_hex,
           (unsigned)msg->type,
           text);
}

static void demo_on_request(mnet_err_t err, const void *value, size_t len)
{
    if (err != MNET_OK) {
        printf("MNET_DEMO|node=%u|event=request|result=fail|err=%d\n", (unsigned)s_demo.slot, (int)err);
        return;
    }

    printf("MNET_DEMO|node=%u|event=request|result=ok|len=%u\n", (unsigned)s_demo.slot, (unsigned)len);
    if (value != NULL && len > 0U) {
        printf("MNET_DEMO|node=%u|event=request_value|%.*s\n",
               (unsigned)s_demo.slot,
               (int)len,
               (const char *)value);
    }
}

static void demo_on_list_vars(mnet_err_t err, const char **names, uint8_t count)
{
    uint8_t i;

    printf("MNET_DEMO|node=%u|event=list_vars|err=%d|count=%u\n",
           (unsigned)s_demo.slot,
           (int)err,
           (unsigned)count);
    for (i = 0U; i < count && names != NULL; ++i) {
        printf("MNET_DEMO|node=%u|event=var|name=%s\n", (unsigned)s_demo.slot, names[i]);
    }
}

static void demo_on_query(mnet_err_t err, const mnet_row_t *rows, uint8_t count)
{
    uint8_t i;

    printf("MNET_DEMO|node=%u|event=query|err=%d|count=%u\n",
           (unsigned)s_demo.slot,
           (int)err,
           (unsigned)count);
    for (i = 0U; i < count && rows != NULL; ++i) {
        printf("MNET_DEMO|node=%u|event=row|len=%u\n", (unsigned)s_demo.slot, (unsigned)rows[i].len);
    }
}

static void demo_on_metrics(mnet_err_t err, const mnet_metrics_t *metrics)
{
    if (err != MNET_OK || metrics == NULL) {
        printf("MNET_DEMO|node=%u|event=metrics|err=%d\n", (unsigned)s_demo.slot, (int)err);
        return;
    }

    printf("MNET_DEMO|node=%u|event=metrics|uptime_s=%u|free_heap=%u|online=%u|groups=%u|health=%u\n",
           (unsigned)s_demo.slot,
           (unsigned)metrics->uptime_s,
           (unsigned)metrics->free_heap,
           (unsigned)metrics->online_peers,
           (unsigned)metrics->group_count,
           (unsigned)metrics->health_score);
}

static bool demo_load_group_seed(mnet_group_seed_t *seed)
{
#if defined(MICRONET_DEMO_GROUP_HASH_HEX) && defined(MICRONET_DEMO_GROUP_KEY_HEX)
    if (seed == NULL) {
        return false;
    }
    if (!demo_hex_decode(CONFIG_MICRONET_DEMO_GROUP_HASH_HEX, seed->group_hash, sizeof(seed->group_hash))) {
        return false;
    }
    if (!demo_hex_decode(CONFIG_MICRONET_DEMO_GROUP_KEY_HEX, seed->group_key, sizeof(seed->group_key))) {
        return false;
    }
    return true;
#else
    (void)seed;
    return false;
#endif
}

static void demo_wifi_event_handler(void *arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retries < 10) {
            esp_wifi_connect();
            s_wifi_retries++;
        } else {
            xEventGroupSetBits(s_wifi_events, DEMO_WIFI_FAIL_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_retries = 0;
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_events, DEMO_WIFI_CONNECTED_BIT);
    }
}

static bool demo_wifi_start(void)
{
    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    EventBits_t bits;

    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", CONFIG_MICRONET_DEMO_WIFI_SSID);
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", CONFIG_MICRONET_DEMO_WIFI_PASSWORD);

    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    {
        wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &demo_wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &demo_wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    bits = xEventGroupWaitBits(s_wifi_events,
                               DEMO_WIFI_CONNECTED_BIT | DEMO_WIFI_FAIL_BIT,
                               pdFALSE,
                               pdFALSE,
                               pdMS_TO_TICKS(30000));
    return (bits & DEMO_WIFI_CONNECTED_BIT) != 0U;
}

static bool demo_mnet_init(void)
{
    mnet_config_t cfg;
    uint8_t slot;

    memset(&cfg, 0, sizeof(cfg));
    cfg.node_name = s_demo.name;
    cfg.network_mode = MNET_MODE_LAN_ONLY;
    cfg.stun_enabled = false;
    cfg.local_port = (uint16_t)CONFIG_MICRONET_DEMO_UDP_PORT;
    cfg.heartbeat_ms = 5000U;
    cfg.offline_timeout_ms = 15000U;
    cfg.retry_interval_ms = 2000U;
    cfg.retry_count = (uint8_t)CONFIG_MICRONET_DEMO_RETRY_COUNT;
    cfg.max_nodes = 16U;
    cfg.max_vars = 16U;
    cfg.max_pending = 8U;
    cfg.on_node_online = demo_on_online;
    cfg.on_node_offline = demo_on_offline;
    cfg.on_custom_msg = demo_on_custom_msg;

    if (s_demo.group_seed_valid) {
        cfg.groups[0] = s_demo.group_seed;
        cfg.group_count = 1U;
    }

    if (mnet_init(&cfg) != MNET_OK) {
        ESP_LOGE(TAG, "mnet_init failed");
        return false;
    }

    for (slot = 1U; slot <= 3U; ++slot) {
        demo_peer_t *peer = demo_peer_by_slot(slot);
        if (peer == NULL || !peer->configured) {
            continue;
        }
        (void)mnet_peer_add_ip(NULL, peer->ip, peer->port);
    }

    return true;
}

static void demo_print_status(void)
{
    mnet_peer_info_t peers[MNET_MAX_NODES];
    uint8_t peer_count = 0U;
    uint8_t i;
    uint8_t self_id[32];
    char hex[65];

    printf("Node %u (%s)\n", (unsigned)s_demo.slot, s_demo.name);
    printf("  wifi=%s\n", s_wifi_connected ? "connected" : "disconnected");
    printf("  local_port=%u\n", (unsigned)CONFIG_MICRONET_DEMO_UDP_PORT);
    printf("  group_seed=%s\n", s_demo.group_seed_valid ? "yes" : "no");
    printf("  local_counter=%lu temp=%.2fC\n",
           (unsigned long)s_demo.local_counter,
           (double)s_demo.local_temp_centi / 100.0);

    if (mnet_get_node_id(self_id) == MNET_OK) {
        demo_hex_encode(self_id, 32U, hex, sizeof(hex));
        printf("  node_id=%s\n", hex);
    }

    if (mnet_peer_list(peers, (uint8_t)MNET_MAX_NODES, &peer_count) == MNET_OK) {
        for (i = 0U; i < peer_count; ++i) {
            demo_hex_encode(peers[i].node_id, 32U, hex, sizeof(hex));
            printf("  peer id=%s ip=%u.%u.%u.%u port=%u online=%s auth=%s groups=%u\n",
                   hex,
                   (unsigned)peers[i].ip[0],
                   (unsigned)peers[i].ip[1],
                   (unsigned)peers[i].ip[2],
                   (unsigned)peers[i].ip[3],
                   (unsigned)peers[i].port,
                   peers[i].is_online ? "yes" : "no",
                   peers[i].is_authorized ? "yes" : "no",
                   (unsigned)peers[i].group_count);
        }
    }
}

static void demo_print_help(void)
{
    puts("Commands:");
    puts("  help");
    puts("  status");
    puts("  whoami");
    puts("  peers");
    puts("  discover");
    puts("  pair <slot> <node_id_hex>");
    puts("  publish <key> <text>");
    puts("  update <key> <text>");
    puts("  request <slot> <key>");
    puts("  list <slot>");
    puts("  query <slot> <table>");
    puts("  metrics <slot>");
    puts("  custom <slot> <text>");
    puts("  group create");
    puts("  group leave");
    puts("  invite <slot>");
    puts("  accept");
    puts("  reject");
}

static void demo_bind_peer(demo_peer_t *peer)
{
    if (peer == NULL || !peer->configured || !peer->node_id_set) {
        return;
    }

    (void)mnet_peer_add_ip(peer->node_id, peer->ip, peer->port);
    (void)mnet_peer_authorize(peer->node_id, true);
    (void)mnet_peer_connect(peer->node_id);
}

static void demo_send_custom(uint8_t slot, const char *text)
{
    demo_peer_t *peer = demo_peer_by_slot(slot);

    if (peer == NULL || !peer->node_id_set || text == NULL) {
        return;
    }

    (void)mnet_send_custom(peer->node_id, DEMO_CUSTOM_MSG_TYPE, (const uint8_t *)text, strlen(text));
}

static void demo_request_peer(uint8_t slot, const char *key)
{
    demo_peer_t *peer = demo_peer_by_slot(slot);

    if (peer == NULL || !peer->node_id_set || key == NULL) {
        return;
    }

    (void)mnet_request(peer->node_id, key, demo_on_request);
}

static void demo_list_peer(uint8_t slot)
{
    demo_peer_t *peer = demo_peer_by_slot(slot);

    if (peer == NULL || !peer->node_id_set) {
        return;
    }

    (void)mnet_list_vars(peer->node_id, demo_on_list_vars);
}

static void demo_query_peer(uint8_t slot, const char *table)
{
    demo_peer_t *peer = demo_peer_by_slot(slot);

    if (peer == NULL || !peer->node_id_set || table == NULL) {
        return;
    }

    (void)mnet_query(peer->node_id, table, NULL, demo_on_query);
}

static void demo_metrics_peer(uint8_t slot)
{
    demo_peer_t *peer = demo_peer_by_slot(slot);

    if (peer == NULL || !peer->node_id_set) {
        return;
    }

    (void)mnet_get_metrics(peer->node_id, demo_on_metrics);
}

static void demo_handle_command(char *line)
{
    char *cmd;

    cmd = strtok(line, " ");
    if (cmd == NULL) {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        demo_print_help();
        return;
    }
    if (strcmp(cmd, "status") == 0) {
        demo_print_status();
        return;
    }
    if (strcmp(cmd, "whoami") == 0) {
        uint8_t self_id[32];
        char hex[65];

        if (mnet_get_node_id(self_id) == MNET_OK) {
            demo_hex_encode(self_id, 32U, hex, sizeof(hex));
            printf("MNET_DEMO|node=%u|event=whoami|node_id=%s\n", (unsigned)s_demo.slot, hex);
        }
        return;
    }
    if (strcmp(cmd, "peers") == 0) {
        demo_print_status();
        return;
    }
    if (strcmp(cmd, "discover") == 0) {
        (void)mnet_discover_lan();
        return;
    }
    if (strcmp(cmd, "pair") == 0) {
        char *slot_text = strtok(NULL, " ");
        char *id_hex = strtok(NULL, " ");
        demo_peer_t *peer;

        if (slot_text == NULL || id_hex == NULL) {
            return;
        }
        peer = demo_peer_by_slot((uint8_t)strtoul(slot_text, NULL, 10));
        if (peer == NULL || !demo_hex_decode(id_hex, peer->node_id, sizeof(peer->node_id))) {
            return;
        }
        peer->node_id_set = true;
        demo_bind_peer(peer);
        return;
    }
    if (strcmp(cmd, "publish") == 0) {
        char *key = strtok(NULL, " ");
        char *text = strtok(NULL, "");
        if (key != NULL && text != NULL) {
            (void)mnet_publish(key, text, strlen(text) + 1U);
        }
        return;
    }
    if (strcmp(cmd, "update") == 0) {
        char *key = strtok(NULL, " ");
        char *text = strtok(NULL, "");
        if (key != NULL && text != NULL) {
            (void)mnet_update(key, text, strlen(text) + 1U);
        }
        return;
    }
    if (strcmp(cmd, "request") == 0) {
        char *slot_text = strtok(NULL, " ");
        char *key = strtok(NULL, "");
        if (slot_text != NULL && key != NULL) {
            demo_request_peer((uint8_t)strtoul(slot_text, NULL, 10), key);
        }
        return;
    }
    if (strcmp(cmd, "list") == 0) {
        char *slot_text = strtok(NULL, " ");
        if (slot_text != NULL) {
            demo_list_peer((uint8_t)strtoul(slot_text, NULL, 10));
        }
        return;
    }
    if (strcmp(cmd, "query") == 0) {
        char *slot_text = strtok(NULL, " ");
        char *table = strtok(NULL, "");
        if (slot_text != NULL && table != NULL) {
            demo_query_peer((uint8_t)strtoul(slot_text, NULL, 10), table);
        }
        return;
    }
    if (strcmp(cmd, "metrics") == 0) {
        char *slot_text = strtok(NULL, " ");
        if (slot_text != NULL) {
            demo_metrics_peer((uint8_t)strtoul(slot_text, NULL, 10));
        }
        return;
    }
    if (strcmp(cmd, "custom") == 0) {
        char *slot_text = strtok(NULL, " ");
        char *text = strtok(NULL, "");
        if (slot_text != NULL && text != NULL) {
            demo_send_custom((uint8_t)strtoul(slot_text, NULL, 10), text);
        }
        return;
    }
    if (strcmp(cmd, "group") == 0) {
        char *mode = strtok(NULL, " ");
        char hash_hex[33];
        char key_hex[33];

        if (mode == NULL) {
            return;
        }
        if (strcmp(mode, "create") == 0) {
            if (mnet_group_create(s_demo.group_seed.group_hash, s_demo.group_seed.group_key) == MNET_OK) {
                s_demo.group_seed_valid = true;
                demo_hex_encode(s_demo.group_seed.group_hash, sizeof(s_demo.group_seed.group_hash), hash_hex, sizeof(hash_hex));
                demo_hex_encode(s_demo.group_seed.group_key, sizeof(s_demo.group_seed.group_key), key_hex, sizeof(key_hex));
                printf("MNET_DEMO|node=%u|event=group_create|hash=%s|key=%s\n",
                       (unsigned)s_demo.slot,
                       hash_hex,
                       key_hex);
            }
            return;
        }
        if (strcmp(mode, "leave") == 0 && s_demo.group_seed_valid) {
            (void)mnet_group_leave(s_demo.group_seed.group_hash);
            return;
        }
        return;
    }
    if (strcmp(cmd, "invite") == 0) {
        char *slot_text = strtok(NULL, " ");
        demo_peer_t *peer;

        if (slot_text == NULL || !s_demo.group_seed_valid) {
            return;
        }
        peer = demo_peer_by_slot((uint8_t)strtoul(slot_text, NULL, 10));
        if (peer != NULL && peer->node_id_set) {
            (void)mnet_group_invite(peer->node_id, s_demo.group_seed.group_hash);
        }
        return;
    }
    if (strcmp(cmd, "accept") == 0 && s_demo.group_seed_valid) {
        (void)mnet_group_accept_invite(s_demo.group_seed.group_hash);
        return;
    }
    if (strcmp(cmd, "reject") == 0 && s_demo.group_seed_valid) {
        (void)mnet_group_reject_invite(s_demo.group_seed.group_hash);
        return;
    }
}

static void demo_console_task(void *arg)
{
    char line[DEMO_MAX_LINE];

    (void)arg;
    demo_print_help();

    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') {
            demo_handle_command(line);
        }
    }
}

static void demo_periodic_task(void *arg)
{
    (void)arg;

    for (;;) {
        uint32_t now_ms = demo_now_ms();
        uint8_t slot;

        (void)mnet_tick();

        if ((now_ms - s_demo.last_publish_ms) >= 2000U) {
            s_demo.last_publish_ms = now_ms;
            s_demo.local_counter++;
            s_demo.local_temp_centi = 2100 + (int)(s_demo.local_counter % 250U);
            demo_refresh_local_vars();
            demo_store_last_text("periodic");
        }

        if ((now_ms - s_demo.last_discover_ms) >= 10000U) {
            s_demo.last_discover_ms = now_ms;
            (void)mnet_discover_lan();
        }

        for (slot = 1U; slot <= 3U; ++slot) {
            demo_peer_t *peer = demo_peer_by_slot(slot);

            if (peer != NULL && peer->configured && peer->node_id_set) {
                peer->configured = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void demo_init_peers(void)
{
    uint8_t slot;

    memset(&s_demo, 0, sizeof(s_demo));
    s_demo.slot = (uint8_t)CONFIG_MICRONET_DEMO_NODE_SLOT;
    snprintf(s_demo.name, sizeof(s_demo.name), "node-%u", (unsigned)s_demo.slot);
    s_demo.local_temp_centi = 2150;

    if (demo_load_group_seed(&s_demo.group_seed)) {
        s_demo.group_seed_valid = true;
    }

    for (slot = 1U; slot <= 3U; ++slot) {
        demo_peer_t *peer = demo_peer_by_slot(slot);

        if (peer == NULL) {
            continue;
        }
        memset(peer, 0, sizeof(*peer));
        peer->slot = slot;
        snprintf(peer->name, sizeof(peer->name), "node-%u", (unsigned)slot);
        snprintf(peer->ip_text, sizeof(peer->ip_text), "%s", demo_configured_ip(slot));
        peer->configured = demo_parse_ipv4(peer->ip_text, peer->ip);
        peer->port = (uint16_t)CONFIG_MICRONET_DEMO_UDP_PORT;
    }
}

void app_main(void)
{
    esp_err_t err;

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    demo_init_peers();
    if (!demo_wifi_start()) {
        ESP_LOGE(TAG, "Wi-Fi setup failed");
        return;
    }
    if (!demo_mnet_init()) {
        return;
    }

    demo_refresh_local_vars();
    demo_store_last_text("boot");
    printf("MNET_DEMO|node=%u|event=boot\n", (unsigned)s_demo.slot);

    xTaskCreate(demo_periodic_task, "demo_periodic", 4096, NULL, 5, NULL);
    xTaskCreate(demo_console_task, "demo_console", 4096, NULL, 4, NULL);
}

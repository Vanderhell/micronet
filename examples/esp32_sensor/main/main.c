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

#include "data/p2p_data.h"
#include "transport/p2p_transport.h"

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

#define DEMO_MAX_LINE 192
#define DEMO_WIFI_CONNECTED_BIT BIT0
#define DEMO_WIFI_FAIL_BIT BIT1
#define DEMO_LOCAL_STORE_KEY_TEMP "temperature_centi"
#define DEMO_LOCAL_STORE_KEY_COUNT "counter"
#define DEMO_LOCAL_STORE_KEY_LAST_TEXT "last_text"

typedef struct {
    uint8_t slot;
    char name[8];
    char ip_text[16];
    uint8_t ip[4];
    bool configured;
    bool online;
    uint32_t last_seen_ms;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t ping_count;
    uint32_t last_counter;
    int last_temp_centi;
    char last_text[96];
} demo_peer_t;

typedef struct {
    p2p_transport_t transport;
    p2p_data_t data;
    demo_peer_t peers[3];
    uint8_t self_slot;
    char self_name[8];
    uint32_t local_counter;
    uint32_t local_sequence;
    int local_temp_centi;
    uint32_t last_telemetry_ms;
    uint32_t last_hello_ms;
} demo_app_t;

static const char *TAG = "micronet_demo";
static EventGroupHandle_t s_wifi_events;
static int s_wifi_retries;
static demo_app_t s_demo;

/* All runtime timestamps use the same monotonic clock so peer liveness checks stay simple. */
static uint32_t demo_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void demo_log_simple(const char *event)
{
    printf("MNET_DEMO|node=%u|event=%s\n", (unsigned)s_demo.self_slot, event);
}

static void demo_log_peer(const char *event,
                          uint8_t peer_slot,
                          const char *kind,
                          const char *detail)
{
    if (detail != NULL && detail[0] != '\0') {
        printf("MNET_DEMO|node=%u|event=%s|peer=%u|kind=%s|detail=%s\n",
               (unsigned)s_demo.self_slot,
               event,
               (unsigned)peer_slot,
               kind,
               detail);
    } else {
        printf("MNET_DEMO|node=%u|event=%s|peer=%u|kind=%s\n",
               (unsigned)s_demo.self_slot,
               event,
               (unsigned)peer_slot,
               kind);
    }
}

static void demo_log_rx(uint8_t peer_slot,
                        const char *kind,
                        const char *extra)
{
    if (extra != NULL && extra[0] != '\0') {
        printf("MNET_DEMO|node=%u|event=rx|peer=%u|kind=%s|%s\n",
               (unsigned)s_demo.self_slot,
               (unsigned)peer_slot,
               kind,
               extra);
    } else {
        printf("MNET_DEMO|node=%u|event=rx|peer=%u|kind=%s\n",
               (unsigned)s_demo.self_slot,
               (unsigned)peer_slot,
               kind);
    }
}

static void demo_log_tx(uint8_t peer_slot,
                        const char *kind,
                        const char *extra)
{
    if (extra != NULL && extra[0] != '\0') {
        printf("MNET_DEMO|node=%u|event=tx|peer=%u|kind=%s|%s\n",
               (unsigned)s_demo.self_slot,
               (unsigned)peer_slot,
               kind,
               extra);
    } else {
        printf("MNET_DEMO|node=%u|event=tx|peer=%u|kind=%s\n",
               (unsigned)s_demo.self_slot,
               (unsigned)peer_slot,
               kind);
    }
}

static demo_peer_t *demo_peer_by_slot(uint8_t slot)
{
    if (slot < 1U || slot > 3U) {
        return NULL;
    }
    return &s_demo.peers[slot - 1U];
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

static void demo_store_text_value(const char *text)
{
    if (text == NULL) {
        return;
    }
    (void)p2p_data_update(&s_demo.data, DEMO_LOCAL_STORE_KEY_LAST_TEXT, text, strlen(text) + 1U);
}

static void demo_refresh_local_store(void)
{
    (void)p2p_data_update(&s_demo.data,
                          DEMO_LOCAL_STORE_KEY_TEMP,
                          &s_demo.local_temp_centi,
                          sizeof(s_demo.local_temp_centi));
    (void)p2p_data_update(&s_demo.data,
                          DEMO_LOCAL_STORE_KEY_COUNT,
                          &s_demo.local_counter,
                          sizeof(s_demo.local_counter));
}

/*
 * The demo uses micronet's transport layer as a reliable UDP hop between boards.
 * The payload itself is kept human-readable on purpose so UART logs are easy to inspect.
 */
static esp_err_t demo_send_payload(uint8_t peer_slot,
                                   const char *kind,
                                   const char *payload)
{
    demo_peer_t *peer = demo_peer_by_slot(peer_slot);
    p2p_err_t err;

    if (peer == NULL || !peer->configured || payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = p2p_transport_send(&s_demo.transport,
                             peer->ip,
                             (uint16_t)CONFIG_MICRONET_DEMO_UDP_PORT,
                             (const uint8_t *)payload,
                             strlen(payload));
    if (err != P2P_OK) {
        demo_log_peer("error", peer_slot, kind, "transport_send_failed");
        return ESP_FAIL;
    }

    peer->tx_count++;
    demo_log_tx(peer_slot, kind, payload);
    return ESP_OK;
}

static void demo_send_to_all(const char *kind, const char *payload)
{
    uint8_t slot;

    for (slot = 1U; slot <= 3U; ++slot) {
        if (slot == s_demo.self_slot) {
            continue;
        }
        (void)demo_send_payload(slot, kind, payload);
    }
}

static void demo_send_hello(void)
{
    char line[DEMO_MAX_LINE];

    snprintf(line, sizeof(line), "HELLO|%u|%s", (unsigned)s_demo.self_slot, s_demo.self_name);
    demo_send_to_all("hello", line);
}

static void demo_send_ping(uint8_t peer_slot)
{
    char line[DEMO_MAX_LINE];

    snprintf(line, sizeof(line), "PING|%u|%lu", (unsigned)s_demo.self_slot, (unsigned long)++s_demo.local_sequence);
    (void)demo_send_payload(peer_slot, "ping", line);
}

static void demo_send_text(uint8_t peer_slot, const char *text)
{
    char line[DEMO_MAX_LINE];

    snprintf(line,
             sizeof(line),
             "TEXT|%u|%lu|%s",
             (unsigned)s_demo.self_slot,
             (unsigned long)++s_demo.local_sequence,
             text != NULL ? text : "");
    if (peer_slot == 0U) {
        demo_send_to_all("text", line);
    } else {
        (void)demo_send_payload(peer_slot, "text", line);
    }
    demo_store_text_value(text != NULL ? text : "");
}

static void demo_send_telemetry(void)
{
    char line[DEMO_MAX_LINE];

    s_demo.local_counter++;
    demo_refresh_local_store();
    snprintf(line,
             sizeof(line),
             "TELEM|%u|%lu|%lu|%d|%lu",
             (unsigned)s_demo.self_slot,
             (unsigned long)++s_demo.local_sequence,
             (unsigned long)s_demo.local_counter,
             s_demo.local_temp_centi,
             (unsigned long)demo_now_ms());
    demo_send_to_all("telemetry", line);
}

static void demo_print_status(void)
{
    uint8_t slot;

    printf("Node %u (%s)\n", (unsigned)s_demo.self_slot, s_demo.self_name);
    printf("  udp_port=%d\n", CONFIG_MICRONET_DEMO_UDP_PORT);
    printf("  telemetry_period_ms=%d\n", CONFIG_MICRONET_DEMO_TELEMETRY_PERIOD_MS);
    printf("  local_counter=%lu\n", (unsigned long)s_demo.local_counter);
    printf("  local_temp=%.2f C\n", (double)s_demo.local_temp_centi / 100.0);
    for (slot = 1U; slot <= 3U; ++slot) {
        demo_peer_t *peer = demo_peer_by_slot(slot);
        if (peer == NULL) {
            continue;
        }
        printf("  peer%u ip=%s online=%s tx=%lu rx=%lu last_counter=%lu last_temp=%.2fC\n",
               (unsigned)peer->slot,
               peer->ip_text,
               peer->online ? "yes" : "no",
               (unsigned long)peer->tx_count,
               (unsigned long)peer->rx_count,
               (unsigned long)peer->last_counter,
               (double)peer->last_temp_centi / 100.0);
    }
}

static void demo_print_help(void)
{
    puts("Commands:");
    puts("  help");
    puts("  status");
    puts("  peers");
    puts("  hello");
    puts("  pingall");
    puts("  ping <slot>");
    puts("  send <slot> <text>");
    puts("  all <text>");
    puts("  temp <value>");
    puts("  counter");
    puts("  snapshot");
    puts("  whoami");
}

static void demo_print_snapshot(void)
{
    uint8_t slot;

    for (slot = 1U; slot <= 3U; ++slot) {
        demo_peer_t *peer = demo_peer_by_slot(slot);
        if (peer == NULL) {
            continue;
        }
        printf("MNET_DEMO|node=%u|event=snapshot|peer=%u|online=%u|tx=%lu|rx=%lu|last_counter=%lu|last_temp=%d\n",
               (unsigned)s_demo.self_slot,
               (unsigned)peer->slot,
               peer->online ? 1U : 0U,
               (unsigned long)peer->tx_count,
               (unsigned long)peer->rx_count,
               (unsigned long)peer->last_counter,
               peer->last_temp_centi);
    }
}

static void demo_mark_peer_seen(demo_peer_t *peer)
{
    if (peer == NULL) {
        return;
    }

    peer->online = true;
    peer->last_seen_ms = demo_now_ms();
}

static void demo_handle_ping(demo_peer_t *peer)
{
    char line[DEMO_MAX_LINE];

    if (peer == NULL) {
        return;
    }

    peer->ping_count++;
    snprintf(line, sizeof(line), "PONG|%u|%lu", (unsigned)s_demo.self_slot, (unsigned long)demo_now_ms());
    (void)demo_send_payload(peer->slot, "pong", line);
}

static void demo_handle_telemetry(demo_peer_t *peer,
                                  const char *counter_text,
                                  const char *temp_text)
{
    char details[96];

    if (peer == NULL || counter_text == NULL || temp_text == NULL) {
        return;
    }

    peer->last_counter = (uint32_t)strtoul(counter_text, NULL, 10);
    peer->last_temp_centi = (int)strtol(temp_text, NULL, 10);
    snprintf(details,
             sizeof(details),
             "counter=%lu|temp=%d",
             (unsigned long)peer->last_counter,
             peer->last_temp_centi);
    demo_log_rx(peer->slot, "telemetry", details);
}

/* Payloads are small ASCII frames such as HELLO|1|node-1 or TELEM|2|17|4|2135|123456. */
static void demo_process_payload(const char *payload, const uint8_t remote_ip[4])
{
    char buffer[DEMO_MAX_LINE];
    char *saveptr = NULL;
    char *type;
    char *slot_text;
    uint8_t source_slot;
    demo_peer_t *peer;
    char ip_detail[32];

    if (payload == NULL) {
        return;
    }

    snprintf(buffer, sizeof(buffer), "%s", payload);
    type = strtok_r(buffer, "|", &saveptr);
    slot_text = strtok_r(NULL, "|", &saveptr);
    if (type == NULL || slot_text == NULL) {
        demo_log_simple("bad_payload");
        return;
    }

    source_slot = (uint8_t)strtoul(slot_text, NULL, 10);
    peer = demo_peer_by_slot(source_slot);
    if (peer == NULL) {
        demo_log_simple("unknown_peer_slot");
        return;
    }

    demo_mark_peer_seen(peer);
    peer->rx_count++;
    snprintf(ip_detail,
             sizeof(ip_detail),
             "ip=%u.%u.%u.%u",
             (unsigned)remote_ip[0],
             (unsigned)remote_ip[1],
             (unsigned)remote_ip[2],
             (unsigned)remote_ip[3]);

    if (strcmp(type, "HELLO") == 0) {
        demo_log_rx(peer->slot, "hello", ip_detail);
        return;
    }

    if (strcmp(type, "PING") == 0) {
        demo_log_rx(peer->slot, "ping", ip_detail);
        demo_handle_ping(peer);
        return;
    }

    if (strcmp(type, "PONG") == 0) {
        demo_log_rx(peer->slot, "pong", ip_detail);
        return;
    }

    if (strcmp(type, "TEXT") == 0) {
        char *seq_text = strtok_r(NULL, "|", &saveptr);
        char *message = saveptr;
        char details[128];

        (void)seq_text;
        if (message == NULL) {
            message = "";
        }
        snprintf(peer->last_text, sizeof(peer->last_text), "%s", message);
        snprintf(details, sizeof(details), "%s|text=%s", ip_detail, message);
        demo_log_rx(peer->slot, "text", details);
        return;
    }

    if (strcmp(type, "TELEM") == 0) {
        char *seq_text = strtok_r(NULL, "|", &saveptr);
        char *counter_text = strtok_r(NULL, "|", &saveptr);
        char *temp_text = strtok_r(NULL, "|", &saveptr);

        (void)seq_text;
        demo_handle_telemetry(peer, counter_text, temp_text);
        return;
    }

    demo_log_rx(peer->slot, "unknown", ip_detail);
}

static void demo_poll_transport(void)
{
    p2p_packet_t packet;
    p2p_err_t err;

    for (;;) {
        memset(&packet, 0, sizeof(packet));
        err = p2p_transport_recv(&s_demo.transport, &packet);
        if (err != P2P_OK) {
            demo_log_peer("error", 0U, "recv", "transport_recv_failed");
            return;
        }
        if (packet.len == 0U) {
            return;
        }

        if (packet.len >= (DEMO_MAX_LINE - 1U)) {
            demo_log_simple("packet_too_large");
            continue;
        }

        packet.data[packet.len] = '\0';
        demo_process_payload((const char *)packet.data, packet.remote_ip);
    }
}

static void demo_network_task(void *arg)
{
    (void)arg;

    for (;;) {
        uint32_t now_ms = demo_now_ms();
        uint8_t slot;
        p2p_err_t tick_err = p2p_transport_tick(&s_demo.transport);

        if (tick_err != P2P_OK && tick_err != P2P_ERR_TIMEOUT) {
            demo_log_simple("transport_tick_error");
        }

        demo_poll_transport();

        for (slot = 1U; slot <= 3U; ++slot) {
            demo_peer_t *peer = demo_peer_by_slot(slot);
            if (peer != NULL &&
                peer->online &&
                peer->last_seen_ms != 0U &&
                (now_ms - peer->last_seen_ms) > ((uint32_t)CONFIG_MICRONET_DEMO_TELEMETRY_PERIOD_MS * 3U)) {
                peer->online = false;
                demo_log_peer("offline", peer->slot, "peer", "telemetry_timeout");
            }
        }

        if ((now_ms - s_demo.last_hello_ms) >= 10000U) {
            s_demo.last_hello_ms = now_ms;
            demo_send_hello();
        }

        if ((now_ms - s_demo.last_telemetry_ms) >= (uint32_t)CONFIG_MICRONET_DEMO_TELEMETRY_PERIOD_MS) {
            s_demo.last_telemetry_ms = now_ms;
            demo_send_telemetry();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void demo_console_task(void *arg)
{
    char line[DEMO_MAX_LINE];

    (void)arg;
    /* Keeping the console text-based makes it usable from idf.py monitor and raw serial tools. */
    demo_print_help();

    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }

        if (strcmp(line, "help") == 0) {
            demo_print_help();
            continue;
        }
        if (strcmp(line, "status") == 0 || strcmp(line, "peers") == 0) {
            demo_print_status();
            continue;
        }
        if (strcmp(line, "hello") == 0) {
            demo_send_hello();
            continue;
        }
        if (strcmp(line, "pingall") == 0) {
            uint8_t slot;
            for (slot = 1U; slot <= 3U; ++slot) {
                if (slot != s_demo.self_slot) {
                    demo_send_ping(slot);
                }
            }
            continue;
        }
        if (strcmp(line, "counter") == 0) {
            s_demo.local_counter++;
            demo_refresh_local_store();
            demo_log_simple("counter_incremented");
            continue;
        }
        if (strcmp(line, "snapshot") == 0) {
            demo_print_snapshot();
            continue;
        }
        if (strcmp(line, "whoami") == 0) {
            printf("node=%u name=%s\n", (unsigned)s_demo.self_slot, s_demo.self_name);
            continue;
        }
        if (strncmp(line, "ping ", 5) == 0) {
            uint8_t slot = (uint8_t)strtoul(line + 5, NULL, 10);
            demo_send_ping(slot);
            continue;
        }
        if (strncmp(line, "send ", 5) == 0) {
            char *message = strchr(line + 5, ' ');
            uint8_t slot = (uint8_t)strtoul(line + 5, NULL, 10);

            if (message != NULL) {
                while (*message == ' ') {
                    message++;
                }
                demo_send_text(slot, message);
            }
            continue;
        }
        if (strncmp(line, "all ", 4) == 0) {
            demo_send_text(0U, line + 4);
            continue;
        }
        if (strncmp(line, "temp ", 5) == 0) {
            double value = strtod(line + 5, NULL);
            s_demo.local_temp_centi = (int)(value * 100.0);
            demo_refresh_local_store();
            demo_log_simple("temperature_updated");
            continue;
        }

        puts("Unknown command. Type 'help'.");
    }
}

static void demo_data_init(void)
{
    p2p_data_config_t cfg;
    static const char empty_text[] = "";

    memset(&cfg, 0, sizeof(cfg));
    cfg.max_vars = 8U;
    cfg.max_subs = 4U;
    cfg.notify_min_interval_ms = 250U;

    (void)p2p_data_init(&s_demo.data, &cfg);
    (void)p2p_data_publish(&s_demo.data,
                           DEMO_LOCAL_STORE_KEY_TEMP,
                           P2P_DATA_VAR,
                           &s_demo.local_temp_centi,
                           sizeof(s_demo.local_temp_centi));
    (void)p2p_data_publish(&s_demo.data,
                           DEMO_LOCAL_STORE_KEY_COUNT,
                           P2P_DATA_VAR,
                           &s_demo.local_counter,
                           sizeof(s_demo.local_counter));
    (void)p2p_data_publish(&s_demo.data,
                           DEMO_LOCAL_STORE_KEY_LAST_TEXT,
                           P2P_DATA_VAR,
                           empty_text,
                           sizeof(empty_text));
}

/* One shared UDP port on all nodes keeps the test setup simple. */
static bool demo_transport_init(void)
{
    p2p_transport_config_t cfg;
    p2p_err_t err;

    memset(&cfg, 0, sizeof(cfg));
    cfg.local_port = (uint16_t)CONFIG_MICRONET_DEMO_UDP_PORT;
    cfg.retry_count = (uint8_t)CONFIG_MICRONET_DEMO_RETRY_COUNT;
    cfg.retry_delay_ms = 300U;
    cfg.rx_buf_size = sizeof(p2p_packet_t) * 8U;
    cfg.tx_buf_size = sizeof(p2p_transport_retry_entry_t) * 8U;
    cfg.heartbeat_ms = 0U;
    cfg.timeout_ms = 0U;

    err = p2p_transport_init(&s_demo.transport, &cfg);
    if (err != P2P_OK) {
        ESP_LOGE(TAG, "transport init failed: %d", (int)err);
        return false;
    }
    return true;
}

static void demo_init_peers(void)
{
    uint8_t slot;

    memset(&s_demo, 0, sizeof(s_demo));
    s_demo.self_slot = (uint8_t)CONFIG_MICRONET_DEMO_NODE_SLOT;
    snprintf(s_demo.self_name, sizeof(s_demo.self_name), "node-%u", (unsigned)s_demo.self_slot);
    s_demo.local_temp_centi = 2150;

    for (slot = 1U; slot <= 3U; ++slot) {
        demo_peer_t *peer = &s_demo.peers[slot - 1U];
        const char *ip_text = demo_configured_ip(slot);

        /* Each firmware image knows the full 3-node topology up front. */
        memset(peer, 0, sizeof(*peer));
        peer->slot = slot;
        snprintf(peer->name, sizeof(peer->name), "node-%u", (unsigned)slot);
        snprintf(peer->ip_text, sizeof(peer->ip_text), "%s", ip_text);
        peer->configured = demo_parse_ipv4(ip_text, peer->ip);
    }
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
        xEventGroupSetBits(s_wifi_events, DEMO_WIFI_CONNECTED_BIT);
    }
}

static void demo_wifi_init(void)
{
    /* The demo prefers easy bring-up over strict security policy checks. */
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

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &demo_wifi_event_handler,
                                               NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &demo_wifi_event_handler,
                                               NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    bits = xEventGroupWaitBits(s_wifi_events,
                               DEMO_WIFI_CONNECTED_BIT | DEMO_WIFI_FAIL_BIT,
                               pdFALSE,
                               pdFALSE,
                               pdMS_TO_TICKS(30000));
    if ((bits & DEMO_WIFI_CONNECTED_BIT) == 0U) {
        ESP_LOGE(TAG, "Wi-Fi connect failed");
    } else {
        ESP_LOGI(TAG, "Wi-Fi connected");
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    demo_init_peers();
    demo_data_init();
    demo_wifi_init();
    if (!demo_transport_init()) {
        demo_log_simple("transport_init_failed");
        return;
    }
    demo_send_hello();

    demo_log_simple("boot");
    xTaskCreate(demo_network_task, "demo_network", 4096, NULL, 5, NULL);
    xTaskCreate(demo_console_task, "demo_console", 4096, NULL, 4, NULL);
}

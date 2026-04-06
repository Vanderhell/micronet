#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "transport/p2p_transport.h"

#define STUN_WIFI_CONNECTED_BIT BIT0
#define STUN_WIFI_FAIL_BIT BIT1

typedef struct {
    p2p_transport_t transport;
    bool transport_ready;
    uint8_t mapped_ip[4];
    uint16_t mapped_port;
    p2p_err_t last_err;
} stun_probe_app_t;

static const char *TAG = "stun_probe";
static EventGroupHandle_t s_wifi_events;
static int s_wifi_retries;
static stun_probe_app_t s_app;

static void stun_print_status(void)
{
    printf("STUN_PROBE|event=status|server=%s:%d|local_port=%d|transport_ready=%u\n",
           CONFIG_MICRONET_STUN_HOST,
           CONFIG_MICRONET_STUN_PORT,
           CONFIG_MICRONET_STUN_LOCAL_PORT,
           s_app.transport_ready ? 1U : 0U);
}

static bool stun_transport_init(void)
{
    p2p_transport_config_t cfg;
    p2p_err_t err;

    memset(&cfg, 0, sizeof(cfg));
    cfg.stun_host = CONFIG_MICRONET_STUN_HOST;
    cfg.stun_port = (uint16_t)CONFIG_MICRONET_STUN_PORT;
    cfg.local_port = (uint16_t)CONFIG_MICRONET_STUN_LOCAL_PORT;
    cfg.retry_count = 1U;
    cfg.retry_delay_ms = 200U;
    cfg.rx_buf_size = sizeof(p2p_packet_t) * 4U;
    cfg.tx_buf_size = sizeof(p2p_transport_retry_entry_t) * 4U;

    err = p2p_transport_init(&s_app.transport, &cfg);
    s_app.last_err = err;
    s_app.transport_ready = (err == P2P_OK);
    if (!s_app.transport_ready) {
        printf("STUN_PROBE|event=transport_init_fail|err=%d\n", (int)err);
        return false;
    }
    return true;
}

/* This is the actual one-board check: send STUN binding request and print mapped address. */
static void stun_run_probe(void)
{
    p2p_err_t err;
    uint8_t ip[4];
    uint16_t port = 0U;

    if (!s_app.transport_ready) {
        puts("STUN_PROBE|event=skip|reason=transport_not_ready");
        return;
    }

    err = p2p_transport_stun_resolve(&s_app.transport);
    s_app.last_err = err;
    if (err != P2P_OK) {
        printf("STUN_PROBE|event=fail|server=%s:%d|err=%d\n",
               CONFIG_MICRONET_STUN_HOST,
               CONFIG_MICRONET_STUN_PORT,
               (int)err);
        return;
    }

    err = p2p_transport_get_external_addr(&s_app.transport, ip, &port);
    s_app.last_err = err;
    if (err != P2P_OK) {
        printf("STUN_PROBE|event=addr_fail|err=%d\n", (int)err);
        return;
    }

    memcpy(s_app.mapped_ip, ip, sizeof(s_app.mapped_ip));
    s_app.mapped_port = port;
    printf("STUN_PROBE|event=ok|server=%s:%d|mapped=%u.%u.%u.%u:%u\n",
           CONFIG_MICRONET_STUN_HOST,
           CONFIG_MICRONET_STUN_PORT,
           (unsigned)ip[0],
           (unsigned)ip[1],
           (unsigned)ip[2],
           (unsigned)ip[3],
           (unsigned)port);
}

static void stun_print_help(void)
{
    puts("Commands:");
    puts("  help");
    puts("  status");
    puts("  probe");
}

static void stun_console_task(void *arg)
{
    char line[64];

    (void)arg;
    stun_print_help();
    stun_print_status();

    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "help") == 0) {
            stun_print_help();
            continue;
        }
        if (strcmp(line, "status") == 0) {
            stun_print_status();
            continue;
        }
        if (strcmp(line, "probe") == 0) {
            stun_run_probe();
            continue;
        }

        puts("Unknown command. Type 'help'.");
    }
}

static void stun_wifi_event_handler(void *arg,
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
            xEventGroupSetBits(s_wifi_events, STUN_WIFI_FAIL_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_retries = 0;
        xEventGroupSetBits(s_wifi_events, STUN_WIFI_CONNECTED_BIT);
    }
}

static bool stun_wifi_init(void)
{
    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    EventBits_t bits;

    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", CONFIG_MICRONET_STUN_WIFI_SSID);
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", CONFIG_MICRONET_STUN_WIFI_PASSWORD);

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
                                               &stun_wifi_event_handler,
                                               NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &stun_wifi_event_handler,
                                               NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    bits = xEventGroupWaitBits(s_wifi_events,
                               STUN_WIFI_CONNECTED_BIT | STUN_WIFI_FAIL_BIT,
                               pdFALSE,
                               pdFALSE,
                               pdMS_TO_TICKS(30000));
    if ((bits & STUN_WIFI_CONNECTED_BIT) == 0U) {
        ESP_LOGE(TAG, "Wi-Fi connect failed");
        return false;
    }

    ESP_LOGI(TAG, "Wi-Fi connected");
    return true;
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    memset(&s_app, 0, sizeof(s_app));
    if (!stun_wifi_init()) {
        puts("STUN_PROBE|event=wifi_fail");
        return;
    }
    if (!stun_transport_init()) {
        return;
    }

    stun_run_probe();
    xTaskCreate(stun_console_task, "stun_console", 4096, NULL, 4, NULL);
}

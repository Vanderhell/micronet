#define _POSIX_C_SOURCE 200809L

#include "micronet.h"

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHAT_MAX_GROUP_NAME 64U
#define CHAT_MAX_PEERS 32U

typedef struct {
    uint8_t ip[4];
    uint16_t port;
} chat_peer_t;

typedef struct {
    char name[64];
    uint16_t port;
    char group_name[CHAT_MAX_GROUP_NAME];
    char send_text[512];
    bool send_once;
    bool discover;
    chat_peer_t peers[CHAT_MAX_PEERS];
    uint8_t peer_count;
} chat_options_t;

static uint64_t chat_splitmix64(uint64_t *state)
{
    uint64_t z;

    *state += 0x9E3779B97F4A7C15ULL;
    z = *state;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

static uint64_t chat_fnv1a64(const char *text)
{
    uint64_t hash = 1469598103934665603ULL;

    while (text != NULL && *text != '\0') {
        hash ^= (unsigned char)*text++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t chat_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }

    return (uint64_t)ts.tv_sec * 1000ULL + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static void chat_derive_group(const char *name, mnet_group_seed_t *seed)
{
    uint64_t state;
    uint64_t first;
    uint64_t second;
    size_t i;

    if (seed == NULL) {
        return;
    }

    state = chat_fnv1a64(name) ^ 0xA5A5A5A5A5A5A5A5ULL;
    first = chat_splitmix64(&state);
    second = chat_splitmix64(&state);
    for (i = 0U; i < sizeof(seed->group_hash); ++i) {
        seed->group_hash[i] = (uint8_t)((i < 8U) ? (first >> (i * 8U)) : (second >> ((i - 8U) * 8U)));
    }
    first ^= 0xC3C3C3C3C3C3C3C3ULL;
    second ^= 0x3C3C3C3C3C3C3C3CULL;
    for (i = 0U; i < sizeof(seed->group_key); ++i) {
        seed->group_key[i] = (uint8_t)((i < 8U) ? (first >> (i * 8U)) : (second >> ((i - 8U) * 8U)));
    }
}

static void chat_bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t out_len)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (out == NULL || out_len == 0U) {
        return;
    }

    if (out_len < (len * 2U + 1U)) {
        out[0] = '\0';
        return;
    }

    for (i = 0U; i < len; ++i) {
        out[i * 2U] = hex[(bytes[i] >> 4U) & 0x0FU];
        out[i * 2U + 1U] = hex[bytes[i] & 0x0FU];
    }
    out[len * 2U] = '\0';
}

static bool chat_parse_ip_port(const char *text, uint8_t out_ip[4], uint16_t *out_port)
{
    unsigned int a, b, c, d, port;
    char tail;

    if (text == NULL || out_ip == NULL || out_port == NULL) {
        return false;
    }

    if (sscanf(text, "%u.%u.%u.%u:%u%c", &a, &b, &c, &d, &port, &tail) != 5) {
        return false;
    }
    if (a > 255U || b > 255U || c > 255U || d > 255U || port == 0U || port > 65535U) {
        return false;
    }

    out_ip[0] = (uint8_t)a;
    out_ip[1] = (uint8_t)b;
    out_ip[2] = (uint8_t)c;
    out_ip[3] = (uint8_t)d;
    *out_port = (uint16_t)port;
    return true;
}

static void chat_on_message(const uint8_t src[32], const uint8_t *payload, size_t len)
{
    char hex[65];
    char text[513];

    chat_bytes_to_hex(src, 32U, hex, sizeof(hex));
    if (len >= sizeof(text)) {
        len = sizeof(text) - 1U;
    }
    if (payload != NULL && len > 0U) {
        memcpy(text, payload, len);
    }
    text[len] = '\0';
    printf("rx src=%s text=%s\n", hex, text);
}

static void chat_print_usage(const char *argv0)
{
    printf("Usage: %s [--name NAME] [--port PORT] [--group NAME] [--peer IP:PORT] [--discover] [--send TEXT]\n",
           argv0);
}

static bool chat_parse_args(int argc, char **argv, chat_options_t *opt)
{
    int i;

    if (opt == NULL) {
        return false;
    }

    memset(opt, 0, sizeof(*opt));
    snprintf(opt->name, sizeof(opt->name), "%s", "micronet-chat");
    snprintf(opt->group_name, sizeof(opt->group_name), "%s", "default");
    opt->port = 33333U;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--name") == 0 && (i + 1) < argc) {
            snprintf(opt->name, sizeof(opt->name), "%s", argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--port") == 0 && (i + 1) < argc) {
            unsigned long port = strtoul(argv[++i], NULL, 10);
            if (port == 0UL || port > 65535UL) {
                return false;
            }
            opt->port = (uint16_t)port;
            continue;
        }
        if (strcmp(argv[i], "--group") == 0 && (i + 1) < argc) {
            snprintf(opt->group_name, sizeof(opt->group_name), "%s", argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--peer") == 0 && (i + 1) < argc) {
            chat_peer_t *peer;

            if (opt->peer_count >= CHAT_MAX_PEERS) {
                return false;
            }
            peer = &opt->peers[opt->peer_count];
            if (!chat_parse_ip_port(argv[++i], peer->ip, &peer->port)) {
                return false;
            }
            opt->peer_count++;
            continue;
        }
        if (strcmp(argv[i], "--discover") == 0) {
            opt->discover = true;
            continue;
        }
        if (strcmp(argv[i], "--send") == 0 && (i + 1) < argc) {
            snprintf(opt->send_text, sizeof(opt->send_text), "%s", argv[++i]);
            opt->send_once = true;
            continue;
        }
        return false;
    }

    return true;
}

static void chat_print_peers(void)
{
    mnet_peer_info_t peers[MNET_MAX_NODES];
    uint8_t count = 0U;
    uint8_t i;
    char id_hex[65];
    char group_hex[33];

    if (mnet_peer_list(peers, (uint8_t)MNET_MAX_NODES, &count) != MNET_OK) {
        puts("peer list unavailable");
        return;
    }

    for (i = 0U; i < count; ++i) {
        uint8_t g;

        chat_bytes_to_hex(peers[i].node_id, 32U, id_hex, sizeof(id_hex));
        printf("peer id=%s ip=%u.%u.%u.%u port=%u online=%s auth=%s groups=",
               id_hex,
               (unsigned)peers[i].ip[0],
               (unsigned)peers[i].ip[1],
               (unsigned)peers[i].ip[2],
               (unsigned)peers[i].ip[3],
               (unsigned)peers[i].port,
               peers[i].is_online ? "yes" : "no",
               peers[i].is_authorized ? "yes" : "no");
        if (peers[i].group_count == 0U) {
            puts("-");
            continue;
        }
        for (g = 0U; g < peers[i].group_count; ++g) {
            chat_bytes_to_hex(peers[i].groups[g], 16U, group_hex, sizeof(group_hex));
            printf("%s%s", (g == 0U) ? "" : ",", group_hex);
        }
        putchar('\n');
    }
}

static mnet_err_t chat_send_group_message(const char *text, const uint8_t group_hash[16])
{
    uint8_t sent_count = 0U;
    size_t len;

    if (text == NULL || group_hash == NULL) {
        return MNET_ERR_INVALID_ARG;
    }

    len = strlen(text);
    if (len == 0U) {
        return MNET_ERR_INVALID_ARG;
    }

    if (mnet_send_group_custom(group_hash, 0x80U, (const uint8_t *)text, len, &sent_count) != MNET_OK) {
        return MNET_ERR_INTERNAL;
    }

    printf("sent=%u\n", (unsigned)sent_count);
    return MNET_OK;
}

int main(int argc, char **argv)
{
    chat_options_t opt;
    mnet_config_t cfg;
    mnet_group_seed_t group_seed;
    bool running = true;
    uint64_t next_discovery_ms = 0ULL;
    uint64_t now_ms;

    if (!chat_parse_args(argc, argv, &opt)) {
        chat_print_usage(argv[0]);
        return 2;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.node_name = opt.name;
    cfg.network_mode = MNET_MODE_LAN_ONLY;
    cfg.stun_enabled = false;
    cfg.local_port = opt.port;
    cfg.max_nodes = 16U;
    cfg.max_vars = 8U;
    cfg.max_pending = 8U;
    chat_derive_group(opt.group_name, &group_seed);
    cfg.groups[0] = group_seed;
    cfg.group_count = 1U;

    if (mnet_init(&cfg) != MNET_OK) {
        puts("mnet_init failed");
        return 1;
    }

    for (uint8_t i = 0U; i < opt.peer_count; ++i) {
        if (mnet_peer_add_ip(NULL, opt.peers[i].ip, opt.peers[i].port) != MNET_OK) {
            printf("peer add failed for %u.%u.%u.%u:%u\n",
                   (unsigned)opt.peers[i].ip[0],
                   (unsigned)opt.peers[i].ip[1],
                   (unsigned)opt.peers[i].ip[2],
                   (unsigned)opt.peers[i].ip[3],
                   (unsigned)opt.peers[i].port);
        }
    }

    if (mnet_register_handler(0x80U, chat_on_message) != MNET_OK) {
        puts("handler registration failed");
        mnet_deinit();
        return 1;
    }

    {
        uint8_t node_id[32];
        char node_hex[65];
        char group_hex[33];

        if (mnet_get_node_id(node_id) != MNET_OK) {
            puts("node_id unavailable");
            mnet_deinit();
            return 1;
        }
        chat_bytes_to_hex(node_id, 32U, node_hex, sizeof(node_hex));
        chat_bytes_to_hex(group_seed.group_hash, 16U, group_hex, sizeof(group_hex));
        printf("name=%s port=%u node=%s group=%s discover=%s\n",
               opt.name,
               (unsigned)opt.port,
               node_hex,
               group_hex,
               opt.discover ? "yes" : "no");
    }

    if (opt.send_once) {
        mnet_err_t send_err = chat_send_group_message(opt.send_text, group_seed.group_hash);
        if (send_err != MNET_OK) {
            printf("send failed err=%d\n", (int)send_err);
        }
        for (int i = 0; i < 5; ++i) {
            (void)mnet_tick();
        }
        mnet_deinit();
        return send_err == MNET_OK ? 0 : 1;
    }

    while (running) {
        struct pollfd pfd;
        int rc;

        now_ms = chat_now_ms();
        if (opt.discover && (next_discovery_ms == 0ULL || now_ms >= next_discovery_ms)) {
            if (mnet_discover_lan() == MNET_OK) {
                next_discovery_ms = (uint64_t)now_ms + 2000ULL;
            } else {
                next_discovery_ms = (uint64_t)now_ms + 5000ULL;
            }
        }

        if (mnet_tick() != MNET_OK) {
            puts("tick error");
        }

        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        pfd.revents = 0;
        rc = poll(&pfd, 1U, 200);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (rc > 0 && (pfd.revents & POLLIN) != 0) {
            char line[512];

            if (fgets(line, sizeof(line), stdin) == NULL) {
                break;
            }
            line[strcspn(line, "\r\n")] = '\0';
            if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
                running = false;
            } else if (strcmp(line, "peers") == 0) {
                chat_print_peers();
            } else if (strcmp(line, "discover") == 0) {
                if (mnet_discover_lan() != MNET_OK) {
                    puts("discover failed");
                }
            } else if (line[0] != '\0') {
                if (chat_send_group_message(line, group_seed.group_hash) != MNET_OK) {
                    puts("send failed");
                }
            }
        }
    }

    mnet_deinit();
    return 0;
}

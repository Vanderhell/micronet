#if !defined(_WIN32)
#if defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE < 200112L)
#undef _POSIX_C_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#if defined(_XOPEN_SOURCE) && (_XOPEN_SOURCE < 600)
#undef _XOPEN_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#endif

#include "p2p_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_compat_t;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef socklen_t socklen_compat_t;
#endif

#define P2P_STUN_BINDING_REQUEST 0x0001U
#define P2P_STUN_BINDING_SUCCESS 0x0101U
#define P2P_STUN_MAGIC_COOKIE 0x2112A442UL
#define P2P_STUN_ATTR_MAPPED_ADDRESS 0x0001U
#define P2P_STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020U

static uint16_t p2p_stun_read_u16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

static uint32_t p2p_stun_read_u32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static void p2p_stun_write_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)((value >> 8) & 0xFFU);
    dst[1] = (uint8_t)(value & 0xFFU);
}

static void p2p_stun_write_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)((value >> 24) & 0xFFU);
    dst[1] = (uint8_t)((value >> 16) & 0xFFU);
    dst[2] = (uint8_t)((value >> 8) & 0xFFU);
    dst[3] = (uint8_t)(value & 0xFFU);
}

static int p2p_stun_parse_address(const uint8_t *attr,
                                  size_t attr_len,
                                  uint8_t out_ip[4],
                                  uint16_t *out_port,
                                  int is_xor)
{
    uint32_t cookie;
    uint16_t port;

    if (attr == NULL || out_ip == NULL || out_port == NULL || attr_len < 8U || attr[1] != 0x01U) {
        return 0;
    }

    port = p2p_stun_read_u16(&attr[2]);
    memcpy(out_ip, &attr[4], 4U);
    if (is_xor) {
        cookie = P2P_STUN_MAGIC_COOKIE;
        port ^= (uint16_t)(cookie >> 16);
        out_ip[0] ^= (uint8_t)((cookie >> 24) & 0xFFU);
        out_ip[1] ^= (uint8_t)((cookie >> 16) & 0xFFU);
        out_ip[2] ^= (uint8_t)((cookie >> 8) & 0xFFU);
        out_ip[3] ^= (uint8_t)(cookie & 0xFFU);
    }

    *out_port = port;
    return 1;
}

static int p2p_stun_parse_response(const uint8_t *resp,
                                   size_t resp_len,
                                   const uint8_t txid[12],
                                   uint8_t out_ip[4],
                                   uint16_t *out_port)
{
    size_t offset = 20U;

    if (resp == NULL || txid == NULL || out_ip == NULL || out_port == NULL || resp_len < 20U) {
        return 0;
    }

    if (p2p_stun_read_u16(resp) != P2P_STUN_BINDING_SUCCESS ||
        p2p_stun_read_u32(resp + 4) != P2P_STUN_MAGIC_COOKIE ||
        memcmp(resp + 8, txid, 12U) != 0) {
        return 0;
    }

    while ((offset + 4U) <= resp_len) {
        uint16_t attr_type = p2p_stun_read_u16(resp + offset);
        uint16_t attr_len = p2p_stun_read_u16(resp + offset + 2U);
        const uint8_t *attr_data = resp + offset + 4U;
        size_t padded_len = (size_t)((attr_len + 3U) & ~3U);

        if ((offset + 4U + padded_len) > resp_len) {
            return 0;
        }

        if (attr_type == P2P_STUN_ATTR_XOR_MAPPED_ADDRESS &&
            p2p_stun_parse_address(attr_data, attr_len, out_ip, out_port, 1)) {
            return 1;
        }

        if (attr_type == P2P_STUN_ATTR_MAPPED_ADDRESS &&
            p2p_stun_parse_address(attr_data, attr_len, out_ip, out_port, 0)) {
            return 1;
        }

        offset += 4U + padded_len;
    }

    return 0;
}

p2p_err_t p2p_transport_stun_resolve(p2p_transport_t *ctx)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    char port_text[16];
    uint8_t req[20];
    uint8_t resp[512];
    uint8_t txid[12];
    int send_len;
    int recv_len;
    int selected;

#if defined(_WIN32)
    fd_set readfds;
    TIMEVAL timeout;
#else
    fd_set readfds;
    struct timeval timeout;
#endif

    if (ctx == NULL || ctx->config.stun_host == NULL || ctx->config.stun_port == 0U) {
        return P2P_ERR_INVALID_ARG;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    snprintf(port_text, sizeof(port_text), "%u", (unsigned)ctx->config.stun_port);
    if (getaddrinfo(ctx->config.stun_host, port_text, &hints, &result) != 0 || result == NULL) {
        return P2P_ERR_STUN;
    }

    srand((unsigned int)time(NULL));
    for (selected = 0; selected < 12; ++selected) {
        txid[selected] = (uint8_t)(rand() & 0xFF);
    }

    memset(req, 0, sizeof(req));
    p2p_stun_write_u16(req, P2P_STUN_BINDING_REQUEST);
    p2p_stun_write_u16(req + 2, 0U);
    p2p_stun_write_u32(req + 4, P2P_STUN_MAGIC_COOKIE);
    memcpy(req + 8, txid, sizeof(txid));

    for (it = result; it != NULL; it = it->ai_next) {
        send_len = (int)sendto(ctx->sock_fd,
                               (const char *)req,
                               (int)sizeof(req),
                               0,
                               it->ai_addr,
                               (socklen_compat_t)it->ai_addrlen);
        if (send_len != (int)sizeof(req)) {
            continue;
        }

        FD_ZERO(&readfds);
        FD_SET((unsigned)ctx->sock_fd, &readfds);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        selected = select(ctx->sock_fd + 1, &readfds, NULL, NULL, &timeout);
        if (selected <= 0) {
            continue;
        }

        recv_len = (int)recv(ctx->sock_fd, (char *)resp, (int)sizeof(resp), 0);
        if (recv_len <= 0) {
            continue;
        }

        if (p2p_stun_parse_response(resp,
                                    (size_t)recv_len,
                                    txid,
                                    ctx->external_ip,
                                    &ctx->external_port)) {
            ctx->stun_resolved = true;
            freeaddrinfo(result);
            return P2P_OK;
        }
    }

    freeaddrinfo(result);
    return P2P_ERR_STUN;
}

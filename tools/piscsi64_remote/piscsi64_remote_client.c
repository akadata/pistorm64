// SPDX-License-Identifier: MIT

#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#ifndef htobe16
#include <endian.h>
#endif

#define PS64_REMOTE_DEFAULT_PORT 4964
#define PS64_REMOTE_MAGIC_HELLO 0x50533634u
#define PS64_REMOTE_MAGIC_IOREQ 0x50533640u
#define PS64_REMOTE_MAGIC_IORSP 0x50533641u
#define PS64_REMOTE_VERSION 1u

#define PS64_REMOTE_OP_READ  1u
#define PS64_REMOTE_OP_WRITE 2u
#define PS64_REMOTE_OP_SYNC  3u
#define PS64_REMOTE_OP_CLOSE 4u
#define PS64_REMOTE_OP_PING  5u

typedef struct __attribute__((packed)) ps64_hello_req {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t flags_be;
    uint16_t token_len_be;
    uint16_t export_len_be;
    uint32_t reserved_be;
    uint8_t client_nonce[16];
} ps64_hello_req_t;

typedef struct __attribute__((packed)) ps64_hello_rsp {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t status_be;
    uint64_t size_bytes_be;
    uint32_t block_size_be;
    uint8_t media_kind;
    uint8_t read_only;
    uint16_t reserved_be;
    uint8_t server_nonce[16];
} ps64_hello_rsp_t;

typedef struct __attribute__((packed)) ps64_io_req {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t op_be;
    uint64_t offset_be;
    uint32_t length_be;
    uint32_t reserved_be;
} ps64_io_req_t;

typedef struct __attribute__((packed)) ps64_io_rsp {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t status_be;
    uint32_t length_be;
    int32_t err_be;
} ps64_io_rsp_t;

typedef struct tls_psk_client_data {
    char identity[128];
    char token[128];
} tls_psk_client_data_t;

static unsigned int tls_psk_client_cb(SSL *ssl, const char *hint,
                                      char *identity, unsigned int max_identity_len,
                                      unsigned char *psk, unsigned int max_psk_len)
{
    (void)hint;
    const tls_psk_client_data_t *psk_data = (const tls_psk_client_data_t *)SSL_get_app_data(ssl);
    if (!psk_data || !psk_data->token[0] || !psk_data->identity[0]) {
        return 0;
    }
    size_t id_len = strlen(psk_data->identity);
    size_t key_len = strlen(psk_data->token);
    if (id_len == 0 || key_len == 0 || id_len + 1 > max_identity_len || key_len > max_psk_len) {
        return 0;
    }
    memcpy(identity, psk_data->identity, id_len + 1);
    memcpy(psk, psk_data->token, key_len);
    return (unsigned int)key_len;
}

static int tls_send_all(SSL *ssl, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        int chunk = (len > (size_t)INT_MAX) ? INT_MAX : (int)len;
        int n = SSL_write(ssl, p, chunk);
        if (n <= 0) {
            int e = SSL_get_error(ssl, n);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                continue;
            }
            errno = EIO;
            return -1;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int tls_recv_all(SSL *ssl, void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    while (len > 0) {
        int chunk = (len > (size_t)INT_MAX) ? INT_MAX : (int)len;
        int n = SSL_read(ssl, p, chunk);
        if (n <= 0) {
            int e = SSL_get_error(ssl, n);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                continue;
            }
            errno = ENOTCONN;
            return -1;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int parse_host_port(const char *in, char *host, size_t host_len, uint16_t *port)
{
    char tmp[256];
    size_t in_len = strlen(in);
    if (in_len == 0 || in_len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, in, in_len + 1);
    char *colon = strrchr(tmp, ':');
    if (!colon) {
        size_t host_part_len = strlen(tmp);
        if (host_part_len == 0 || host_part_len >= host_len) {
            return -1;
        }
        memcpy(host, tmp, host_part_len + 1);
        *port = PS64_REMOTE_DEFAULT_PORT;
        return 0;
    }
    *colon = '\0';
    long p = strtol(colon + 1, NULL, 10);
    if (p <= 0 || p > 65535) {
        return -1;
    }
    size_t host_part_len = strlen(tmp);
    if (host_part_len == 0 || host_part_len >= host_len) {
        return -1;
    }
    memcpy(host, tmp, host_part_len + 1);
    *port = (uint16_t)p;
    return 0;
}

static void dump_hex(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if ((i % 16u) == 0) {
            printf("%08X: ", i);
        }
        printf("%02X ", buf[i]);
        if ((i % 16u) == 15u || i + 1 == len) {
            printf("\n");
        }
    }
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s HOST[:PORT] EXPORT TOKEN [OFFSET LENGTH]\n"
            "Example: %s 192.168.1.2:4964 workbench s3cr3t 0 512\n",
            argv0, argv0);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        usage(argv[0]);
        return 1;
    }

    char host[128];
    uint16_t port = PS64_REMOTE_DEFAULT_PORT;
    if (parse_host_port(argv[1], host, sizeof(host), &port) != 0) {
        fprintf(stderr, "Invalid host:port '%s'\n", argv[1]);
        return 1;
    }

    const char *export_name = argv[2];
    const char *token = argv[3];
    uint64_t read_offset = 0;
    uint32_t read_len = 0;
    if (argc >= 6) {
        read_offset = strtoull(argv[4], NULL, 0);
        read_len = (uint32_t)strtoul(argv[5], NULL, 0);
    }

    char port_s[16];
    snprintf(port_s, sizeof(port_s), "%u", (unsigned int)port);
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(host, port_s, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(gai));
        return 1;
    }

    int sock = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0) {
            continue;
        }
        if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0) {
        perror("connect");
        return 1;
    }

    SSL_CTX *tls_ctx = SSL_CTX_new(TLS_client_method());
    if (!tls_ctx) {
        fprintf(stderr, "TLS context init failed\n");
        close(sock);
        return 1;
    }
    if (SSL_CTX_set_min_proto_version(tls_ctx, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(tls_ctx, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_cipher_list(tls_ctx, "PSK-AES256-GCM-SHA384:PSK-AES128-GCM-SHA256") != 1) {
        fprintf(stderr, "TLS PSK setup failed\n");
        SSL_CTX_free(tls_ctx);
        close(sock);
        return 1;
    }
    SSL_CTX_set_psk_client_callback(tls_ctx, tls_psk_client_cb);

    SSL *ssl = SSL_new(tls_ctx);
    if (!ssl) {
        fprintf(stderr, "TLS session init failed\n");
        SSL_CTX_free(tls_ctx);
        close(sock);
        return 1;
    }
    tls_psk_client_data_t psk_data;
    memset(&psk_data, 0, sizeof(psk_data));
    snprintf(psk_data.identity, sizeof(psk_data.identity), "%s", export_name);
    snprintf(psk_data.token, sizeof(psk_data.token), "%s", token);
    SSL_set_app_data(ssl, &psk_data);
    SSL_set_fd(ssl, sock);
    if (SSL_connect(ssl) <= 0) {
        fprintf(stderr, "TLS handshake failed\n");
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        return 1;
    }
    {
        int bits = SSL_get_cipher_bits(ssl, NULL);
        const char *tls_ver = SSL_get_version(ssl);
        const char *tls_cipher = SSL_get_cipher_name(ssl);
        printf("TLS: version=%s cipher=%s bits=%d\n",
               tls_ver ? tls_ver : "unknown",
               tls_cipher ? tls_cipher : "unknown",
               bits);
    }

    ps64_hello_req_t hello;
    memset(&hello, 0, sizeof(hello));
    if (RAND_bytes(hello.client_nonce, sizeof(hello.client_nonce)) != 1) {
        fprintf(stderr, "RAND_bytes failed\n");
        close(sock);
        return 1;
    }
    hello.magic_be = htobe32(PS64_REMOTE_MAGIC_HELLO);
    hello.version_be = htobe16(PS64_REMOTE_VERSION);
    hello.flags_be = htobe16(0);
    hello.token_len_be = htobe16(0);
    hello.export_len_be = htobe16((uint16_t)strlen(export_name));

    if (tls_send_all(ssl, &hello, sizeof(hello)) != 0 ||
        tls_send_all(ssl, export_name, strlen(export_name)) != 0) {
        perror("send hello");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        return 1;
    }

    ps64_hello_rsp_t rsp;
    if (tls_recv_all(ssl, &rsp, sizeof(rsp)) != 0) {
        perror("recv hello");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        return 1;
    }
    if (be32toh(rsp.magic_be) != PS64_REMOTE_MAGIC_HELLO ||
        be16toh(rsp.version_be) != PS64_REMOTE_VERSION) {
        fprintf(stderr, "Invalid hello response\n");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        return 1;
    }
    uint16_t status = be16toh(rsp.status_be);
    if (status != 0) {
        fprintf(stderr, "Server rejected request (status=%u)\n", status);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        return 1;
    }

    uint64_t size_bytes = be64toh(rsp.size_bytes_be);
    uint32_t block_size = be32toh(rsp.block_size_be);
    printf("Connected: size=%" PRIu64 " block=%u kind=%u mode=%s\n",
           size_bytes,
           block_size,
           (unsigned int)rsp.media_kind,
           rsp.read_only ? "ro" : "rw");

    if (read_len > 0) {
        ps64_io_req_t req;
        memset(&req, 0, sizeof(req));
        req.magic_be = htobe32(PS64_REMOTE_MAGIC_IOREQ);
        req.version_be = htobe16(PS64_REMOTE_VERSION);
        req.op_be = htobe16(PS64_REMOTE_OP_READ);
        req.offset_be = htobe64(read_offset);
        req.length_be = htobe32(read_len);

        if (tls_send_all(ssl, &req, sizeof(req)) != 0) {
            perror("send read req");
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(tls_ctx);
            close(sock);
            return 1;
        }

        ps64_io_rsp_t r;
        if (tls_recv_all(ssl, &r, sizeof(r)) != 0) {
            perror("recv read rsp");
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(tls_ctx);
            close(sock);
            return 1;
        }
        uint16_t rstatus = be16toh(r.status_be);
        uint32_t rlen = be32toh(r.length_be);
        uint32_t rerr = be32toh((uint32_t)r.err_be);
        if (rstatus != 0) {
            fprintf(stderr, "READ failed status=%u err=%u\n", rstatus, rerr);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(tls_ctx);
            close(sock);
            return 1;
        }

        uint8_t *buf = malloc(rlen);
        if (!buf) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(tls_ctx);
            close(sock);
            return 1;
        }
        if (tls_recv_all(ssl, buf, rlen) != 0) {
            perror("recv read payload");
            free(buf);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(tls_ctx);
            close(sock);
            return 1;
        }
        dump_hex(buf, rlen);
        free(buf);
    }

    ps64_io_req_t close_req;
    memset(&close_req, 0, sizeof(close_req));
    close_req.magic_be = htobe32(PS64_REMOTE_MAGIC_IOREQ);
    close_req.version_be = htobe16(PS64_REMOTE_VERSION);
    close_req.op_be = htobe16(PS64_REMOTE_OP_CLOSE);
    (void)tls_send_all(ssl, &close_req, sizeof(close_req));

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(tls_ctx);
    close(sock);
    return 0;
}

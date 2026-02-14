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
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

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

static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            errno = EPIPE;
            return -1;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            errno = ENOTCONN;
            return -1;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int derive_crypto(const char *token,
                         const uint8_t client_nonce[16],
                         const uint8_t server_nonce[16],
                         uint8_t key_out[32], uint8_t iv_out[16])
{
    EVP_MD_CTX *ctx = NULL;
    unsigned int digest_len = 0;
    uint8_t digest[32];

    if (!token || !token[0]) {
        return -1;
    }

    ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return -1;
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, token, strlen(token)) != 1 ||
        EVP_DigestUpdate(ctx, client_nonce, 16) != 1 ||
        EVP_DigestUpdate(ctx, server_nonce, 16) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1 ||
        digest_len != sizeof(digest)) {
        EVP_MD_CTX_free(ctx);
        return -1;
    }
    EVP_MD_CTX_free(ctx);
    memcpy(key_out, digest, 32);

    ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return -1;
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, server_nonce, 16) != 1 ||
        EVP_DigestUpdate(ctx, client_nonce, 16) != 1 ||
        EVP_DigestUpdate(ctx, token, strlen(token)) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1 ||
        digest_len != sizeof(digest)) {
        EVP_MD_CTX_free(ctx);
        return -1;
    }
    EVP_MD_CTX_free(ctx);
    memcpy(iv_out, digest, 16);
    return 0;
}

static int crypt_ctr(const uint8_t key[32], const uint8_t iv_base[16],
                     uint64_t counter, const uint8_t *in, uint8_t *out, uint32_t len)
{
    EVP_CIPHER_CTX *ctx = NULL;
    uint8_t iv[16];
    int outl = 0;
    int finl = 0;
    uint64_t ctr_be = htobe64(counter);

    memcpy(iv, iv_base, sizeof(iv));
    memcpy(iv + 8, &ctr_be, sizeof(ctr_be));

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return -1;
    }
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), NULL, key, iv) != 1 ||
        EVP_EncryptUpdate(ctx, out, &outl, in, (int)len) != 1 ||
        EVP_EncryptFinal_ex(ctx, out + outl, &finl) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    EVP_CIPHER_CTX_free(ctx);
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
    uint8_t key[32];
    uint8_t iv[16];
    uint64_t rx_ctr = 1;
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
    hello.token_len_be = htobe16((uint16_t)strlen(token));
    hello.export_len_be = htobe16((uint16_t)strlen(export_name));

    if (send_all(sock, &hello, sizeof(hello)) != 0 ||
        (token[0] && send_all(sock, token, strlen(token)) != 0) ||
        send_all(sock, export_name, strlen(export_name)) != 0) {
        perror("send hello");
        close(sock);
        return 1;
    }

    ps64_hello_rsp_t rsp;
    if (recv_all(sock, &rsp, sizeof(rsp)) != 0) {
        perror("recv hello");
        close(sock);
        return 1;
    }
    if (be32toh(rsp.magic_be) != PS64_REMOTE_MAGIC_HELLO ||
        be16toh(rsp.version_be) != PS64_REMOTE_VERSION) {
        fprintf(stderr, "Invalid hello response\n");
        close(sock);
        return 1;
    }
    uint16_t status = be16toh(rsp.status_be);
    if (status != 0) {
        fprintf(stderr, "Server rejected request (status=%u)\n", status);
        close(sock);
        return 1;
    }
    if (derive_crypto(token, hello.client_nonce, rsp.server_nonce, key, iv) != 0) {
        fprintf(stderr, "derive_crypto failed\n");
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

        if (send_all(sock, &req, sizeof(req)) != 0) {
            perror("send read req");
            close(sock);
            return 1;
        }

        ps64_io_rsp_t r;
        if (recv_all(sock, &r, sizeof(r)) != 0) {
            perror("recv read rsp");
            close(sock);
            return 1;
        }
        uint16_t rstatus = be16toh(r.status_be);
        uint32_t rlen = be32toh(r.length_be);
        uint32_t rerr = be32toh((uint32_t)r.err_be);
        if (rstatus != 0) {
            fprintf(stderr, "READ failed status=%u err=%u\n", rstatus, rerr);
            close(sock);
            return 1;
        }

        uint8_t *buf = malloc(rlen);
        if (!buf) {
            close(sock);
            return 1;
        }
        if (recv_all(sock, buf, rlen) != 0) {
            perror("recv read payload");
            free(buf);
            close(sock);
            return 1;
        }
        if (rlen && crypt_ctr(key, iv, rx_ctr, buf, buf, rlen) != 0) {
            fprintf(stderr, "decrypt payload failed\n");
            free(buf);
            close(sock);
            return 1;
        }
        if (rlen) {
            rx_ctr++;
        }
        dump_hex(buf, rlen);
        free(buf);
    }

    ps64_io_req_t close_req;
    memset(&close_req, 0, sizeof(close_req));
    close_req.magic_be = htobe32(PS64_REMOTE_MAGIC_IOREQ);
    close_req.version_be = htobe16(PS64_REMOTE_VERSION);
    close_req.op_be = htobe16(PS64_REMOTE_OP_CLOSE);
    (void)send_all(sock, &close_req, sizeof(close_req));

    close(sock);
    return 0;
}

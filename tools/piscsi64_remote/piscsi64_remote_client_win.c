// SPDX-License-Identifier: MIT
// Minimal Windows probe client for the PiSCSI64 remote backend protocol.

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#pragma comment(lib, "ws2_32.lib")

#define PS64_REMOTE_DEFAULT_PORT 4964
#define PS64_REMOTE_MAGIC_HELLO 0x50533634u
#define PS64_REMOTE_MAGIC_IOREQ 0x50533640u
#define PS64_REMOTE_MAGIC_IORSP 0x50533641u
#define PS64_REMOTE_VERSION 1u

#define PS64_REMOTE_OP_READ  1u
#define PS64_REMOTE_OP_CLOSE 4u

static uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static uint32_t swap32(uint32_t v)
{
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8) |
           ((v & 0xFF000000u) >> 24);
}
static uint64_t swap64(uint64_t v)
{
    return ((uint64_t)swap32((uint32_t)(v & 0xFFFFFFFFu)) << 32) |
           (uint64_t)swap32((uint32_t)(v >> 32));
}

#define htobe16(x) swap16(x)
#define htobe32(x) swap32(x)
#define htobe64(x) swap64(x)
#define be16toh(x) swap16(x)
#define be32toh(x) swap32(x)
#define be64toh(x) swap64(x)

#pragma pack(push, 1)
typedef struct ps64_hello_req {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t flags_be;
    uint16_t token_len_be;
    uint16_t export_len_be;
    uint32_t reserved_be;
    uint8_t client_nonce[16];
} ps64_hello_req_t;

typedef struct ps64_hello_rsp {
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

typedef struct ps64_io_req {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t op_be;
    uint64_t offset_be;
    uint32_t length_be;
    uint32_t reserved_be;
} ps64_io_req_t;

typedef struct ps64_io_rsp {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t status_be;
    uint32_t length_be;
    int32_t err_be;
} ps64_io_rsp_t;
#pragma pack(pop)

static int send_all(SOCKET s, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        int n = send(s, p, (int)len, 0);
        if (n <= 0) {
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_all(SOCKET s, void *buf, size_t len)
{
    char *p = (char *)buf;
    while (len > 0) {
        int n = recv(s, p, (int)len, 0);
        if (n <= 0) {
            return -1;
        }
        p += n;
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

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s HOST EXPORT TOKEN [OFFSET LENGTH]\n"
            "Example: %s 192.168.1.50 workbench token 0 512\n",
            argv0, argv0);
}

static void dump_hex(const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        if ((i % 16u) == 0) {
            printf("%08X: ", i);
        }
        printf("%02X ", buf[i]);
        if ((i % 16u) == 15u || i + 1u == len) {
            printf("\n");
        }
    }
}

int main(int argc, char **argv)
{
    WSADATA wsa;
    SOCKET sock = INVALID_SOCKET;
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    char port_s[16];

    if (argc < 4) {
        usage(argv[0]);
        return 1;
    }

    const char *host = argv[1];
    const char *export_name = argv[2];
    const char *token = argv[3];
    uint8_t key[32];
    uint8_t iv[16];
    uint64_t rx_ctr = 1;
    uint64_t read_offset = (argc >= 6) ? _strtoui64(argv[4], NULL, 0) : 0;
    uint32_t read_len = (argc >= 6) ? (uint32_t)strtoul(argv[5], NULL, 0) : 0;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_s, sizeof(port_s), "%u", (unsigned int)PS64_REMOTE_DEFAULT_PORT);
    if (getaddrinfo(host, port_s, &hints, &res) != 0) {
        fprintf(stderr, "getaddrinfo failed\n");
        WSACleanup();
        return 1;
    }

    struct addrinfo *ai = res;
    for (; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == INVALID_SOCKET) {
            continue;
        }
        if (connect(sock, ai->ai_addr, (int)ai->ai_addrlen) == 0) {
            break;
        }
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "connect failed\n");
        WSACleanup();
        return 1;
    }

    ps64_hello_req_t hello;
    memset(&hello, 0, sizeof(hello));
    if (RAND_bytes(hello.client_nonce, sizeof(hello.client_nonce)) != 1) {
        fprintf(stderr, "RAND_bytes failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    hello.magic_be = htobe32(PS64_REMOTE_MAGIC_HELLO);
    hello.version_be = htobe16(PS64_REMOTE_VERSION);
    hello.token_len_be = htobe16((uint16_t)strlen(token));
    hello.export_len_be = htobe16((uint16_t)strlen(export_name));

    if (send_all(sock, &hello, sizeof(hello)) != 0 ||
        (token[0] && send_all(sock, token, strlen(token)) != 0) ||
        send_all(sock, export_name, strlen(export_name)) != 0) {
        fprintf(stderr, "send hello failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    ps64_hello_rsp_t rsp;
    if (recv_all(sock, &rsp, sizeof(rsp)) != 0) {
        fprintf(stderr, "recv hello failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    if (be32toh(rsp.magic_be) != PS64_REMOTE_MAGIC_HELLO ||
        be16toh(rsp.version_be) != PS64_REMOTE_VERSION ||
        be16toh(rsp.status_be) != 0) {
        fprintf(stderr, "server rejected hello (status=%u)\n", (unsigned)be16toh(rsp.status_be));
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    if (derive_crypto(token, hello.client_nonce, rsp.server_nonce, key, iv) != 0) {
        fprintf(stderr, "derive_crypto failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Connected: size=%llu block=%u kind=%u mode=%s\n",
           (unsigned long long)be64toh(rsp.size_bytes_be),
           (unsigned int)be32toh(rsp.block_size_be),
           (unsigned int)rsp.media_kind,
           rsp.read_only ? "ro" : "rw");

    if (read_len > 0) {
        ps64_io_req_t rreq;
        memset(&rreq, 0, sizeof(rreq));
        rreq.magic_be = htobe32(PS64_REMOTE_MAGIC_IOREQ);
        rreq.version_be = htobe16(PS64_REMOTE_VERSION);
        rreq.op_be = htobe16(PS64_REMOTE_OP_READ);
        rreq.offset_be = htobe64(read_offset);
        rreq.length_be = htobe32(read_len);

        if (send_all(sock, &rreq, sizeof(rreq)) != 0) {
            fprintf(stderr, "send read request failed\n");
            closesocket(sock);
            WSACleanup();
            return 1;
        }

        ps64_io_rsp_t rrsp;
        if (recv_all(sock, &rrsp, sizeof(rrsp)) != 0) {
            fprintf(stderr, "recv read response failed\n");
            closesocket(sock);
            WSACleanup();
            return 1;
        }
        if (be16toh(rrsp.status_be) != 0) {
            fprintf(stderr, "read failed status=%u err=%u\n",
                    (unsigned)be16toh(rrsp.status_be),
                    (unsigned)be32toh((uint32_t)rrsp.err_be));
            closesocket(sock);
            WSACleanup();
            return 1;
        }

        uint32_t rlen = be32toh(rrsp.length_be);
        uint8_t *buf = (uint8_t *)malloc(rlen);
        if (!buf || recv_all(sock, buf, rlen) != 0) {
            fprintf(stderr, "recv read payload failed\n");
            free(buf);
            closesocket(sock);
            WSACleanup();
            return 1;
        }
        if (rlen && crypt_ctr(key, iv, rx_ctr, buf, buf, rlen) != 0) {
            fprintf(stderr, "decrypt payload failed\n");
            free(buf);
            closesocket(sock);
            WSACleanup();
            return 1;
        }
        if (rlen) {
            rx_ctr++;
        }
        dump_hex(buf, rlen);
        free(buf);
    }

    ps64_io_req_t creq;
    memset(&creq, 0, sizeof(creq));
    creq.magic_be = htobe32(PS64_REMOTE_MAGIC_IOREQ);
    creq.version_be = htobe16(PS64_REMOTE_VERSION);
    creq.op_be = htobe16(PS64_REMOTE_OP_CLOSE);
    (void)send_all(sock, &creq, sizeof(creq));

    closesocket(sock);
    WSACleanup();
    return 0;
}

#else
int main(void)
{
    return 1;
}
#endif

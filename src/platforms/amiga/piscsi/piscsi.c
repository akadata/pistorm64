// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <time.h>
#include <limits.h>
#include <sys/time.h>
#include <ctype.h>

#include <openssl/ssl.h>
#include <openssl/rand.h>

#include "m68k.h"
#include "config_file/config_file.h"
#include "gpio/ps_protocol.h"
#include "log.h"
#include "leds/osd_leds.h"
#include "piscsi-enums.h"
#include "piscsi.h"
#include "platforms/amiga/fsid.h"
#include "platforms/amiga/hunk-reloc.h"

/* Legacy PiSCSI backend: maintenance mode compatibility path. */

#define BE(val) be32toh(val)
#define BE16(val) be16toh(val)

extern struct emulator_config *cfg;

/*
 * PiSCSI debug is noisy enough to distort timing-sensitive workloads.
 * Keep it opt-in even when global log level is DEBUG:
 *   PISCSI_DEBUG=0   (default) no PiSCSI debug logs
 *   PISCSI_DEBUG=1   normal PiSCSI debug
 *   PISCSI_DEBUG=2   include trivial/verbose PiSCSI debug
 */
static int piscsi_debug_level_cached = -1;

static int piscsi_debug_level(void)
{
    const char *env;
    int level = 0;
    if (piscsi_debug_level_cached >= 0) {
        return piscsi_debug_level_cached;
    }
    env = getenv("PISCSI_DEBUG");
    if (!env || !*env) {
        piscsi_debug_level_cached = 0;
        return 0;
    }
    if (!strcasecmp(env, "true") || !strcasecmp(env, "yes") || !strcmp(env, "on")) {
        level = 1;
    } else if (!strcasecmp(env, "verbose") || !strcasecmp(env, "trace")) {
        level = 2;
    } else {
        level = atoi(env);
    }
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    piscsi_debug_level_cached = level;
    return level;
}

#define DEBUG(...) do { if (piscsi_debug_level() >= 1) LOG_DEBUG(__VA_ARGS__); } while (0)
#define DEBUG_TRIVIAL(...) do { if (piscsi_debug_level() >= 2) LOG_DEBUG(__VA_ARGS__); } while (0)

/* extern void stop_cpu_emulation(uint8_t disasm_cur); */
#define stop_cpu_emulation(...)

static const char *op_type_names[4] = {
    "BYTE",
    "WORD",
    "LONGWORD",
    "MEM",
};

extern unsigned int cpu_type;

#define PISCSI_MEMF_PUBLIC_HOST      0x00000001u
#define PISCSI_MEMF_24BITDMA_HOST    0x00000200u
#define PISCSI_24BIT_ADDR_MASK       0x00FFFFFFu
#define PISCSI_24BIT_MAXTRANSFER     0x0001FE00u

static int piscsi_force_24bit_dma(void)
{
    const char *env = getenv("PISCSI_FORCE_24BITDMA");
    if (!env || !*env) {
        return (cpu_type == M68K_CPU_TYPE_68000 || cpu_type == M68K_CPU_TYPE_68010);
    }
    if (!strcasecmp(env, "1") || !strcasecmp(env, "true") ||
        !strcasecmp(env, "yes") || !strcasecmp(env, "on")) {
        return 1;
    }
    if (!strcasecmp(env, "0") || !strcasecmp(env, "false") ||
        !strcasecmp(env, "no") || !strcasecmp(env, "off")) {
        return 0;
    }
    return atoi(env) != 0;
}

static int piscsi_force_24bit_dma_strict_mask(void)
{
    const char *env = getenv("PISCSI_FORCE_24BITDMA_STRICT_MASK");
    if (!env || !*env) {
        return 0;
    }
    if (!strcasecmp(env, "1") || !strcasecmp(env, "true") ||
        !strcasecmp(env, "yes") || !strcasecmp(env, "on")) {
        return 1;
    }
    if (!strcasecmp(env, "0") || !strcasecmp(env, "false") ||
        !strcasecmp(env, "no") || !strcasecmp(env, "off")) {
        return 0;
    }
    return atoi(env) != 0;
}

#define PISCSI_REMOTE_DEFAULT_PORT 4964
#define PISCSI_REMOTE_VERSION 1u
#define PISCSI_REMOTE_MAGIC_HELLO 0x50533634u /* "PS64" */
#define PISCSI_REMOTE_MAGIC_IOREQ 0x50533640u /* "PS6@" */
#define PISCSI_REMOTE_MAGIC_IORSP 0x50533641u /* "PS6A" */

#define PISCSI_REMOTE_FLAG_REQ_RW  0x0001u
#define PISCSI_REMOTE_FLAG_HINT_CD 0x0002u

#define PISCSI_REMOTE_OP_READ  1u
#define PISCSI_REMOTE_OP_WRITE 2u
#define PISCSI_REMOTE_OP_SYNC  3u
#define PISCSI_REMOTE_OP_CLOSE 4u
#define PISCSI_REMOTE_OP_PING  5u
#define PISCSI_PIO_FALLBACK_CHUNK 4096u

typedef struct __attribute__((packed)) piscsi_remote_hello_req {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t flags_be;
    uint16_t token_len_be;
    uint16_t export_len_be;
    uint32_t reserved_be;
    uint8_t client_nonce[16];
} piscsi_remote_hello_req_t;

typedef struct __attribute__((packed)) piscsi_remote_hello_rsp {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t status_be;
    uint64_t size_bytes_be;
    uint32_t block_size_be;
    uint8_t media_kind;
    uint8_t read_only;
    uint16_t reserved_be;
    uint8_t server_nonce[16];
} piscsi_remote_hello_rsp_t;

typedef struct __attribute__((packed)) piscsi_remote_io_req {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t op_be;
    uint64_t offset_be;
    uint32_t length_be;
    uint32_t reserved_be;
} piscsi_remote_io_req_t;

typedef struct __attribute__((packed)) piscsi_remote_io_rsp {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t status_be;
    uint32_t length_be;
    uint32_t reserved_be;
} piscsi_remote_io_rsp_t;

typedef struct {
    char identity[128];
    char token[128];
} piscsi_tls_psk_client_data_t;

static const char *piscsi_media_kind_name(uint8_t kind) {
    switch (kind) {
        case PISCSI_MEDIA_DISK: return "disk";
        case PISCSI_MEDIA_CDROM: return "cdrom";
        default: return "none";
    }
}

static const char *piscsi_remote_status_name(uint16_t status)
{
    switch (status) {
        case 0: return "ok";
        case 1: return "auth";
        case 2: return "export";
        case 3: return "open";
        case 4: return "protocol";
        default: return "unknown";
    }
}

static unsigned int piscsi_tls_psk_client_cb(SSL *ssl, const char *hint,
                                             char *identity, unsigned int max_identity_len,
                                             unsigned char *psk, unsigned int max_psk_len)
{
    (void)hint;
    if (!ssl || !identity || !psk) {
        return 0;
    }
    piscsi_tls_psk_client_data_t *data =
        (piscsi_tls_psk_client_data_t *)SSL_get_app_data(ssl);
    if (!data) {
        return 0;
    }
    snprintf(identity, max_identity_len, "%s", data->identity);
    size_t token_len = strlen(data->token);
    if (token_len == 0 || token_len > max_psk_len) {
        return 0;
    }
    memcpy(psk, data->token, token_len);
    return (unsigned int)token_len;
}

static int piscsi_tls_send_all(SSL *ssl, const void *buf, size_t len)
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
            errno = ENOTCONN;
            return -1;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int piscsi_tls_recv_all(SSL *ssl, void *buf, size_t len)
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

static int piscsi_parse_remote_endpoint(const char *spec,
                                        char *token, size_t token_len,
                                        char *host, size_t host_len,
                                        uint16_t *port_out,
                                        char *export_name, size_t export_len)
{
    if (!spec || !host || !port_out || !export_name) {
        return -1;
    }
    if (token && token_len > 0) {
        token[0] = '\0';
    }

    const char *raw = spec;
    if (strncasecmp(raw, "remote:", 7) == 0) {
        raw += 7;
    }
    if (!raw[0]) {
        return -1;
    }

    char tmp[PISCSI_MAX_SPEC];
    size_t raw_len = strlen(raw);
    if (raw_len == 0 || raw_len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, raw, raw_len + 1);

    char *slash = strchr(tmp, '/');
    char *hostpart = tmp;
    if (slash) {
        *slash = '\0';
        size_t export_part_len = strlen(slash + 1);
        if (export_len == 0 || export_part_len >= export_len) {
            return -1;
        }
        memcpy(export_name, slash + 1, export_part_len + 1);
    } else {
        if (export_len < sizeof("default")) {
            return -1;
        }
        memcpy(export_name, "default", sizeof("default"));
    }

    char *at = strchr(hostpart, '@');
    if (at) {
        *at = '\0';
        if (token && token_len > 0) {
            size_t token_part_len = strlen(hostpart);
            if (token_part_len >= token_len) {
                return -1;
            }
            memcpy(token, hostpart, token_part_len + 1);
        }
        hostpart = at + 1;
    }

    if (!hostpart[0]) {
        return -1;
    }

    uint16_t port = PISCSI_REMOTE_DEFAULT_PORT;
    char hostbuf[128];
    hostbuf[0] = '\0';

    if (hostpart[0] == '[') {
        char *rb = strchr(hostpart + 1, ']');
        if (!rb) {
            return -1;
        }
        *rb = '\0';
        size_t host_part_len = strlen(hostpart + 1);
        if (host_part_len == 0 || host_part_len >= sizeof(hostbuf)) {
            return -1;
        }
        memcpy(hostbuf, hostpart + 1, host_part_len + 1);
        if (rb[1] == ':') {
            char *endp = NULL;
            unsigned long p = strtoul(rb + 2, &endp, 10);
            if (endp && *endp == '\0' && p > 0 && p <= 65535u) {
                port = (uint16_t)p;
            }
        }
    } else {
        char *colon = strrchr(hostpart, ':');
        if (colon && strchr(colon + 1, ':') == NULL) {
            *colon = '\0';
            char *endp = NULL;
            unsigned long p = strtoul(colon + 1, &endp, 10);
            if (endp && *endp == '\0' && p > 0 && p <= 65535u) {
                port = (uint16_t)p;
            }
        }
        size_t host_part_len = strlen(hostpart);
        if (host_part_len == 0 || host_part_len >= sizeof(hostbuf)) {
            return -1;
        }
        memcpy(hostbuf, hostpart, host_part_len + 1);
    }

    if (host_len <= strlen(hostbuf)) {
        return -1;
    }
    memcpy(host, hostbuf, strlen(hostbuf) + 1);
    *port_out = port;
    return 0;
}

static int piscsi_remote_send_req(struct piscsi_dev *d, uint16_t op,
                                  uint64_t offset, uint32_t length)
{
    SSL *ssl = (SSL *)d->remote_tls;
    if (!ssl) {
        errno = ENOTCONN;
        return -1;
    }
    piscsi_remote_io_req_t req;
    req.magic_be = htobe32(PISCSI_REMOTE_MAGIC_IOREQ);
    req.version_be = htobe16(PISCSI_REMOTE_VERSION);
    req.op_be = htobe16(op);
    req.offset_be = htobe64(offset);
    req.length_be = htobe32(length);
    req.reserved_be = 0;
    return piscsi_tls_send_all(ssl, &req, sizeof(req));
}

static int piscsi_remote_recv_rsp(struct piscsi_dev *d, piscsi_remote_io_rsp_t *rsp)
{
    SSL *ssl = (SSL *)d->remote_tls;
    if (!ssl) {
        errno = ENOTCONN;
        return -1;
    }
    if (piscsi_tls_recv_all(ssl, rsp, sizeof(*rsp)) != 0) {
        return -1;
    }
    if (be32toh(rsp->magic_be) != PISCSI_REMOTE_MAGIC_IORSP ||
        be16toh(rsp->version_be) != PISCSI_REMOTE_VERSION) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int piscsi_remote_connect(int unit_index, const char *spec, int req_rw,
                                 enum piscsi_media_kind hint_media,
                                 int *sock_out, uint64_t *size_out,
                                 uint32_t *block_size_out,
                                 uint8_t *media_kind_out,
                                 uint8_t *read_only_out,
                                 void **tls_ctx_out, void **tls_out)
{
    char token[128];
    char host[128];
    char export_name[128];
    uint16_t port = PISCSI_REMOTE_DEFAULT_PORT;
    if (piscsi_parse_remote_endpoint(spec, token, sizeof(token), host, sizeof(host),
                                     &port, export_name, sizeof(export_name)) != 0) {
        LOG_ERROR("[PISCSI-REMOTE] Unit %d endpoint parse failed.\n", unit_index);
        errno = EINVAL;
        return -1;
    }
    if (!token[0]) {
        LOG_ERROR("[PISCSI-REMOTE] Unit %d token missing for %s:%u/%s.\n",
                  unit_index, host, (unsigned int)port, export_name);
        errno = EACCES;
        return -1;
    }
    LOG_INFO("[PISCSI-REMOTE] Unit %d connecting to %s:%u/%s (req=%s hint=%s).\n",
             unit_index,
             host,
             (unsigned int)port,
             export_name,
             req_rw ? "rw" : "ro",
             piscsi_media_kind_name((uint8_t)hint_media));

    char port_s[16];
    snprintf(port_s, sizeof(port_s), "%u", (unsigned int)port);
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int gai = getaddrinfo(host, port_s, &hints, &res);
    if (gai != 0) {
        LOG_ERROR("[PISCSI-REMOTE] Unit %d DNS/connect lookup failed for %s:%u (%s).\n",
                  unit_index, host, (unsigned int)port, gai_strerror(gai));
        errno = ECONNREFUSED;
        return -1;
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
        LOG_ERROR("[PISCSI-REMOTE] Unit %d TCP connect failed for %s:%u (errno=%d).\n",
                  unit_index, host, (unsigned int)port, errno);
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    SSL_CTX *tls_ctx = SSL_CTX_new(TLS_client_method());
    if (!tls_ctx) {
        close(sock);
        errno = EIO;
        return -1;
    }
    if (SSL_CTX_set_min_proto_version(tls_ctx, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(tls_ctx, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_cipher_list(tls_ctx, "PSK-AES256-GCM-SHA384:PSK-AES128-GCM-SHA256") != 1) {
        SSL_CTX_free(tls_ctx);
        close(sock);
        errno = EIO;
        return -1;
    }
    SSL_CTX_set_psk_client_callback(tls_ctx, piscsi_tls_psk_client_cb);
    SSL *ssl = SSL_new(tls_ctx);
    if (!ssl) {
        SSL_CTX_free(tls_ctx);
        close(sock);
        errno = EIO;
        return -1;
    }
    piscsi_tls_psk_client_data_t psk_data;
    memset(&psk_data, 0, sizeof(psk_data));
    snprintf(psk_data.identity, sizeof(psk_data.identity), "%s", export_name);
    snprintf(psk_data.token, sizeof(psk_data.token), "%s", token);
    SSL_set_app_data(ssl, &psk_data);
    SSL_set_fd(ssl, sock);
    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        errno = EACCES;
        return -1;
    }
    {
        int bits = 0;
        const char *tls_ver = SSL_get_version(ssl);
        const char *tls_cipher = SSL_get_cipher_name(ssl);
        bits = SSL_get_cipher_bits(ssl, NULL);
        LOG_INFO("[PISCSI-REMOTE] Unit %d TLS established: version=%s cipher=%s bits=%d\n",
                 unit_index,
                 tls_ver ? tls_ver : "unknown",
                 tls_cipher ? tls_cipher : "unknown",
                 bits);
    }

    piscsi_remote_hello_req_t req;
    memset(&req, 0, sizeof(req));
    if (RAND_bytes(req.client_nonce, sizeof(req.client_nonce)) != 1) {
        close(sock);
        LOG_ERROR("[PISCSI-REMOTE] Unit %d nonce generation failed.\n", unit_index);
        errno = EIO;
        return -1;
    }
    uint16_t flags = 0;
    if (req_rw) {
        flags |= PISCSI_REMOTE_FLAG_REQ_RW;
    }
    if (hint_media == PISCSI_MEDIA_CDROM) {
        flags |= PISCSI_REMOTE_FLAG_HINT_CD;
    }
    req.magic_be = htobe32(PISCSI_REMOTE_MAGIC_HELLO);
    req.version_be = htobe16(PISCSI_REMOTE_VERSION);
    req.flags_be = htobe16(flags);
    req.token_len_be = htobe16(0);
    req.export_len_be = htobe16((uint16_t)strlen(export_name));

    if (piscsi_tls_send_all(ssl, &req, sizeof(req)) != 0 ||
        (export_name[0] && piscsi_tls_send_all(ssl, export_name, strlen(export_name)) != 0)) {
        LOG_ERROR("[PISCSI-REMOTE] Unit %d hello send failed (errno=%d).\n", unit_index, errno);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        return -1;
    }

    piscsi_remote_hello_rsp_t rsp;
    if (piscsi_tls_recv_all(ssl, &rsp, sizeof(rsp)) != 0) {
        LOG_ERROR("[PISCSI-REMOTE] Unit %d hello recv failed (errno=%d).\n", unit_index, errno);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        return -1;
    }
    if (be32toh(rsp.magic_be) != PISCSI_REMOTE_MAGIC_HELLO ||
        be16toh(rsp.version_be) != PISCSI_REMOTE_VERSION) {
        LOG_ERROR("[PISCSI-REMOTE] Unit %d hello protocol mismatch.\n", unit_index);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        errno = EPROTO;
        return -1;
    }
    uint16_t status = be16toh(rsp.status_be);
    if (status != 0) {
        LOG_ERROR("[PISCSI-REMOTE] Unit %d hello rejected: status=%u (%s) export=%s.\n",
                  unit_index, (unsigned int)status, piscsi_remote_status_name(status), export_name);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        switch (status) {
            case 1u:
                errno = EACCES;
                break;
            case 2u:
                errno = ENOENT;
                break;
            case 3u:
                errno = ENODEV;
                LOG_ERROR("[PISCSI-REMOTE] Unit %d remote media missing or cannot be opened: %s:%u/%s\n",
                          unit_index, host, (unsigned int)port, export_name);
                break;
            case 4u:
                errno = EPROTO;
                break;
            default:
                errno = EIO;
                break;
        }
        return -1;
    }

    if (sock_out) {
        *sock_out = sock;
    } else {
        close(sock);
    }
    if (size_out) {
        *size_out = be64toh(rsp.size_bytes_be);
    }
    if (block_size_out) {
        *block_size_out = be32toh(rsp.block_size_be);
    }
    if (media_kind_out) {
        *media_kind_out = rsp.media_kind;
    }
    if (read_only_out) {
        *read_only_out = rsp.read_only;
    }
    if (tls_ctx_out) {
        *tls_ctx_out = tls_ctx;
    } else {
        SSL_CTX_free(tls_ctx);
    }
    if (tls_out) {
        *tls_out = ssl;
    } else {
        SSL_free(ssl);
    }

    return 0;
}

static int piscsi_backend_file_close(struct piscsi_dev *d)
{
    if (d && d->fd != -1) {
        close(d->fd);
        d->fd = -1;
    }
    return 0;
}

static off64_t piscsi_backend_file_seek(struct piscsi_dev *d, off64_t offset, int whence)
{
    if (!d) {
        errno = EBADF;
        return -1;
    }
    return lseek64(d->fd, offset, whence);
}

static ssize_t piscsi_backend_file_read(struct piscsi_dev *d, void *buf, size_t count)
{
    if (!d) {
        errno = EBADF;
        return -1;
    }
    return read(d->fd, buf, count);
}

static ssize_t piscsi_backend_file_write(struct piscsi_dev *d, const void *buf, size_t count)
{
    if (!d) {
        errno = EBADF;
        return -1;
    }
    return write(d->fd, buf, count);
}

static ssize_t piscsi_backend_file_pread(struct piscsi_dev *d, void *buf, size_t count,
                                         off64_t offset)
{
    if (!d) {
        errno = EBADF;
        return -1;
    }
    return pread(d->fd, buf, count, offset);
}

static int piscsi_backend_file_sync(struct piscsi_dev *d)
{
    if (!d) {
        errno = EBADF;
        return -1;
    }
    return fsync(d->fd);
}

static const struct piscsi_backend_ops piscsi_backend_file_ops = {
    .name = "file",
    .close = piscsi_backend_file_close,
    .seek = piscsi_backend_file_seek,
    .read = piscsi_backend_file_read,
    .write = piscsi_backend_file_write,
    .pread = piscsi_backend_file_pread,
    .sync = piscsi_backend_file_sync,
};

static off64_t piscsi_backend_remote_seek(struct piscsi_dev *d, off64_t offset, int whence)
{
    if (!d) {
        errno = EBADF;
        return -1;
    }
    off64_t new_pos = d->remote_pos;
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (off64_t)d->remote_pos + offset;
            break;
        case SEEK_END:
            new_pos = (off64_t)d->fs + offset;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    if (new_pos < 0) {
        errno = EINVAL;
        return -1;
    }
    d->remote_pos = (uint64_t)new_pos;
    return new_pos;
}

static ssize_t piscsi_backend_remote_pread(struct piscsi_dev *d, void *buf, size_t count,
                                           off64_t offset)
{
    if (!d) {
        errno = EBADF;
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (piscsi_remote_send_req(d, PISCSI_REMOTE_OP_READ, (uint64_t)offset, (uint32_t)count) != 0) {
        return -1;
    }
    piscsi_remote_io_rsp_t rsp;
    if (piscsi_remote_recv_rsp(d, &rsp) != 0) {
        return -1;
    }
    uint16_t status = be16toh(rsp.status_be);
    uint32_t len = be32toh(rsp.length_be);
    if (status != 0 || len == 0) {
        errno = EIO;
        return -1;
    }
    if (len > count) {
        len = (uint32_t)count;
    }
    if (piscsi_tls_recv_all((SSL *)d->remote_tls, buf, len) != 0) {
        return -1;
    }
    return (ssize_t)len;
}

static ssize_t piscsi_backend_remote_read(struct piscsi_dev *d, void *buf, size_t count)
{
    ssize_t rc = piscsi_backend_remote_pread(d, buf, count, (off64_t)d->remote_pos);
    if (rc > 0) {
        d->remote_pos += (uint64_t)rc;
    }
    return rc;
}

static ssize_t piscsi_backend_remote_write(struct piscsi_dev *d, const void *buf, size_t count)
{
    if (!d) {
        errno = EBADF;
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (piscsi_remote_send_req(d, PISCSI_REMOTE_OP_WRITE, d->remote_pos, (uint32_t)count) != 0) {
        return -1;
    }
    if (piscsi_tls_send_all((SSL *)d->remote_tls, buf, count) != 0) {
        return -1;
    }
    piscsi_remote_io_rsp_t rsp;
    if (piscsi_remote_recv_rsp(d, &rsp) != 0) {
        return -1;
    }
    uint16_t status = be16toh(rsp.status_be);
    if (status != 0) {
        errno = EIO;
        return -1;
    }
    d->remote_pos += (uint64_t)count;
    return (ssize_t)count;
}

static int piscsi_backend_remote_sync(struct piscsi_dev *d)
{
    if (!d) {
        errno = EBADF;
        return -1;
    }
    if (piscsi_remote_send_req(d, PISCSI_REMOTE_OP_SYNC, 0, 0) != 0) {
        return -1;
    }
    piscsi_remote_io_rsp_t rsp;
    if (piscsi_remote_recv_rsp(d, &rsp) != 0) {
        return -1;
    }
    uint16_t status = be16toh(rsp.status_be);
    if (status != 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int piscsi_backend_remote_close(struct piscsi_dev *d)
{
    if (!d) {
        errno = EBADF;
        return -1;
    }
    (void)piscsi_remote_send_req(d, PISCSI_REMOTE_OP_CLOSE, 0, 0);
    if (d->remote_tls) {
        SSL_shutdown((SSL *)d->remote_tls);
        SSL_free((SSL *)d->remote_tls);
        d->remote_tls = NULL;
    }
    if (d->remote_tls_ctx) {
        SSL_CTX_free((SSL_CTX *)d->remote_tls_ctx);
        d->remote_tls_ctx = NULL;
    }
    if (d->remote_sock != -1) {
        close(d->remote_sock);
        d->remote_sock = -1;
    }
    d->fd = -1;
    return 0;
}

static const struct piscsi_backend_ops piscsi_backend_remote_ops = {
    .name = "remote",
    .close = piscsi_backend_remote_close,
    .seek = piscsi_backend_remote_seek,
    .read = piscsi_backend_remote_read,
    .write = piscsi_backend_remote_write,
    .pread = piscsi_backend_remote_pread,
    .sync = piscsi_backend_remote_sync,
};

static off64_t piscsi_dev_seek(struct piscsi_dev *d, off64_t offset, int whence)
{
    if (d && d->backend_ops && d->backend_ops->seek) {
        return d->backend_ops->seek(d, offset, whence);
    }
    errno = ENOTCONN;
    return -1;
}

static ssize_t piscsi_dev_read(struct piscsi_dev *d, void *buf, size_t count)
{
    if (d && d->backend_ops && d->backend_ops->read) {
        return d->backend_ops->read(d, buf, count);
    }
    errno = ENOTCONN;
    return -1;
}

static ssize_t piscsi_dev_write(struct piscsi_dev *d, const void *buf, size_t count)
{
    if (d && d->backend_ops && d->backend_ops->write) {
        return d->backend_ops->write(d, buf, count);
    }
    errno = ENOTCONN;
    return -1;
}

static int piscsi_dev_close(struct piscsi_dev *d)
{
    if (d && d->backend_ops && d->backend_ops->close) {
        return d->backend_ops->close(d);
    }
    return 0;
}

static void piscsi_copy_spec(char *dst, size_t dst_sz, const char *src,
                             const char *field_name, uint8_t unit)
{
    if (!dst || dst_sz == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t src_len = strlen(src);
    size_t copy_len = (src_len < (dst_sz - 1)) ? src_len : (dst_sz - 1);
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';

    if (src_len >= dst_sz) {
        LOG_WARN("[PISCSI] Truncated %s for unit %u (%zu -> %zu chars).\n",
                 field_name, (unsigned int)unit, src_len, dst_sz - 1);
    }
}

static void piscsi_parse_mode_opt(const char *opt, int *mode_opt)
{
    if (!opt || !mode_opt) {
        return;
    }
    if (strncasecmp(opt, "mode=", 5) != 0) {
        return;
    }
    const char *mode = opt + 5;
    if (strcasecmp(mode, "ro") == 0) {
        *mode_opt = 0;
    } else if (strcasecmp(mode, "rw") == 0) {
        *mode_opt = 1;
    } else {
        LOG_WARN("[PISCSI] Unknown mode option '%s' (expected mode=ro|mode=rw)\n", opt);
    }
}

static int piscsi_block_size_supported(uint32_t bs)
{
    switch (bs) {
        case 512u:
        case 4096u:
            return 1;
        default:
            return 0;
    }
}

static void piscsi_parse_block_size_opt(const char *opt, uint32_t *block_size_opt)
{
    if (!opt || !block_size_opt) {
        return;
    }
    if (strncasecmp(opt, "blocksize=", 10) != 0 && strncasecmp(opt, "bs=", 3) != 0) {
        return;
    }
    const char *arg = (strncasecmp(opt, "bs=", 3) == 0) ? (opt + 3) : (opt + 10);
    char *endp = NULL;
    errno = 0;
    unsigned long v = strtoul(arg, &endp, 10);
    if (errno != 0 || endp == arg || (endp && *endp != '\0') || v > UINT32_MAX) {
        LOG_WARN("[PISCSI] Invalid block size option '%s' (expected integer bytes)\n", opt);
        return;
    }
    uint32_t bs = (uint32_t)v;
    if (!piscsi_block_size_supported(bs)) {
        LOG_WARN("[PISCSI] Unsupported block size %u in '%s'. Supported: 512 or 4096\n", bs, opt);
        return;
    }
    *block_size_opt = bs;
}

static int piscsi_split_path_and_opts(const char *path_in, char *path_out, size_t path_out_sz,
                                      int *mode_opt, uint32_t *block_size_opt)
{
    if (!path_in || !path_out || path_out_sz == 0) {
        return -1;
    }
    snprintf(path_out, path_out_sz, "%s", path_in);
    if (mode_opt) {
        *mode_opt = -1;
    }
    if (block_size_opt) {
        *block_size_opt = 0;
    }

    char *comma = strchr(path_out, ',');
    if (!comma) {
        return 0;
    }
    *comma = '\0';
    char *tok = comma + 1;
    while (tok && *tok) {
        char *next = strchr(tok, ',');
        if (next) {
            *next = '\0';
        }
        if (mode_opt) {
            piscsi_parse_mode_opt(tok, mode_opt);
        }
        if (block_size_opt) {
            piscsi_parse_block_size_opt(tok, block_size_opt);
        }
        tok = next ? (next + 1) : NULL;
    }
    return 0;
}

static int str_ends_with_ci(const char *value, const char *suffix) {
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > value_len) {
        return 0;
    }
    return strcasecmp(value + value_len - suffix_len, suffix) == 0;
}

static enum piscsi_media_kind piscsi_parse_media_spec(const char *spec, const char **path_out) {
    const char *path = spec;
    enum piscsi_media_kind media_kind = PISCSI_MEDIA_DISK;

    if (!spec || spec[0] == '\0') {
        if (path_out) {
            *path_out = spec;
        }
        return PISCSI_MEDIA_NONE;
    }

    if (strncasecmp(spec, "disk:", 5) == 0) {
        path = spec + 5;
        media_kind = PISCSI_MEDIA_DISK;
    } else if (strncasecmp(spec, "cdrom:", 6) == 0) {
        path = spec + 6;
        media_kind = PISCSI_MEDIA_CDROM;
    } else {
        if (strncasecmp(spec, "file:", 5) == 0) {
            path = spec + 5;
        }
        media_kind = str_ends_with_ci(path, ".iso") ? PISCSI_MEDIA_CDROM : PISCSI_MEDIA_DISK;
    }

    if (path_out) {
        *path_out = path;
    }
    return media_kind;
}

static __thread char piscsi_disasm_buf[256];

static void piscsi_appendf(char *buf, size_t buf_sz, int *off, const char *fmt, ...) {
    if (!buf || !off || *off < 0 || (size_t)*off >= buf_sz) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, buf_sz - (size_t)*off, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n >= buf_sz - (size_t)*off) {
        *off = (int)buf_sz - 1;
        return;
    }
    *off += n;
}

static void __attribute__((unused)) piscsi_dump_cpu_state(const char *tag) {
    if (log_get_level() < LOG_LEVEL_DEBUG) {
        return;
    }
    unsigned int pc = m68k_get_reg(NULL, M68K_REG_PC);
    unsigned int ppc = m68k_get_reg(NULL, M68K_REG_PPC);
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    unsigned int a7 = m68k_get_reg(NULL, M68K_REG_A7);
    int32_t map_idx = get_mapped_item_by_address(cfg, pc);
    if (map_idx >= 0) {
        LOG_DEBUG("[PISCSI-CPU] PC map[%d] type=%u range=$%.8lX-$%.8lX id=%s\n",
                  map_idx,
                  (unsigned int)cfg->map_type[map_idx],
                  cfg->map_offset[map_idx],
                  cfg->map_high[map_idx] - 1,
                  cfg->map_id[map_idx] ? cfg->map_id[map_idx] : "None");
    } else {
        LOG_DEBUG("[PISCSI-CPU] PC map: unmapped\n");
    }
    LOG_DEBUG("[PISCSI-CPU] %s PC=$%.8X PPC=$%.8X SR=$%.4X\n", tag ? tag : "state", pc, ppc, sr);
    m68k_disassemble(piscsi_disasm_buf, pc, cpu_type);
    LOG_DEBUG("[PISCSI-CPU] %s\n", piscsi_disasm_buf);
    LOG_DEBUG("[PISCSI-CPU] REGA: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n",
              m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
              m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3),
              m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5),
              m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7));
    LOG_DEBUG("[PISCSI-CPU] REGD: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n",
              m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
              m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
              m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5),
              m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));

    uint8_t pc_bytes[18];
    for (int i = -8; i < 10; i++) {
        pc_bytes[i + 8] = (uint8_t)m68k_read_memory_8(pc + (uint32_t)i);
    }
    char pc_line[128];
    int pc_off = 0;
    piscsi_appendf(pc_line, sizeof(pc_line), &pc_off, "[PISCSI-CPU] PC bytes:");
    for (int i = 0; i < 18; i++) {
        piscsi_appendf(pc_line, sizeof(pc_line), &pc_off, " %.2X", pc_bytes[i]);
    }
    LOG_DEBUG("%s\n", pc_line);

    LOG_DEBUG("[PISCSI-CPU] A7=$%.8X stack longs:", a7);
    for (int i = 0; i < 8; i++) {
        uint32_t val = (uint32_t)m68k_read_memory_32(a7 + (uint32_t)(i * 4));
        LOG_DEBUG(" %.8X", val);
    }
    LOG_DEBUG("\n");

    uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0);
    uint32_t a1 = m68k_get_reg(NULL, M68K_REG_A1);
    uint32_t a2 = m68k_get_reg(NULL, M68K_REG_A2);
    int32_t a0_map = get_mapped_item_by_address(cfg, a0);
    int32_t a1_map = get_mapped_item_by_address(cfg, a1);
    int32_t a2_map = get_mapped_item_by_address(cfg, a2);
    LOG_DEBUG("[PISCSI-CPU] A0 map: %d A1 map: %d A2 map: %d\n", a0_map, a1_map, a2_map);

    if (pc_bytes[8] == 0x4C && pc_bytes[9] == 0xDF && pc_bytes[10] == 0x7F && pc_bytes[11] == 0xFF &&
        pc_bytes[12] == 0x4E && pc_bytes[13] == 0x75) {
        uint32_t a7_after = a7 + (15u * 4u);
        uint32_t retaddr = (uint32_t)m68k_read_memory_32(a7_after);
        LOG_DEBUG("[PISCSI-CPU] movem.l (A7)+,D0-D7/A0-A6 -> A7=$%.8X RTS_ret=$%.8X\n", a7_after, retaddr);
        int32_t ret_map = get_mapped_item_by_address(cfg, retaddr);
        if (ret_map >= 0) {
            LOG_DEBUG("[PISCSI-CPU] RTS target map[%d] type=%u range=$%.8lX-$%.8lX id=%s\n",
                      ret_map,
                      (unsigned int)cfg->map_type[ret_map],
                      cfg->map_offset[ret_map],
                      cfg->map_high[ret_map] - 1,
                      cfg->map_id[ret_map] ? cfg->map_id[ret_map] : "None");
        } else {
            LOG_DEBUG("[PISCSI-CPU] RTS target map: unmapped\n");
        }
    }
}
#ifdef FAKESTORM
#define lseek64 lseek
#endif

extern struct emulator_config *cfg;

struct piscsi_dev devs[8];
struct piscsi_fs filesystems[NUM_FILESYSTEMS];

uint8_t piscsi_num_fs = 0;

#define FS_ALLOC_MAX_BYTES (512 * 1024)

static int fs_handler_valid(const struct piscsi_fs *fs, uint32_t handler_addr, uint8_t partition,
                            const char *dosID) {
    if (!fs || !fs->valid) {
        LOG_ERROR("[PISCSI] Rejecting handler for %c%c%c/%d (partition %u): filesystem invalid\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition);
        return 0;
    }
    if (fs->h_info.alloc_size == 0 || fs->h_info.alloc_size > FS_ALLOC_MAX_BYTES) {
        LOG_ERROR("[PISCSI] Rejecting handler for %c%c%c/%d (partition %u): invalid alloc_size=%u\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition, fs->h_info.alloc_size);
        return 0;
    }
    if ((handler_addr & 1u) != 0) {
        LOG_ERROR("[PISCSI] Rejecting handler for %c%c%c/%d (partition %u): handler=0x%08X not aligned\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition, handler_addr);
        return 0;
    }
    if (!fs->h_info.hunk_offsets || fs->h_info.num_hunks == 0 ||
        fs->h_info.hunk_offsets[0] >= fs->h_info.alloc_size) {
        LOG_ERROR("[PISCSI] Rejecting handler for %c%c%c/%d (partition %u): invalid hunk table\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition);
        return 0;
    }
    if (fs->h_info.base_offset == 0 || handler_addr < fs->h_info.base_offset ||
        (handler_addr - fs->h_info.base_offset) >= fs->h_info.alloc_size) {
        LOG_ERROR("[PISCSI] Rejecting handler for %c%c%c/%d (partition %u): handler=0x%08X "
                  "outside buffer base=0x%08X size=0x%08X\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition, handler_addr,
                  fs->h_info.base_offset, fs->h_info.alloc_size);
        return 0;
    }
    if (fs->h_info.header_size >= fs->h_info.alloc_size || (fs->h_info.header_size & 3u) != 0) {
        LOG_ERROR("[PISCSI] Rejecting handler for %c%c%c/%d (partition %u): header_size=0x%08X "
                  "invalid for alloc=0x%08X\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition, fs->h_info.header_size,
                  fs->h_info.alloc_size);
        return 0;
    }
    uint32_t seglist_addr = handler_addr + fs->h_info.header_size;
    if (seglist_addr < fs->h_info.base_offset ||
        (seglist_addr - fs->h_info.base_offset) >= fs->h_info.alloc_size) {
        LOG_ERROR("[PISCSI] Rejecting handler for %c%c%c/%d (partition %u): seglist=0x%08X "
                  "outside buffer base=0x%08X size=0x%08X\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition, seglist_addr,
                  fs->h_info.base_offset, fs->h_info.alloc_size);
        return 0;
    }
    return 1;
}

static int piscsi_get_map_bounds(struct emulator_config *cfg_local, uint32_t addr, uint32_t len,
                                 uint8_t **map_out, uint32_t *avail_out) {
    int32_t map_idx = get_mapped_item_by_address(cfg_local, addr);
    if (map_idx < 0) {
        if (map_out) {
            *map_out = NULL;
        }
        if (avail_out) {
            *avail_out = 0;
        }
        return -1;
    }

    if (cfg_local->map_type[map_idx] == MAPTYPE_ROM) {
        if (map_out) {
            *map_out = NULL;
        }
        if (avail_out) {
            *avail_out = 0;
        }
        LOG_ERROR("[PISCSI] Refusing DMA into ROM map %d at 0x%08X\n", map_idx, addr);
        return -2;
    }

    uint32_t high = (uint32_t)cfg_local->map_high[map_idx];
    if (addr >= high) {
        if (map_out) {
            *map_out = NULL;
        }
        if (avail_out) {
            *avail_out = 0;
        }
        return -1;
    }

    uint32_t avail = high - addr;
    if (map_out) {
        *map_out = cfg_local->map_data[map_idx] + (addr - cfg_local->map_offset[map_idx]);
    }
    if (avail_out) {
        *avail_out = avail;
    }
    if (len > avail) {
        LOG_ERROR("[PISCSI] Refusing %u-byte DMA at 0x%08X: exceeds map end (avail=%u)\n",
                  len, addr, avail);
        return -2;
    }

    return 0;
}

static int piscsi_get_dma_window(struct emulator_config *cfg_local, uint8_t **buf_out,
                                 uint32_t *size_out, int32_t *map_idx_out) {
    int32_t idx = get_named_mapped_item(cfg_local, "piscsi_dma");
    if (idx < 0 || !cfg_local->map_data[idx]) {
        return -1;
    }

    uint32_t size = (uint32_t)(cfg_local->map_high[idx] - cfg_local->map_offset[idx]);
    if (size == 0) {
        return -1;
    }

    if (buf_out) {
        *buf_out = cfg_local->map_data[idx];
    }
    if (size_out) {
        *size_out = size;
    }
    if (map_idx_out) {
        *map_idx_out = idx;
    }
    return 0;
}

uint8_t piscsi_cur_drive = 0;
uint32_t piscsi_u32[4];
uint32_t piscsi_dbg[8];
uint32_t piscsi_rom_size = 0;
uint8_t *piscsi_rom_ptr;
static uint32_t last_debugme_idx = 0xFFFFFFFFu;

uint32_t rom_partitions[128];
uint32_t rom_partition_prio[128];
uint32_t rom_partition_dostype[128];
uint32_t rom_cur_partition = 0, rom_cur_fs = 0;

extern unsigned char ac_piscsi_rom[];

char partition_names[128][32];
unsigned int times_used[128];
unsigned int num_partition_names = 0;

struct hunk_info piscsi_hinfo;
struct hunk_reloc piscsi_hreloc[256];

void piscsi_init(void) {
    for (int i = 0; i < 8; i++) {
        if (i < NUM_UNITS) {
            osd_led_piscsi_set_unit_present(i, 0);
        }
        devs[i].fd = -1;
        devs[i].remote_sock = -1;
        devs[i].lba = 0;
        devs[i].c = devs[i].h = devs[i].s = 0;
        devs[i].fs = 0;
        devs[i].block_size = 0;
        devs[i].backend_type = PISCSI_BACKEND_NONE;
        devs[i].backend_ops = NULL;
        devs[i].backend_spec[0] = '\0';
        devs[i].configured_spec[0] = '\0';
        devs[i].media_kind = PISCSI_MEDIA_NONE;
        devs[i].read_only = 0;
    }

    if (piscsi_rom_ptr == NULL) {
        FILE *in = fopen("./src/platforms/amiga/piscsi/piscsi.rom", "rb");
        if (in == NULL) {
            LOG_ERROR("[PISCSI] Could not open PISCSI Boot ROM file for reading!\n");
            // Zero out the boot ROM offset from the autoconfig ROM.
            ac_piscsi_rom[20] = 0;
            ac_piscsi_rom[21] = 0;
            ac_piscsi_rom[22] = 0;
            ac_piscsi_rom[23] = 0;
            return;
        }
        fseek(in, 0, SEEK_END);
        piscsi_rom_size = (uint32_t)ftell(in);
        fseek(in, 0, SEEK_SET);
        piscsi_rom_ptr = malloc(piscsi_rom_size);
        fread(piscsi_rom_ptr, piscsi_rom_size, 1, in);
        LOG_INFO("[PISCSI] Loaded boot ROM: %u bytes from %s\n",
                 piscsi_rom_size, "./src/platforms/amiga/piscsi/piscsi.rom");

        fseek(in, PISCSI_DRIVER_OFFSET, SEEK_SET);
        process_hunks(in, &piscsi_hinfo, piscsi_hreloc, PISCSI_DRIVER_OFFSET);
        LOG_INFO("[PISCSI] Boot ROM driver offset=0x%X, hunks=%u\n",
                 PISCSI_DRIVER_OFFSET, piscsi_hinfo.num_hunks);
        uint32_t driver_size = 0x4000 - PISCSI_DRIVER_OFFSET;
        piscsi_hinfo.byte_size = driver_size;
        piscsi_hinfo.alloc_size = driver_size + piscsi_hinfo.bss_size;

        fclose(in);

        printf("[PISCSI] Loaded Boot ROM.\n");
    } else {
        printf("[PISCSI] Boot ROM already loaded.\n");
    }
    fflush(stdout);
}

void piscsi_shutdown(void) {
    printf("[PISCSI] Shutting down PiSCSI.\n");
    for (int i = 0; i < 8; i++) {
        if (i < NUM_UNITS) {
            osd_led_piscsi_set_unit_present(i, 0);
        }
        if (devs[i].fd != -1) {
            piscsi_dev_close(&devs[i]);
            devs[i].fd = -1;
            devs[i].remote_sock = -1;
            devs[i].block_size = 0;
            devs[i].backend_type = PISCSI_BACKEND_NONE;
            devs[i].backend_ops = NULL;
            devs[i].backend_spec[0] = '\0';
            devs[i].configured_spec[0] = '\0';
            devs[i].media_kind = PISCSI_MEDIA_NONE;
            devs[i].read_only = 0;
        }
    }

    for (int i = 0; i < NUM_FILESYSTEMS; i++) {
        if (filesystems[i].binary_data) {
            free(filesystems[i].binary_data);
            filesystems[i].binary_data = NULL;
        }
        if (filesystems[i].fhb) {
            free(filesystems[i].fhb);
            filesystems[i].fhb = NULL;
        }
        filesystems[i].h_info.current_hunk = 0;
        filesystems[i].h_info.reloc_hunks = 0;
        filesystems[i].FS_ID = 0;
        filesystems[i].handler = 0;
        filesystems[i].valid = 0;
    }
}

static void piscsi_find_partitions(struct piscsi_dev *d) {
    int cur_partition = 0;
    uint8_t tmp;

    for (int i = 0; i < 16; i++) {
        if (d->pb[i]) {
            free(d->pb[i]);
            d->pb[i] = NULL;
        }
    }

    if (!d->rdb || d->rdb->rdb_PartitionList == 0) {
        DEBUG("[PISCSI] No partitions on disk.\n");
        return;
    }

    char *block = malloc(d->block_size);

    piscsi_dev_seek(d, (off64_t)BE(d->rdb->rdb_PartitionList) * d->block_size, SEEK_SET);
next_partition:;
    piscsi_dev_read(d, block, d->block_size);

    uint32_t first_temp;
    memcpy(&first_temp, &block[0], sizeof(first_temp));
    uint32_t first = be32toh(first_temp);
    if (first != PART_IDENTIFIER) {
        DEBUG("Entry at block %d is not a valid partition. Aborting.\n", BE(d->rdb->rdb_PartitionList));
        return;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
    /*
     * Cast alignment warning intentionally suppressed:
     * PiSCSI interprets Amiga-side structures that arrive as byte streams.
     * On the Pi side these are represented as C structs with 32-bit alignment.
     * The external contract guarantees these blocks are properly aligned as expected by the Amiga.
     */
    struct PartitionBlock *pb = (struct PartitionBlock *)((char *)block);
#pragma GCC diagnostic pop
    tmp = pb->pb_DriveName[0];
    pb->pb_DriveName[tmp + 1] = 0x00;
    LOG_INFO("[PISCSI] Partition %d: %s (%d)\n", cur_partition, pb->pb_DriveName + 1, pb->pb_DriveName[0]);
    DEBUG("Checksum: %.8X HostID: %d\n", BE(pb->pb_ChkSum), BE(pb->pb_HostID));
    DEBUG("Flags: %d (%.8X) Devflags: %d (%.8X)\n", BE(pb->pb_Flags), BE(pb->pb_Flags), BE(pb->pb_DevFlags), BE(pb->pb_DevFlags));
    d->pb[cur_partition] = pb;

    for (int i = 0; i < 128; i++) {
        if (strcmp((char *)pb->pb_DriveName + 1, partition_names[i]) == 0) {
            DEBUG("[PISCSI] Duplicate partition name %s. Temporarily renaming to %s_%d.\n", pb->pb_DriveName + 1, pb->pb_DriveName + 1, times_used[i] + 1);
            times_used[i]++;
            sprintf((char *)pb->pb_DriveName + 1 + pb->pb_DriveName[0], "_%d", times_used[i]);
            pb->pb_DriveName[0] += 2;
            if (times_used[i] > 9)
                pb->pb_DriveName[0]++;
            goto partition_renamed;
        }
    }
    sprintf(partition_names[num_partition_names], "%s", pb->pb_DriveName + 1);
    num_partition_names++;

partition_renamed:
    if (d->pb[cur_partition]->pb_Next != 0xFFFFFFFF) {
        uint64_t next = be32toh(pb->pb_Next);
        block = malloc(d->block_size);
        piscsi_dev_seek(d, (off64_t)(next * d->block_size), SEEK_SET);
        cur_partition++;
        DEBUG("[PISCSI] Next partition at block %d.\n", be32toh(pb->pb_Next));
        goto next_partition;
    }
    DEBUG("[PISCSI] No more partitions on disk.\n");
    d->num_partitions = (uint8_t)(cur_partition + 1);
    d->fshd_offs = (uint32_t)piscsi_dev_seek(d, 0, SEEK_CUR);

    return;
}

static int piscsi_parse_rdb(struct piscsi_dev *d) {
    int i = 0;
    uint8_t *block = malloc(PISCSI_MAX_BLOCK_SIZE);

    piscsi_dev_seek(d, 0, SEEK_SET);
    for (i = 0; i < RDB_BLOCK_LIMIT; i++) {
        piscsi_dev_read(d, block, PISCSI_MAX_BLOCK_SIZE);
        uint32_t first_temp;
        memcpy(&first_temp, &block[0], sizeof(first_temp));
        uint32_t first = be32toh(first_temp);
        if (first == RDB_IDENTIFIER)
            goto rdb_found;
    }
    goto no_rdb_found;
rdb_found:;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
    /*
     * Cast alignment warning intentionally suppressed:
     * PiSCSI interprets Amiga-side structures that arrive as byte streams.
     * On the Pi side these are represented as C structs with 32-bit alignment.
     * The external contract guarantees these blocks are properly aligned as expected by the Amiga.
     */
    struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)((char *)block);
#pragma GCC diagnostic pop
    DEBUG("[PISCSI] RDB found at block %d.\n", i);
    d->c = be32toh(rdb->rdb_Cylinders);
    d->h = (uint16_t)be32toh(rdb->rdb_Heads);
    d->s = (uint16_t)be32toh(rdb->rdb_Sectors);
    d->num_partitions = 0;
    DEBUG("[PISCSI] RDB - first partition at block %d.\n", be32toh(rdb->rdb_PartitionList));
    d->block_size = be32toh(rdb->rdb_BlockBytes);
    DEBUG("[PISCSI] Block size: %d. (%d)\n", be32toh(rdb->rdb_BlockBytes), d->block_size);
    if (d->rdb)
        free(d->rdb);
    d->rdb = rdb;
    sprintf(d->rdb->rdb_DriveInitName, "pi-scsi.device");
    return 0;

no_rdb_found:;
    if (block)
        free(block);

    return -1;
}

void piscsi_refresh_drives(void) {
    piscsi_num_fs = 0;

    for (int i = 0; i < NUM_FILESYSTEMS; i++) {
        if (filesystems[i].binary_data) {
            free(filesystems[i].binary_data);
            filesystems[i].binary_data = NULL;
        }
        if (filesystems[i].fhb) {
            free(filesystems[i].fhb);
            filesystems[i].fhb = NULL;
        }
        filesystems[i].h_info.current_hunk = 0;
        filesystems[i].h_info.reloc_hunks = 0;
        filesystems[i].FS_ID = 0;
        filesystems[i].handler = 0;
        filesystems[i].valid = 0;
    }

    rom_cur_fs = 0;

    for (int i = 0; i < 128; i++) {
        memset(partition_names[i], 0x00, 32);
        times_used[i] = 0;
    }
    num_partition_names = 0;

    for (int i = 0; i < NUM_UNITS; i++) {
        if (devs[i].fd != -1 && devs[i].media_kind == PISCSI_MEDIA_DISK) {
            LOG_INFO("[PISCSI] Refresh drive %d: block_size=%u fs=%llu bytes\n",
                     i, devs[i].block_size, (unsigned long long)devs[i].fs);
            piscsi_parse_rdb(&devs[i]);
            piscsi_find_partitions(&devs[i]);
            LOG_INFO("[PISCSI] Drive %d partitions found: %u\n", i, devs[i].num_partitions);
            if (devs[i].backend_type != PISCSI_BACKEND_REMOTE) {
                piscsi_find_filesystems(&devs[i]);
            }
        }
    }
}

void piscsi_find_filesystems(struct piscsi_dev *d) {
    if (!d->num_partitions)
        return;

    uint8_t fs_found = 0;

    uint8_t *fhb_block = malloc(d->block_size);

    piscsi_dev_seek(d, d->fshd_offs, SEEK_SET);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
    /*
     * Cast alignment warning intentionally suppressed:
     * PiSCSI interprets Amiga-side structures that arrive as byte streams.
     * On the Pi side these are represented as C structs with 32-bit alignment.
     * The external contract guarantees these blocks are properly aligned as expected by the Amiga.
     */
    struct FileSysHeaderBlock *fhb = (struct FileSysHeaderBlock *)((char *)fhb_block);
#pragma GCC diagnostic pop
    piscsi_dev_read(d, fhb_block, d->block_size);

    while (BE(fhb->fhb_ID) == FS_IDENTIFIER) {
        char *dosID = (char *)&fhb->fhb_DosType;
#ifdef PISCSI_DEBUG
        uint16_t *fsVer = (uint16_t *)&fhb->fhb_Version;

        DEBUG("[FSHD] FSHD Block found.\n");
        DEBUG("[FSHD] HostID: %d Next: %d Size: %d\n", BE(fhb->fhb_HostID), BE(fhb->fhb_Next), BE(fhb->fhb_SummedLongs));
        DEBUG("[FSHD] Flags: %.8X DOSType: %c%c%c/%d\n", BE(fhb->fhb_Flags), dosID[0], dosID[1], dosID[2], dosID[3]);
        DEBUG("[FSHD] Version: %d.%d\n", BE16(fsVer[0]), BE16(fsVer[1]));
        DEBUG("[FSHD] Patchflags: %d Type: %d\n", BE(fhb->fhb_PatchFlags), BE(fhb->fhb_Type));
        DEBUG("[FSHD] Task: %d Lock: %d\n", BE(fhb->fhb_Task), BE(fhb->fhb_Lock));
        DEBUG("[FSHD] Handler: %d StackSize: %d\n", BE(fhb->fhb_Handler), BE(fhb->fhb_StackSize));
        DEBUG("[FSHD] Prio: %d Startup: %d (%.8X)\n", BE(fhb->fhb_Priority), BE(fhb->fhb_Startup), BE(fhb->fhb_Startup));
        DEBUG("[FSHD] SegListBlocks: %d GlobalVec: %d\n", BE(fhb->fhb_Priority), BE(fhb->fhb_Startup));
        DEBUG("[FSHD] FileSysName: %s\n", fhb->fhb_FileSysName + 1);
#endif

        for (int i = 0; i < NUM_FILESYSTEMS; i++) {
            if (filesystems[i].FS_ID == fhb->fhb_DosType) {
                DEBUG("[FSHD] File system %c%c%c/%d already loaded. Skipping.\n", dosID[0], dosID[1], dosID[2], dosID[3]);
                if (BE(fhb->fhb_Next) == 0xFFFFFFFF)
                    goto fs_done;

                goto skip_fs_load_lseg;
            }
        }

        if (load_lseg(d->fd, &filesystems[piscsi_num_fs].binary_data, &filesystems[piscsi_num_fs].h_info, filesystems[piscsi_num_fs].relocs, d->block_size) != -1) {
            filesystems[piscsi_num_fs].FS_ID = fhb->fhb_DosType;
            filesystems[piscsi_num_fs].fhb = fhb;
            filesystems[piscsi_num_fs].valid = 1;
            printf("[FSHD] Loaded and set up file system %d: %c%c%c/%d\n", piscsi_num_fs + 1, dosID[0], dosID[1], dosID[2], dosID[3]);
            {
                char fs_save_filename[256];
                memset(fs_save_filename, 0x00, 256);
                sprintf(fs_save_filename, "./data/fs/%c%c%c.%d", dosID[0], dosID[1], dosID[2], dosID[3]);
                FILE *save_fs = fopen(fs_save_filename, "rb");
                if (save_fs == NULL) {
                    save_fs = fopen(fs_save_filename, "wb+");
                    if (save_fs != NULL) {
                        fwrite(filesystems[piscsi_num_fs].binary_data, filesystems[piscsi_num_fs].h_info.byte_size, 1, save_fs);
                        fclose(save_fs);
                        printf("[FSHD] File system %c%c%c/%d saved to fs storage.\n", dosID[0], dosID[1], dosID[2], dosID[3]);
                    } else {
                        printf("[FSHD] Failed to save file system to fs storage. (Permission issues?)\n");
                    }
                } else {
                    fclose(save_fs);
                }
            }
            piscsi_num_fs++;
        } else {
            filesystems[piscsi_num_fs].valid = 0;
        }

skip_fs_load_lseg:;
        fs_found++;
        piscsi_dev_seek(d, (off64_t)BE(fhb->fhb_Next) * d->block_size, SEEK_SET);
        fhb_block = malloc(d->block_size);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
        /*
         * Cast alignment warning intentionally suppressed:
         * PiSCSI interprets Amiga-side structures that arrive as byte streams.
         * On the Pi side these are represented as C structs with 32-bit alignment.
         * The external contract guarantees these blocks are properly aligned as expected by the Amiga.
         */
        fhb = (struct FileSysHeaderBlock *)((char *)fhb_block);
#pragma GCC diagnostic pop
        piscsi_dev_read(d, fhb_block, d->block_size);
    }

    if (!fs_found) {
        DEBUG("[!!!FSHD] No file systems found on hard drive!\n");
    }

fs_done:;
    if (fhb_block)
        free(fhb_block);
}

struct piscsi_dev *piscsi_get_dev(uint8_t index) {
    return &devs[index];
}

void piscsi_map_drive(const char *spec, uint8_t index) {
    if (index > 7) {
        LOG_ERROR("[PISCSI] Drive index %d out of range. Unable to map spec %s to drive.\n",
                  index, spec ? spec : "(null)");
        return;
    }

    if (!spec || spec[0] == '\0') {
        LOG_ERROR("[PISCSI] Empty drive spec for unit %d.\n", index);
        return;
    }

    const char *path_spec = NULL;
    char path[PATH_MAX];
    int mode_opt = -1; /* -1 default, 0 ro, 1 rw */
    uint32_t block_size_opt = 0;
    enum piscsi_backend_type backend_type = PISCSI_BACKEND_FILE;
    enum piscsi_media_kind media_kind = piscsi_parse_media_spec(spec, &path_spec);
    if (piscsi_split_path_and_opts(path_spec, path, sizeof(path), &mode_opt, &block_size_opt) != 0) {
        LOG_ERROR("[PISCSI] Invalid drive spec '%s' for unit %d.\n", spec, index);
        return;
    }
    if (!path[0]) {
        LOG_ERROR("[PISCSI] Invalid drive spec '%s' for unit %d.\n", spec, index);
        return;
    }

    if (strncasecmp(path, "remote:", 7) == 0) {
        backend_type = PISCSI_BACKEND_REMOTE;
    } else if (strncmp(path, "/dev/", 5) == 0) {
        backend_type = PISCSI_BACKEND_BLOCK;
    }

    int open_flags;
    if (media_kind == PISCSI_MEDIA_CDROM) {
        open_flags = O_RDONLY;
    } else if (mode_opt == 0) {
        open_flags = O_RDONLY;
    } else if (mode_opt == 1) {
        open_flags = O_RDWR;
    } else if (backend_type == PISCSI_BACKEND_BLOCK ||
               backend_type == PISCSI_BACKEND_REMOTE) {
        open_flags = O_RDONLY;
    } else {
        open_flags = O_RDWR;
    }
    int read_only = (open_flags == O_RDONLY) ? 1 : 0;
    int32_t tmp_fd = -1;
    uint64_t file_size = 0;
    uint32_t remote_block_size = 0;
    uint8_t remote_media_kind = PISCSI_MEDIA_NONE;
    uint8_t remote_read_only = 0;
    void *remote_tls_ctx = NULL;
    void *remote_tls = NULL;

    if (backend_type == PISCSI_BACKEND_REMOTE) {
        if (block_size_opt != 0) {
            LOG_WARN("[PISCSI] Unit %d ignores blocksize=%u for remote backend (server defines block size).\n",
                     index, block_size_opt);
        }
        int req_rw = (media_kind != PISCSI_MEDIA_CDROM && open_flags == O_RDWR) ? 1 : 0;
        if (piscsi_remote_connect(index, path, req_rw, media_kind, &tmp_fd, &file_size,
                                  &remote_block_size, &remote_media_kind, &remote_read_only,
                                  &remote_tls_ctx, &remote_tls) != 0) {
            LOG_ERROR("[PISCSI] Failed to connect remote backend %s for drive %d (errno=%d).\n",
                      path, index, errno);
            return;
        }
        if (remote_media_kind == PISCSI_MEDIA_CDROM) {
            media_kind = PISCSI_MEDIA_CDROM;
        }
        if (remote_read_only) {
            read_only = 1;
        }
    } else {
        tmp_fd = open(path, open_flags);
        if (tmp_fd == -1) {
            int first_errno = errno;
            if (backend_type == PISCSI_BACKEND_FILE &&
                media_kind == PISCSI_MEDIA_DISK &&
                (first_errno == EACCES || first_errno == EPERM || first_errno == EROFS)) {
                tmp_fd = open(path, O_RDONLY);
                if (tmp_fd != -1) {
                    read_only = 1;
                }
            }
            if (tmp_fd == -1) {
                LOG_ERROR("[PISCSI] Failed to open %s, could not map drive %d (errno=%d).\n",
                          path, index, first_errno);
                return;
            }
        }

        off64_t file_size_off = lseek64(tmp_fd, 0, SEEK_END);
        if (file_size_off < 0) {
            LOG_ERROR("[PISCSI] Failed to determine size for %s (unit %d).\n", path, index);
            close(tmp_fd);
            return;
        }
        file_size = (uint64_t)file_size_off;
        lseek64(tmp_fd, 0, SEEK_SET);
    }

    piscsi_unmap_drive(index);

    struct piscsi_dev *d = &devs[index];
    d->fs = file_size;
    d->fd = tmp_fd;
    d->remote_sock = (backend_type == PISCSI_BACKEND_REMOTE) ? tmp_fd : -1;
    d->remote_pos = 0;
    d->remote_last_probe_ms = 0;
    d->remote_tx_ctr = 1;
    d->remote_rx_ctr = 1;
    d->remote_crypto_enabled = 0;
    d->remote_tls_ctx = (backend_type == PISCSI_BACKEND_REMOTE) ? remote_tls_ctx : NULL;
    d->remote_tls = (backend_type == PISCSI_BACKEND_REMOTE) ? remote_tls : NULL;
    if (backend_type == PISCSI_BACKEND_REMOTE) {
        memset(d->remote_key, 0, sizeof(d->remote_key));
        memset(d->remote_iv, 0, sizeof(d->remote_iv));
    } else {
        memset(d->remote_key, 0, sizeof(d->remote_key));
        memset(d->remote_iv, 0, sizeof(d->remote_iv));
    }
    d->remote_block_size = remote_block_size;
    d->remote_media_kind = remote_media_kind;
    d->backend_type = backend_type;
    if (backend_type == PISCSI_BACKEND_REMOTE) {
        d->backend_ops = &piscsi_backend_remote_ops;
    } else {
        d->backend_ops = &piscsi_backend_file_ops;
    }
    piscsi_copy_spec(d->backend_spec, sizeof(d->backend_spec), path, "backend_spec", index);
    piscsi_copy_spec(d->configured_spec, sizeof(d->configured_spec), spec, "configured_spec", index);
    d->media_kind = (uint8_t)media_kind;
    d->read_only = (uint8_t)read_only;

    LOG_INFO("[PISCSI] Map %d: [%s] (%s%s,%s) - %lu bytes.\n",
             index, path,
             media_kind == PISCSI_MEDIA_CDROM ? "cdrom" : "disk",
             d->read_only ? ",ro" : ",rw",
             (d->backend_ops && d->backend_ops->name) ? d->backend_ops->name : "unknown",
             (unsigned long)file_size);

    if (media_kind == PISCSI_MEDIA_CDROM) {
        if (block_size_opt != 0 && block_size_opt != 2048u) {
            LOG_WARN("[PISCSI] Unit %d ignores blocksize=%u for CD-ROM media (fixed 2048 bytes).\n",
                     index, block_size_opt);
        }
        uint32_t cd_block = (backend_type == PISCSI_BACKEND_REMOTE && remote_block_size) ? remote_block_size : 2048u;
        uint64_t blocks = (file_size / cd_block);
        d->block_size = cd_block;
        d->h = 1;
        d->s = 1;
        d->c = (uint32_t)((blocks == 0) ? 1 : ((blocks > UINT32_MAX) ? UINT32_MAX : blocks));
        d->num_partitions = 0;
        d->fshd_offs = 0;
        LOG_INFO("[PISCSI] Unit %d configured as CD-ROM (%llu blocks @ %u bytes).\n",
                 index, (unsigned long long)blocks, cd_block);
        osd_led_piscsi_set_unit_present(index, 1);
        return;
    }

    if (backend_type != PISCSI_BACKEND_REMOTE) {
        uint8_t hdfID[4] = {0};
        ssize_t id_read = pread(tmp_fd, hdfID, sizeof(hdfID), 0);
        if (id_read == (ssize_t)sizeof(hdfID) &&
            (memcmp(hdfID, "DOS", 3) == 0 ||
             memcmp(hdfID, "PFS", 3) == 0 ||
             memcmp(hdfID, "PDS", 3) == 0 ||
             memcmp(hdfID, "SFS", 3) == 0 ||
             memcmp(hdfID, "MSH", 3) == 0 ||
             memcmp(hdfID, "MSD", 3) == 0 ||
             memcmp(hdfID, "UNI", 3) == 0)) {
            printf("[!!!PISCSI] The disk image %s is a UAE Single Partition Hardfile!\n", path);
            printf("[!!!PISCSI] Detected DOSType signature: %c%c%c\\x%02X (0x%02X%02X%02X%02X)\n",
                   hdfID[0], hdfID[1], hdfID[2], hdfID[3], hdfID[0], hdfID[1], hdfID[2], hdfID[3]);
            printf("[!!!PISCSI] WARNING: PiSCSI does NOT support UAE Single Partition Hardfiles!\n");
            printf("[!!!PISCSI] PLEASE check the PiSCSI readme file in the GitHub repo for more information.\n");
            printf("[!!!PISCSI] If this is merely an empty or placeholder file you've created to partition and format on the Amiga, please disregard this warning message.\n");
        }
    }

    uint32_t fallback_block = 512u;
    if (backend_type == PISCSI_BACKEND_REMOTE && remote_block_size) {
        fallback_block = remote_block_size;
    } else if (block_size_opt != 0) {
        fallback_block = block_size_opt;
    }
    if (piscsi_parse_rdb(d) == -1) {
        DEBUG("[PISCSI] No RDB found on disk, making up some CHS values.\n");
        d->h = 16;
        d->s = 63;
        d->c = (uint32_t)((file_size / fallback_block) / (d->s * d->h));
        d->block_size = fallback_block;
    }
    printf("[PISCSI] CHS: %d %d %d\n", d->c, d->h, d->s);

    printf("Finding partitions.\n");
    piscsi_find_partitions(d);
    if (backend_type != PISCSI_BACKEND_REMOTE) {
        printf("Finding file systems.\n");
        piscsi_find_filesystems(d);
    } else {
        DEBUG("[PISCSI] Skipping FSHD extraction for remote unit (not yet supported).\n");
    }
    printf("Done.\n");

    if (backend_type != PISCSI_BACKEND_REMOTE) {
        printf("[PISCSI-SELFTEST] Running HDF integrity validation for drive %d...\n", index);
        if (!piscsi_validate_hdf(d, path)) {
            printf("[PISCSI-SELFTEST-ERROR] HDF validation failed for drive %d (%s)\n", index, path);
        } else {
            printf("[PISCSI-SELFTEST-SUCCESS] HDF validation passed for drive %d (%s)\n", index, path);
        }
    }

    osd_led_piscsi_set_unit_present(index, 1);
}

// HDF integrity validation function
int piscsi_validate_hdf(struct piscsi_dev *d, const char *filename) {
    if (!d || d->fd == -1) {
        printf("[PISCSI-SELFTEST] ERROR: Invalid device or file descriptor\n");
        return 0;
    }
    if (d->backend_type == PISCSI_BACKEND_REMOTE) {
        printf("[PISCSI-SELFTEST] INFO: Skipping HDF validation for remote backend (%s)\n",
               d->backend_spec);
        return 1;
    }

    // Test 1: Read RDB block 0 (first 512 bytes)
    uint8_t rdb_block[512];
    if (piscsi_dev_seek(d, 0, SEEK_SET) == (off64_t)-1) {
        printf("[PISCSI-SELFTEST] ERROR: Cannot seek to RDB block 0 in %s\n", filename);
        return 0;
    }

    ssize_t bytes_read = piscsi_dev_read(d, rdb_block, 512);
    if (bytes_read < 512) {
        printf("[PISCSI-SELFTEST] ERROR: Cannot read full RDB block 0 from %s (got %zd bytes)\n", filename, bytes_read);
        return 0;
    }

    // Verify RDB signature (should start with "RDSK")
    if (rdb_block[0] == 'R' && rdb_block[1] == 'D' && rdb_block[2] == 'S' && rdb_block[3] == 'K') {
        printf("[PISCSI-SELFTEST] INFO: Valid RDB signature found in %s\n", filename);
    } else {
        // Not all HDFs have RDB, some are partitioned drives, so this isn't always an error
        printf("[PISCSI-SELFTEST] INFO: No RDB signature found in %s (may be partitioned drive)\n", filename);
    }

    // Test 2: For DH0 (unit 0), try to read first partition block (usually at block 2 or offset 1024)
    if (d - devs == 0) { // This is unit 0 (DH0)
        printf("[PISCSI-SELFTEST] Testing DH0 partition accessibility...\n");

        // Look for first partition block (usually at offset 1024 for standard Amiga HDFs)
        uint8_t boot_block[512];
        if (piscsi_dev_seek(d, 1024, SEEK_SET) == (off64_t)-1) {
            printf("[PISCSI-SELFTEST] ERROR: Cannot seek to DH0 boot block in %s\n", filename);
            return 0;
        }

        bytes_read = piscsi_dev_read(d, boot_block, 512);
        if (bytes_read < 512) {
            printf("[PISCSI-SELFTEST] ERROR: Cannot read DH0 boot block from %s (got %zd bytes)\n", filename, bytes_read);
            return 0;
        }

        // Check for DOS boot block signature (starts with 0x444F5300 = "DOS\0")
        uint32_t dos_sig = ((uint32_t)boot_block[0] << 24) | ((uint32_t)boot_block[1] << 16) | ((uint32_t)boot_block[2] << 8) | (uint32_t)boot_block[3];
        if (dos_sig == 0x444F5300) {
            printf("[PISCSI-SELFTEST] SUCCESS: Valid DOS boot block signature found in DH0\n");
        } else {
            printf("[PISCSI-SELFTEST] INFO: No DOS boot block signature in DH0 (signature: 0x%08X)\n", dos_sig);
        }
    }

    // Test 3: Verify we can seek to end of file
    off64_t file_end = piscsi_dev_seek(d, 0, SEEK_END);
    if (file_end == (off64_t)-1) {
        printf("[PISCSI-SELFTEST] ERROR: Cannot seek to end of file %s\n", filename);
        return 0;
    }

    if ((uint64_t)file_end != d->fs) {
        printf("[PISCSI-SELFTEST] WARNING: File size mismatch: reported=%llu, actual=%lld\n",
               (unsigned long long)d->fs, (long long)file_end);
    }

    // Test 4: Try reading a few random blocks to verify integrity
    for (int i = 0; i < 3; i++) {
        off64_t test_offset = (i + 1) * 512 * 100; // Every 100th block for testing
        if (test_offset >= (off64_t)d->fs) {
            continue; // Skip if beyond file size
        }

        uint8_t test_block[512];
        if (piscsi_dev_seek(d, test_offset, SEEK_SET) == (off64_t)-1) {
            printf("[PISCSI-SELFTEST] ERROR: Cannot seek to test block at offset %lld in %s\n",
                   (long long)test_offset, filename);
            return 0;
        }

        bytes_read = piscsi_dev_read(d, test_block, 512);
        if (bytes_read < 512) {
            printf("[PISCSI-SELFTEST] ERROR: Cannot read test block at offset %lld from %s (got %zd bytes)\n",
                   (long long)test_offset, filename, bytes_read);
            return 0;
        }
    }

    return 1; // All tests passed
}

void piscsi_unmap_drive(uint8_t index) {
    if (index < NUM_UNITS) {
        osd_led_piscsi_set_unit_present(index, 0);
    }
    if (devs[index].fd != -1) {
        DEBUG("[PISCSI] Unmapped drive %d.\n", index);
        piscsi_dev_close(&devs[index]);
        devs[index].fd = -1;
        devs[index].remote_sock = -1;
        devs[index].backend_type = PISCSI_BACKEND_NONE;
        devs[index].backend_ops = NULL;
        devs[index].backend_spec[0] = '\0';
        devs[index].configured_spec[0] = '\0';
        devs[index].media_kind = PISCSI_MEDIA_NONE;
        devs[index].read_only = 0;
        devs[index].block_size = 0;
    }
}

static __attribute__((unused)) const char *io_cmd_name(int index) {
    switch (index) {
        case CMD_INVALID: return "INVALID";
        case CMD_RESET: return "RESET";
        case CMD_READ: return "READ";
        case CMD_WRITE: return "WRITE";
        case CMD_UPDATE: return "UPDATE";
        case CMD_CLEAR: return "CLEAR";
        case CMD_STOP: return "STOP";
        case CMD_START: return "START";
        case CMD_FLUSH: return "FLUSH";
        case TD_MOTOR: return "TD_MOTOR";
        case TD_SEEK: return "SEEK";
        case TD_FORMAT: return "FORMAT";
        case TD_REMOVE: return "REMOVE";
        case TD_CHANGENUM: return "CHANGENUM";
        case TD_CHANGESTATE: return "CHANGESTATE";
        case TD_PROTSTATUS: return "PROTSTATUS";
        case TD_RAWREAD: return "RAWREAD";
        case TD_RAWWRITE: return "RAWWRITE";
        case TD_GETDRIVETYPE: return "GETDRIVETYPE";
        case TD_GETNUMTRACKS: return "GETNUMTRACKS";
        case TD_ADDCHANGEINT: return "ADDCHANGEINT";
        case TD_REMCHANGEINT: return "REMCHANGEINT";
        case TD_GETGEOMETRY: return "GETGEOMETRY";
        case TD_EJECT: return "EJECT";
        case TD_LASTCOMM: return "LASTCOMM/READ64";
        case TD_WRITE64: return "WRITE64";
        case HD_SCSICMD: return "HD_SCSICMD";
        case NSCMD_DEVICEQUERY: return "NSCMD_DEVICEQUERY";
        case NSCMD_TD_READ64: return "NSCMD_TD_READ64";
        case NSCMD_TD_WRITE64: return "NSCMD_TD_WRITE64";
        case NSCMD_TD_FORMAT64: return "NSCMD_TD_FORMAT64";

        default:
            return "[!!!PISCSI] Unhandled IO command";
    }
}

#define GETSCSINAME(a) case a: return ""#a"";
#define SCSIUNHANDLED(a) return "[!!!PISCSI] Unhandled SCSI command "#a"";

static __attribute__((unused)) const char *scsi_cmd_name(int index) {
    switch(index) {
        GETSCSINAME(SCSICMD_TEST_UNIT_READY);
        GETSCSINAME(SCSICMD_REQUEST_SENSE);
        GETSCSINAME(SCSICMD_FORMAT);
        GETSCSINAME(SCSICMD_INQUIRY);
        GETSCSINAME(SCSICMD_SEEK_6);
        GETSCSINAME(SCSICMD_VERIFY_6);
        GETSCSINAME(SCSICMD_READ_6);
        GETSCSINAME(SCSICMD_WRITE_6);
        GETSCSINAME(SCSICMD_START_STOP_UNIT);
        GETSCSINAME(SCSICMD_PREVENT_ALLOW_MEDIUM_REMOVAL);
        GETSCSINAME(SCSICMD_READ_10);
        GETSCSINAME(SCSICMD_WRITE_10);
        GETSCSINAME(SCSICMD_SEEK_10);
        GETSCSINAME(SCSICMD_VERIFY_10);
        GETSCSINAME(SCSICMD_SYNCHRONIZE_CACHE_10);
        GETSCSINAME(SCSICMD_READ_CAPACITY_10);
        GETSCSINAME(SCSICMD_MODE_SENSE_6);
        GETSCSINAME(SCSICMD_READ_DEFECT_DATA_10);
        default:
            return "[!!!PISCSI] Unhandled SCSI command";
    }
}

static __attribute__((unused)) void print_piscsi_debug_message(int index) {
    int32_t r = 0;

    switch (index) {
        case DBG_INIT:
            DEBUG("[PISCSI] Initializing devices.\n");
            break;
        case DBG_OPENDEV:
            if ((int)piscsi_dbg[0] != 255) {
                DEBUG("[PISCSI] Opening device %d (%d). Flags: %d (%.2X)\n", (int)piscsi_dbg[0], (int)piscsi_dbg[2], (int)piscsi_dbg[1], (int)piscsi_dbg[1]);
            }
            break;
        case DBG_CLEANUP:
            DEBUG("[PISCSI] Cleaning up.\n");
            break;
        case DBG_CHS:
            DEBUG("[PISCSI] C/H/S: %d / %d / %d\n", (int)piscsi_dbg[0], (int)piscsi_dbg[1], (int)piscsi_dbg[2]);
            break;
        case DBG_BEGINIO:
            DEBUG("[PISCSI] BeginIO: io_Command: %d (%s) - io_Flags = %d - quick: %d\n", (int)piscsi_dbg[0], io_cmd_name((int)piscsi_dbg[0]), (int)piscsi_dbg[1], (int)piscsi_dbg[2]);
            break;
        case DBG_ABORTIO:
            DEBUG("[PISCSI] AbortIO!\n");
            break;
        case DBG_SCSICMD:
            DEBUG("[PISCSI] SCSI Command %d (%s) unit=%d\n", (int)piscsi_dbg[1], scsi_cmd_name((int)piscsi_dbg[1]), (int)piscsi_dbg[5]);
            DEBUG("Len: %d - %.2X %.2X %.2X - Command Length: %d\n", (int)piscsi_dbg[0], (int)piscsi_dbg[1], (int)piscsi_dbg[2], (int)piscsi_dbg[3], (int)piscsi_dbg[4]);
            break;
        case DBG_SCSI_UNKNOWN_MODESENSE:
            DEBUG("[!!!PISCSI] SCSI: Unknown modesense %.4X\n", (int)piscsi_dbg[0]);
            break;
        case DBG_SCSI_UNKNOWN_COMMAND:
            DEBUG("[!!!PISCSI] SCSI: Unknown command opcode=0x%02X unit=%d\n", (int)(piscsi_dbg[0] & 0xFF), (int)piscsi_dbg[1]);
            break;
        case DBG_SCSIERR:
            DEBUG("[!!!PISCSI] SCSI: An error occured: %.4X\n", (int)piscsi_dbg[0]);
            break;
        case DBG_IOCMD:
            DEBUG_TRIVIAL("[PISCSI] IO Command %d (%s)\n", (int)piscsi_dbg[0], io_cmd_name((int)piscsi_dbg[0]));
            break;
        case DBG_IOCMD_UNHANDLED:
            DEBUG("[!!!PISCSI] WARN: IO command %.4X (%s) is unhandled by driver.\n", piscsi_dbg[0], io_cmd_name((int)piscsi_dbg[0]));
            break;
        case DBG_SCSI_FORMATDEVICE:
            DEBUG("[PISCSI] Get SCSI FormatDevice MODE SENSE.\n");
            break;
        case DBG_SCSI_RDG:
            DEBUG("[PISCSI] Get SCSI RDG MODE SENSE.\n");
            break;
        case DBG_SCSICMD_RW10:
#ifdef PISCSI_DEBUG
            r = get_mapped_item_by_address(cfg, piscsi_dbg[0]);
            struct SCSICmd_RW10 *rwdat = NULL;
            uint8_t data[10];
            if (r != -1) {
                uint32_t addr = (uint32_t)(piscsi_dbg[0] - cfg->map_offset[r]);
                rwdat = (struct SCSICmd_RW10 *)(&cfg->map_data[r][addr]);
            }
            else {
                DEBUG_TRIVIAL("[RW10] scsiData: %.8X\n", piscsi_dbg[0]);
                for (int i = 0; i < 10; i++) {
                    data[i] = read8((uint32_t)piscsi_dbg[0] + (uint32_t)i);
                }
                rwdat = (struct SCSICmd_RW10 *)data;
            }
            if (rwdat) {
                DEBUG_TRIVIAL("[RW10] CMD: %.2X\n", rwdat->opcode);
                DEBUG_TRIVIAL("[RW10] RDP: %.2X\n", rwdat->rdprotect_flags);
                DEBUG_TRIVIAL("[RW10] Block: %d (%d)\n", rwdat->block, BE(rwdat->block));
                DEBUG_TRIVIAL("[RW10] Res_Group: %.2X\n", rwdat->res_groupnum);
                DEBUG_TRIVIAL("[RW10] Len: %d (%d)\n", rwdat->len, BE16(rwdat->len));
            }
#endif
            break;
        case DBG_SCSI_DEBUG_MODESENSE_6:
            DEBUG_TRIVIAL("[PISCSI] SCSI ModeSense debug. Data: %.8X\n", piscsi_dbg[0]);
            r = get_mapped_item_by_address(cfg, piscsi_dbg[0]);
            if (r != -1) {
#ifdef PISCSI_DEBUG
                uint32_t addr = (uint32_t)(piscsi_dbg[0] - cfg->map_offset[r]);
                struct SCSICmd_ModeSense6 *sense = (struct SCSICmd_ModeSense6 *)(&cfg->map_data[r][addr]);
                DEBUG_TRIVIAL("[SenseData] CMD: %.2X\n", sense->opcode);
                DEBUG_TRIVIAL("[SenseData] DBD: %d\n", sense->reserved_dbd & 0x04);
                DEBUG_TRIVIAL("[SenseData] PC: %d\n", (sense->pc_pagecode & 0xC0 >> 6));
                DEBUG_TRIVIAL("[SenseData] PageCodes: %.2X %.2X\n", (sense->pc_pagecode & 0x3F), sense->subpage_code);
                DEBUG_TRIVIAL("[SenseData] AllocLen: %d\n", sense->alloc_len);
                DEBUG_TRIVIAL("[SenseData] Control: %.2X (%d)\n", sense->control, sense->control);
#endif
            }
            else {
                DEBUG("[!!!PISCSI] ModeSense data not immediately available.\n");
            }
            break;
        default:
            DEBUG("[!!!PISCSI] No debug message available for index %d.\n", index);
            break;
    }
}

#define DEBUGME_SIMPLE(i, s) case i: DEBUG(s); break;

static void piscsi_debugme(uint32_t index) {
        if (index != last_debugme_idx) {
        last_debugme_idx = index;
        if (index >= 30 && index <= 41) {
            #ifdef PISCSI_DEBUG
            piscsi_dump_cpu_state("DEBUGME step");
            #endif
        }
    }
    switch (index) {
        DEBUGME_SIMPLE(1, "[PISCSI-DEBUGME] Arrived at DiagEntry.\n");
        DEBUGME_SIMPLE(2, "[PISCSI-DEBUGME] Arrived at BootEntry, for some reason.\n");
        DEBUGME_SIMPLE(3, "[PISCSI-DEBUGME] Init: Interrupt disable.\n");
        DEBUGME_SIMPLE(4, "[PISCSI-DEBUGME] Init: Copy/reloc driver.\n");
        DEBUGME_SIMPLE(5, "[PISCSI-DEBUGME] Init: InitResident.\n");
        DEBUGME_SIMPLE(7, "[PISCSI-DEBUGME] Init: Begin partition loop.\n");
        DEBUGME_SIMPLE(8, "[PISCSI-DEBUGME] Init: Partition loop done. Cleaning up and returning to Exec.\n");
        DEBUGME_SIMPLE(9, "[PISCSI-DEBUGME] Init: Load file systems.\n");
        DEBUGME_SIMPLE(10, "[PISCSI-DEBUGME] Init: AllocMem for resident.\n");
        DEBUGME_SIMPLE(11, "[PISCSI-DEBUGME] Init: Checking if resident is loaded.\n");
        DEBUGME_SIMPLE(22, "[PISCSI-DEBUGME] Arrived at BootEntry.\n");
        case 30:
            DEBUG("[PISCSI-DEBUGME] LoadFileSystems: Opening FileSystem.resource.\n");
            rom_cur_fs = 0;
            break;
        DEBUGME_SIMPLE(33, "[PISCSI-DEBUGME] FileSystem.resource not available, creating.\n");
        case 31:
            DEBUG("[PISCSI-DEBUGME] OpenResource result: %d\n", piscsi_u32[0]);
            break;
        case 32:
            DEBUG("[PISCSI-DEBUGME] DEBUGME 32 marker.\n");
#ifdef PISCSI_DEBUG
            piscsi_dump_cpu_state("DEBUGME 32");
#endif
            break;
        case 35:
            DEBUG("[PISCSI-DEBUGME] stuff output\n");
            break;
        case 36:
            DEBUG("[PISCSI-DEBUGME] Debug pointers: %.8X %.8X %.8X %.8X\n", piscsi_u32[0], piscsi_u32[1], piscsi_u32[2], piscsi_u32[3]);
            break;
        default:
            // Handle undefined indexes by printing the index number
            DEBUG("[PISCSI-DEBUGME] idx=%u (no string)\n", index);
            break;
    }

    if (index == 8) {
        stop_cpu_emulation(1);
    }
}

void handle_piscsi_write(uint32_t addr, uint32_t val, uint8_t type) {
    uint8_t *map;
#ifndef PISCSI_DEBUG
    if (type) {}
#endif

    struct piscsi_dev *d = &devs[piscsi_cur_drive];

    uint16_t cmd = (addr & 0xFFFF);

    switch (cmd) {
        case PISCSI_CMD_READ64:
        case PISCSI_CMD_READ:
        case PISCSI_CMD_READBYTES:
            osd_led_piscsi_host_pulse();
            d = &devs[val];
            if (d->fd == -1) {
                DEBUG("[!!!PISCSI] BUG: Attempted read from unmapped drive %d.\n", val);
                break;
            }
            if ((int)val >= 0 && (int)val < NUM_UNITS) {
                osd_led_piscsi_unit_pulse_read((int)val);
            }

            if (cmd == PISCSI_CMD_READBYTES) {
                uint32_t src = piscsi_u32[0];
                uint32_t block = src / d->block_size;
                d->lba = block;
                DEBUG("[PISCSI-IO] Unit:%d CMD:READBYTES io_Offset:0x%X io_Length:%d LBA:0x%X file_offset:0x%X to_addr:0x%.8X\n", val, src, piscsi_u32[1], block, src, piscsi_u32[2]);
                piscsi_dev_seek(d, (off64_t)src, SEEK_SET);
            }
            else if (cmd == PISCSI_CMD_READ) {
                uint32_t block = piscsi_u32[0];
                uint64_t file_offset = (uint64_t)block * d->block_size;
                d->lba = block;
                DEBUG("[PISCSI-IO] Unit:%d CMD:READ io_Offset:0x%X io_Length:%d LBA:0x%X file_offset:0x%llX to_addr:0x%.8X\n", val, block, piscsi_u32[1], block, (unsigned long long)file_offset, piscsi_u32[2]);
                piscsi_dev_seek(d, (off64_t)file_offset, SEEK_SET);
            }
            else {
                uint64_t src = ((uint64_t)piscsi_u32[3] << 32) | piscsi_u32[0];
                uint32_t block = (uint32_t)(src / d->block_size);
                d->lba = block;
                DEBUG("[PISCSI-IO] Unit:%d CMD:READ64 io_Offset:0x%llX io_Length:%d LBA:0x%X file_offset:0x%llX to_addr:0x%.8X\n", val, (unsigned long long)src, piscsi_u32[1], block, (unsigned long long)src, piscsi_u32[2]);
                piscsi_dev_seek(d, (off64_t)src, SEEK_SET);
            }

            uint32_t avail = 0;
            map = NULL;
            int map_rc_read = piscsi_get_map_bounds(cfg, piscsi_u32[2], piscsi_u32[1], &map, &avail);
            if (map_rc_read == 0) {
#ifdef PISCSI_DEBUG
                int32_t debug_r = get_mapped_item_by_address(cfg, piscsi_u32[2]);
                DEBUG_TRIVIAL("[PISCSI-%d] \"DMA\" Read goes to mapped range %d.\n", val, debug_r);
#endif
                uint8_t *dma_buf = NULL;
                uint32_t dma_size = 0;
                int32_t dma_idx = -1;
                if (piscsi_get_dma_window(cfg, &dma_buf, &dma_size, &dma_idx) == 0 &&
                    get_mapped_item_by_address(cfg, piscsi_u32[2]) == dma_idx) {
#ifdef PISCSI_DEBUG
                    DEBUG_TRIVIAL("[PISCSI-%d] Using piscsi_dma window map %d (size=%u) for READ.\n",
                                  val, dma_idx, dma_size);
#endif
                    uint32_t remaining = piscsi_u32[1];
                    uint32_t dst_addr = piscsi_u32[2];
                    ssize_t total_read = 0;
                    int success = 1;
                    while (remaining) {
                        uint32_t chunk = remaining < dma_size ? remaining : dma_size;
                        uint8_t *dst = NULL;
                        uint32_t dst_avail = 0;
                        int rc = piscsi_get_map_bounds(cfg, dst_addr, chunk, &dst, &dst_avail);
                        if (rc != 0) {
                            DEBUG("[PISCSI-IO-ERROR] Unit:%d READ refused: DMA range overflow at 0x%08X len=%u\n",
                                  val, dst_addr, chunk);
                            success = 0;
                            break;
                        }
                        ssize_t bytes_read = piscsi_dev_read(d, dma_buf, chunk);
                        if (bytes_read < 0) {
                            DEBUG("[PISCSI-IO-ERROR] Unit:%d READ failed: bytes_requested=%u, bytes_read=%zd, errno=%d\n",
                                  val, chunk, bytes_read, errno);
                            success = 0;
                            break;
                        }
                        memcpy(dst, dma_buf, (size_t)bytes_read);
                        total_read += bytes_read;
                        if ((uint32_t)bytes_read != chunk) {
                            DEBUG("[PISCSI-IO-WARN] Unit:%d PARTIAL READ: requested=%u, actual=%zd\n",
                                  val, chunk, bytes_read);
                            break;
                        }
                        remaining -= chunk;
                        dst_addr += chunk;
                    }
                    if (success) {
                        DEBUG("[PISCSI-IO-SUCCESS] Unit:%d READ: %zd bytes OK\n", val, total_read);
                    }
                } else {
                    ssize_t bytes_read = piscsi_dev_read(d, map, piscsi_u32[1]);
                    if (bytes_read < 0) {
                        DEBUG("[PISCSI-IO-ERROR] Unit:%d READ failed: bytes_requested=%d, bytes_read=%zd, errno=%d\n", val, piscsi_u32[1], bytes_read, errno);
                    } else if (bytes_read != (ssize_t)piscsi_u32[1]) {
                        DEBUG("[PISCSI-IO-WARN] Unit:%d PARTIAL READ: requested=%d, actual=%zd\n", val, piscsi_u32[1], bytes_read);
                    } else {
                        DEBUG("[PISCSI-IO-SUCCESS] Unit:%d READ: %zd bytes OK\n", val, bytes_read);
                    }
                }
            }
            else if (map_rc_read == -2) {
                DEBUG("[PISCSI-IO-ERROR] Unit:%d READ refused: DMA range overflow at 0x%08X len=%u\n",
                      val, piscsi_u32[2], piscsi_u32[1]);
            }
            else {
                DEBUG_TRIVIAL("[PISCSI-%d] No mapped range found for read.\n", val);
                uint8_t pio_buf[PISCSI_PIO_FALLBACK_CHUNK];
                int success = 1;
                uint32_t remaining = piscsi_u32[1];
                uint32_t dst_addr = piscsi_u32[2];
                size_t total_read = 0;
                while (remaining) {
                    uint32_t chunk = remaining < PISCSI_PIO_FALLBACK_CHUNK
                                         ? remaining
                                         : PISCSI_PIO_FALLBACK_CHUNK;
                    ssize_t result = piscsi_dev_read(d, pio_buf, chunk);
                    if (result <= 0) {
                        DEBUG("[PISCSI-IO-ERROR] Unit:%d PIO READ failed at addr 0x%08X: result=%zd\n",
                              val, dst_addr, result);
                        success = 0;
                        break;
                    }
                    for (size_t i = 0; i < (size_t)result; i++) {
                        m68k_write_memory_8(dst_addr + (uint32_t)i, pio_buf[i]);
                    }
                    total_read += (size_t)result;
                    dst_addr += (uint32_t)result;
                    remaining -= (uint32_t)result;
                    if ((uint32_t)result != chunk) {
                        DEBUG("[PISCSI-IO-WARN] Unit:%d PARTIAL PIO READ: requested=%u, actual=%zd\n",
                              val, chunk, result);
                        break;
                    }
                }
                if (success) {
                    DEBUG("[PISCSI-IO-SUCCESS] Unit:%d PIO READ: %zu bytes OK\n", val, total_read);
                }
            }
            break;
        case PISCSI_CMD_WRITE64:
        case PISCSI_CMD_WRITE:
        case PISCSI_CMD_WRITEBYTES:
            osd_led_piscsi_host_pulse();
            d = &devs[val];
            if (d->fd == -1) {
                DEBUG ("[PISCSI] BUG: Attempted write to unmapped drive %d.\n", val);
                break;
            }
            if (d->read_only) {
                DEBUG("[PISCSI] Refusing write to read-only drive %d.\n", val);
                break;
            }
            if ((int)val >= 0 && (int)val < NUM_UNITS) {
                osd_led_piscsi_unit_pulse_write((int)val);
            }

            if (cmd == PISCSI_CMD_WRITEBYTES) {
                uint32_t src = piscsi_u32[0];
                uint32_t block = src / d->block_size;
                d->lba = block;
                DEBUG("[PISCSI-IO] Unit:%d CMD:WRITEBYTES io_Offset:0x%X io_Length:%d LBA:0x%X file_offset:0x%X from_addr:0x%.8X\n", val, src, piscsi_u32[1], block, src, piscsi_u32[2]);
                piscsi_dev_seek(d, (off64_t)src, SEEK_SET);
            }
            else if (cmd == PISCSI_CMD_WRITE) {
                uint32_t block = piscsi_u32[0];
                uint64_t file_offset = (uint64_t)block * d->block_size;
                d->lba = block;
                DEBUG("[PISCSI-IO] Unit:%d CMD:WRITE io_Offset:0x%X io_Length:%d LBA:0x%X file_offset:0x%llX from_addr:0x%.8X\n", val, block, piscsi_u32[1], block, (unsigned long long)file_offset, piscsi_u32[2]);
                piscsi_dev_seek(d, (off64_t)file_offset, SEEK_SET);
            }
            else {
                uint64_t src = ((uint64_t)piscsi_u32[3] << 32) | piscsi_u32[0];
                uint32_t block = (uint32_t)(src / d->block_size);
                d->lba = block;
                DEBUG("[PISCSI-IO] Unit:%d CMD:WRITE64 io_Offset:0x%llX io_Length:%d LBA:0x%X file_offset:0x%llX from_addr:0x%.8X\n", val, (unsigned long long)src, piscsi_u32[1], block, (unsigned long long)src, piscsi_u32[2]);
                piscsi_dev_seek(d, (off64_t)src, SEEK_SET);
            }

            uint32_t avail_w = 0;
            map = NULL;
            int map_rc_write = piscsi_get_map_bounds(cfg, piscsi_u32[2], piscsi_u32[1], &map, &avail_w);
            if (map_rc_write == 0) {
#ifdef PISCSI_DEBUG
                int32_t debug_r = get_mapped_item_by_address(cfg, piscsi_u32[2]);
                DEBUG_TRIVIAL("[PISCSI-%d] \"DMA\" Write comes from mapped range %d.\n", val, debug_r);
#endif
                uint8_t *dma_buf = NULL;
                uint32_t dma_size = 0;
                int32_t dma_idx = -1;
                if (piscsi_get_dma_window(cfg, &dma_buf, &dma_size, &dma_idx) == 0 &&
                    get_mapped_item_by_address(cfg, piscsi_u32[2]) == dma_idx) {
#ifdef PISCSI_DEBUG
                    DEBUG_TRIVIAL("[PISCSI-%d] Using piscsi_dma window map %d (size=%u) for WRITE.\n",
                                  val, dma_idx, dma_size);
#endif
                    uint32_t remaining = piscsi_u32[1];
                    uint32_t src_addr = piscsi_u32[2];
                    ssize_t total_written = 0;
                    int success = 1;
                    while (remaining) {
                        uint32_t chunk = remaining < dma_size ? remaining : dma_size;
                        uint8_t *src_ptr = NULL;
                        uint32_t src_avail = 0;
                        int rc = piscsi_get_map_bounds(cfg, src_addr, chunk, &src_ptr, &src_avail);
                        if (rc != 0) {
                            DEBUG("[PISCSI-IO-ERROR] Unit:%d WRITE refused: DMA range overflow at 0x%08X len=%u\n",
                                  val, src_addr, chunk);
                            success = 0;
                            break;
                        }
                        memcpy(dma_buf, src_ptr, chunk);
                        ssize_t bytes_written = piscsi_dev_write(d, dma_buf, chunk);
                        if (bytes_written < 0) {
                            DEBUG("[PISCSI-IO-ERROR] Unit:%d WRITE failed: bytes_requested=%u, bytes_written=%zd, errno=%d\n",
                                  val, chunk, bytes_written, errno);
                            success = 0;
                            break;
                        }
                        total_written += bytes_written;
                        if ((uint32_t)bytes_written != chunk) {
                            DEBUG("[PISCSI-IO-WARN] Unit:%d PARTIAL WRITE: requested=%u, actual=%zd\n",
                                  val, chunk, bytes_written);
                            break;
                        }
                        remaining -= chunk;
                        src_addr += chunk;
                    }
                    if (success) {
                        DEBUG("[PISCSI-IO-SUCCESS] Unit:%d WRITE: %zd bytes OK\n", val, total_written);
                    }
                } else {
                    ssize_t bytes_written = piscsi_dev_write(d, map, piscsi_u32[1]);
                    if (bytes_written < 0) {
                        DEBUG("[PISCSI-IO-ERROR] Unit:%d WRITE failed: bytes_requested=%d, bytes_written=%zd, errno=%d\n", val, piscsi_u32[1], bytes_written, errno);
                    } else if (bytes_written != (ssize_t)piscsi_u32[1]) {
                        DEBUG("[PISCSI-IO-WARN] Unit:%d PARTIAL WRITE: requested=%d, actual=%zd\n", val, piscsi_u32[1], bytes_written);
                    } else {
                        DEBUG("[PISCSI-IO-SUCCESS] Unit:%d WRITE: %zd bytes OK\n", val, bytes_written);
                    }
                }
            }
            else if (map_rc_write == -2) {
                DEBUG("[PISCSI-IO-ERROR] Unit:%d WRITE refused: DMA range overflow at 0x%08X len=%u\n",
                      val, piscsi_u32[2], piscsi_u32[1]);
            }
            else {
                DEBUG_TRIVIAL("[PISCSI-%d] No mapped range found for write.\n", val);
                uint8_t pio_buf[PISCSI_PIO_FALLBACK_CHUNK];
                int success = 1;
                uint32_t remaining = piscsi_u32[1];
                uint32_t src_addr = piscsi_u32[2];
                size_t total_written = 0;
                while (remaining) {
                    uint32_t chunk = remaining < PISCSI_PIO_FALLBACK_CHUNK
                                         ? remaining
                                         : PISCSI_PIO_FALLBACK_CHUNK;
                    for (uint32_t i = 0; i < chunk; i++) {
                        pio_buf[i] = (uint8_t)m68k_read_memory_8(src_addr + i);
                    }
                    ssize_t result = piscsi_dev_write(d, pio_buf, chunk);
                    if (result <= 0) {
                        DEBUG("[PISCSI-IO-ERROR] Unit:%d PIO WRITE failed at addr 0x%08X: result=%zd\n",
                              val, src_addr, result);
                        success = 0;
                        break;
                    }
                    total_written += (size_t)result;
                    src_addr += (uint32_t)result;
                    remaining -= (uint32_t)result;
                    if ((uint32_t)result != chunk) {
                        DEBUG("[PISCSI-IO-WARN] Unit:%d PARTIAL PIO WRITE: requested=%u, actual=%zd\n",
                              val, chunk, result);
                        break;
                    }
                }
                if (success) {
                    DEBUG("[PISCSI-IO-SUCCESS] Unit:%d PIO WRITE: %zu bytes OK\n", val, total_written);
                }
            }
            break;
        case PISCSI_CMD_ADDR1: case PISCSI_CMD_ADDR2: case PISCSI_CMD_ADDR3: case PISCSI_CMD_ADDR4: {
            int addr_idx = ((addr & 0xFFFF) - PISCSI_CMD_ADDR1) / 4;
            piscsi_u32[addr_idx] = val;
            break;
        }
        case PISCSI_CMD_DRVNUM:
            if (val > 6) {
                piscsi_cur_drive = 255;
            }
            else {
                piscsi_cur_drive = (uint8_t)val;
            }
            if (piscsi_cur_drive != 255) {
                DEBUG("[PISCSI] (%s) Drive number set to %d (%d)\n", op_type_names[type], piscsi_cur_drive, val);
            }
            break;
        case PISCSI_CMD_DRVNUMX:
            piscsi_cur_drive = (uint8_t)val;
            DEBUG("[PISCSI] DRVNUMX: %d.\n", val);
            break;
        case PISCSI_CMD_DEBUGME:
            piscsi_debugme(val);
            break;
        case PISCSI_CMD_DRIVER:
            osd_led_piscsi_host_pulse();
            DEBUG("[PISCSI] Driver copy/patch called, destination address %.8X.\n", val);
            int32_t driver_r = get_mapped_item_by_address(cfg, val);
            if (driver_r != -1) {
                uint32_t driver_base_addr = (uint32_t)(val - cfg->map_offset[driver_r]);
                uint8_t *dst_data = cfg->map_data[driver_r];
                uint8_t cur_partition = 0;
                memcpy(dst_data + driver_base_addr, piscsi_rom_ptr + PISCSI_DRIVER_OFFSET, 0x4000 - PISCSI_DRIVER_OFFSET);

                piscsi_hinfo.base_offset = val;

                if (reloc_hunks(piscsi_hreloc, dst_data + driver_base_addr, &piscsi_hinfo) != 0) {
                    LOG_ERROR("[PISCSI] Driver relocation failed; aborting handler install\n");
                    break;
                }

                #define PUTNODELONG(val) do { uint32_t temp = htobe32(val); memcpy(&dst_data[p_offs], &temp, sizeof(temp)); p_offs += 4; } while(0)
                #define PUTNODELONGBE(val) do { uint32_t temp = val; memcpy(&dst_data[p_offs], &temp, sizeof(temp)); p_offs += 4; } while(0)

                for (int i = 0; i < 128; i++) {
                    rom_partitions[i] = 0;
                    rom_partition_prio[i] = 0;
                    rom_partition_dostype[i] = 0;
                }
                rom_cur_partition = 0;

                uint32_t driver_data_addr = driver_base_addr + 0x3F00;
                sprintf((char *)dst_data + driver_data_addr, "pi-scsi.device");
                uint32_t driver_addr2 = driver_base_addr + 0x4000;
                for (int i = 0; i < NUM_UNITS; i++) {
                    if (devs[i].fd == -1)
                        goto skip_disk;

                    if (devs[i].num_partitions) {
                        uint32_t p_offs = driver_addr2;
                        DEBUG("[PISCSI] Adding %d partitions for unit %d\n", devs[i].num_partitions, i);
                        for (uint32_t j = 0; j < devs[i].num_partitions; j++) {
                            DEBUG("Partition %d: %s\n", j, devs[i].pb[j]->pb_DriveName + 1);
                            sprintf((char *)dst_data + p_offs, "%s", devs[i].pb[j]->pb_DriveName + 1);
                            p_offs += 0x20;
                            PUTNODELONG(driver_addr2 + cfg->map_offset[driver_r]);
                            PUTNODELONG(driver_data_addr + cfg->map_offset[driver_r]);
                            PUTNODELONG(i);
                            PUTNODELONG(0);
                            uint32_t nodesize = (be32toh(devs[i].pb[j]->pb_Environment[0]) + 1) * 4;
                            memcpy(dst_data + p_offs, devs[i].pb[j]->pb_Environment, nodesize);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
                            /*
                             * Cast alignment warning intentionally suppressed:
                             * PiSCSI interprets Amiga-side structures that arrive as byte streams.
                             * On the Pi side these are represented as C structs with 32-bit alignment.
                             * The external contract guarantees these blocks are properly aligned as expected by the Amiga.
                             */
                            struct pihd_dosnode_data *dat = (struct pihd_dosnode_data *)((char *)(&dst_data[driver_addr2+0x20]));
#pragma GCC diagnostic pop

                            if (piscsi_force_24bit_dma()) {
                                uint32_t mem_type = BE(dat->mem_type);
                                uint32_t maxtransfer = BE(dat->maxtransfer);
                                uint32_t transfer_mask = BE(dat->transfer_mask);

                                mem_type |= (PISCSI_MEMF_PUBLIC_HOST | PISCSI_MEMF_24BITDMA_HOST);
                                if (maxtransfer == 0 || maxtransfer > PISCSI_24BIT_MAXTRANSFER) {
                                    maxtransfer = PISCSI_24BIT_MAXTRANSFER;
                                }
                                if (piscsi_force_24bit_dma_strict_mask()) {
                                    if (transfer_mask == 0 || transfer_mask > PISCSI_24BIT_ADDR_MASK) {
                                        /* Keep bit0 clear like classic DMA masks. */
                                        transfer_mask = (PISCSI_24BIT_ADDR_MASK & ~1u);
                                    }
                                }

                                dat->mem_type = htobe32(mem_type);
                                dat->maxtransfer = htobe32(maxtransfer);
                                if (piscsi_force_24bit_dma_strict_mask()) {
                                    dat->transfer_mask = htobe32(transfer_mask);
                                }
                            }

                            if (BE(devs[i].pb[j]->pb_Flags) & 0x01) {
                                DEBUG("Partition is bootable.\n");
                                rom_partition_prio[cur_partition] = BE(dat->priority);
                            }
                            else {
                                DEBUG("Partition is not bootable.\n");
                                rom_partition_prio[cur_partition] = (uint32_t)-128;
                            }

                            DEBUG("DOSNode Data:\n");
                            DEBUG("Name: %s Device: %s\n", dst_data + driver_addr2, dst_data + driver_data_addr);
                            DEBUG("Unit: %d Flags: %d Pad1: %d\n", BE(dat->unit), BE(dat->flags), BE(dat->pad1));
                            DEBUG("Node len: %d Block len: %d\n", BE(dat->node_len) * 4, BE(dat->block_len) * 4);
                            DEBUG("H: %d SPB: %d BPS: %d\n", BE(dat->surf), BE(dat->secs_per_block), BE(dat->blocks_per_track));
                            DEBUG("Reserved: %d Prealloc: %d\n", BE(dat->reserved_blocks), BE(dat->pad2));
                            DEBUG("Interleaved: %d Buffers: %d Memtype: %d\n", BE(dat->interleave), BE(dat->buffers), BE(dat->mem_type));
                            DEBUG("Lowcyl: %d Highcyl: %d Prio: %d\n", BE(dat->lowcyl), BE(dat->highcyl), BE(dat->priority));
                            DEBUG("Maxtransfer: %.8X Mask: %.8X\n", BE(dat->maxtransfer), BE(dat->transfer_mask));
                            DEBUG("DOSType: %.8X\n", BE(dat->dostype));

                            rom_partitions[cur_partition] = (uint32_t)(driver_addr2 + 0x20 + cfg->map_offset[driver_r]);
                            rom_partition_dostype[cur_partition] = dat->dostype;
                            cur_partition++;
                            driver_addr2 += 0x100;
                            p_offs = driver_addr2;
                        }
                    }
skip_disk:;
                }
            }

            break;
        case PISCSI_CMD_NEXTPART:
            osd_led_piscsi_host_pulse();
            DEBUG("[PISCSI] Switch partition %d -> %d\n", rom_cur_partition, rom_cur_partition + 1);
            rom_cur_partition++;
            break;
        case PISCSI_CMD_NEXTFS:
            osd_led_piscsi_host_pulse();
            DEBUG("[PISCSI] Switch file file system %d -> %d\n", rom_cur_fs, rom_cur_fs + 1);
            rom_cur_fs++;
            break;
        case PISCSI_CMD_COPYFS:
            osd_led_piscsi_host_pulse();
            DEBUG("[PISCSI] Copy file system %d to %.8X and reloc.\n", rom_cur_fs, piscsi_u32[2]);
            int32_t copy_r = get_mapped_item_by_address(cfg, piscsi_u32[2]);
            if (copy_r != -1) {
                uint32_t copy_base_addr = (uint32_t)(piscsi_u32[2] - cfg->map_offset[copy_r]);
                memcpy(cfg->map_data[copy_r] + copy_base_addr, filesystems[rom_cur_fs].binary_data, filesystems[rom_cur_fs].h_info.byte_size);
                filesystems[rom_cur_fs].h_info.base_offset = piscsi_u32[2];
                if (reloc_hunks(filesystems[rom_cur_fs].relocs, cfg->map_data[copy_r] + copy_base_addr,
                                &filesystems[rom_cur_fs].h_info) != 0) {
                    char *dosID = (char *)&filesystems[rom_cur_fs].FS_ID;
                    LOG_ERROR("[PISCSI] Rejecting filesystem %c%c%c/%d: relocation failed\n",
                              dosID[0], dosID[1], dosID[2], dosID[3]);
                    filesystems[rom_cur_fs].handler = 0;
                    filesystems[rom_cur_fs].valid = 0;
                    break;
                }
                filesystems[rom_cur_fs].handler = piscsi_u32[2];
                filesystems[rom_cur_fs].valid = 1;
                {
                    char *dosID = (char *)&filesystems[rom_cur_fs].FS_ID;
                    if (!fs_handler_valid(&filesystems[rom_cur_fs], filesystems[rom_cur_fs].handler,
                                          (uint8_t)rom_cur_partition, dosID)) {
                        filesystems[rom_cur_fs].handler = 0;
                        filesystems[rom_cur_fs].valid = 0;
                    }
                }
            }
            break;
        case PISCSI_CMD_SETFSH: {
            osd_led_piscsi_host_pulse();
            int fs_idx = 0;
            DEBUG("[PISCSI] Set handler for partition %d (DeviceNode: %.8X)\n", rom_cur_partition, val);
            int32_t setfsh_r = get_mapped_item_by_address(cfg, val);
            if (setfsh_r != -1) {
                uint32_t setfsh_base_addr = (uint32_t)(val - cfg->map_offset[setfsh_r]);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
                /*
                 * Cast alignment warning intentionally suppressed:
                 * PiSCSI interprets Amiga-side structures that arrive as byte streams.
                 * On the Pi side these are represented as C structs with 32-bit alignment.
                 * The external contract guarantees these blocks are properly aligned as expected by the Amiga.
                 */
                struct DeviceNode *node = (struct DeviceNode *)((char *)(cfg->map_data[setfsh_r] + setfsh_base_addr));
#pragma GCC diagnostic pop
                char *dosID = (char *)&rom_partition_dostype[rom_cur_partition];

                DEBUG("[PISCSI] Partition DOSType is %c%c%c/%d\n", dosID[0], dosID[1], dosID[2], dosID[3]);
                // First try exact match
                for (fs_idx = 0; fs_idx < piscsi_num_fs; fs_idx++) {
                    if (rom_partition_dostype[rom_cur_partition] == filesystems[fs_idx].FS_ID) {
                        if (fs_handler_valid(&filesystems[fs_idx], filesystems[fs_idx].handler,
                                             (uint8_t)rom_cur_partition, dosID)) {
                            node->dn_SegList = htobe32((((filesystems[fs_idx].handler) +
                                                         filesystems[fs_idx].h_info.header_size) >>
                                                        2));
                            node->dn_GlobalVec = 0xFFFFFFFF;
                            goto fs_found;
                        }
                        LOG_ERROR("[PISCSI] Handler rejected for %c%c%c/%d (partition %u)\n",
                                  dosID[0], dosID[1], dosID[2], dosID[3], rom_cur_partition);
                        goto fs_not_found;
                    }
                }

                // If no exact match, try fallback mappings (e.g., DOS/3 -> DOS/1 for FastFileSystem)
                uint32_t fallback_dostype = rom_partition_dostype[rom_cur_partition];

                // Map DOS/3 (FFS International) to DOS/1 (FFS) handler since they use the same filesystem
                if (fallback_dostype == 0x444F5303) { // DOS/3
                    fallback_dostype = 0x444F5301;   // DOS/1
                    for (fs_idx = 0; fs_idx < piscsi_num_fs; fs_idx++) {
                        if (fallback_dostype == filesystems[fs_idx].FS_ID) {
                            if (fs_handler_valid(&filesystems[fs_idx], filesystems[fs_idx].handler,
                                                 (uint8_t)rom_cur_partition, dosID)) {
                                node->dn_SegList = htobe32((((filesystems[fs_idx].handler) +
                                                             filesystems[fs_idx].h_info.header_size) >>
                                                            2));
                                node->dn_GlobalVec = 0xFFFFFFFF;
                                DEBUG("[PISCSI] Fallback: Mapped DOS/3 partition to DOS/1 filesystem handler.\n");
                                goto fs_found;
                            }
                            LOG_ERROR("[PISCSI] Handler rejected for %c%c%c/%d (partition %u)\n",
                                      dosID[0], dosID[1], dosID[2], dosID[3], rom_cur_partition);
                            goto fs_not_found;
                        }
                    }
                }

                node->dn_GlobalVec = 0xFFFFFFFF;
                node->dn_SegList = 0;
fs_not_found:
                printf("[!!!PISCSI] Found no valid handler for file system %c%c%c/%d\n", dosID[0], dosID[1], dosID[2], dosID[3]);
fs_found:;
                DEBUG("[FS-HANDLER] Next: %d Type: %.8X\n", BE(node->dn_Next), BE(node->dn_Type));
                DEBUG("[FS-HANDLER] Task: %d Lock: %d\n", BE(node->dn_Task), BE(node->dn_Lock));
                DEBUG("[FS-HANDLER] Handler: %d Stacksize: %d\n", BE(node->dn_Handler), BE(node->dn_StackSize));
                DEBUG("[FS-HANDLER] Priority: %d Startup: %d (%.8X)\n", BE(node->dn_Priority), BE(node->dn_Startup), BE(node->dn_Startup));
                DEBUG("[FS-HANDLER] SegList: %.8X GlobalVec: %d\n", BE((uint32_t)node->dn_SegList), (int)BE(node->dn_GlobalVec));
                DEBUG("[PISCSI] Handler for partition %.8X set to %.8X (%.8X).\n", (uint32_t)BE(node->dn_Name), filesystems[fs_idx].FS_ID, filesystems[fs_idx].handler);
            }
            break;
        }
        case PISCSI_CMD_LOADFS: {
            osd_led_piscsi_host_pulse();
            DEBUG("[PISCSI] Attempt to load file system for partition %d from disk.\n", rom_cur_partition);
            int32_t mapped_r = get_mapped_item_by_address(cfg, val);
            if (mapped_r != -1) {
                char dosID[4];
                char dosID_str[16];
                memset(dosID_str, 0x00, sizeof(dosID_str));
                uint32_t raw_dostype = rom_partition_dostype[rom_cur_partition];
                if (amiga_fsid_build_dosid(raw_dostype, dosID, dosID_str, sizeof(dosID_str)) != 0) {
                    printf("[FSHD-Late] No mapping for DOSType 0x%08X\n", be32toh(raw_dostype));
                    piscsi_u32[3] = 0xFFFFFFFF;
                    break;
                }
                filesystems[piscsi_num_fs].binary_data = NULL;
                filesystems[piscsi_num_fs].fhb = NULL;
                filesystems[piscsi_num_fs].FS_ID = raw_dostype;
                filesystems[piscsi_num_fs].handler = 0;
                filesystems[piscsi_num_fs].valid = 0;
                if (load_fs(&filesystems[piscsi_num_fs], dosID) != -1) {
                    printf("[FSHD-Late] Loaded file system %s from fs storage.\n", dosID_str);
                    piscsi_u32[3] = piscsi_num_fs;
                    rom_cur_fs = piscsi_num_fs;
                    filesystems[piscsi_num_fs].valid = 1;
                    piscsi_num_fs++;
                } else {
                    printf("[FSHD-Late] Failed to load file system %s from fs storage.\n", dosID_str);
                    piscsi_u32[3] = 0xFFFFFFFF;
                }
            }
            break;
        }
        case PISCSI_DBG_VAL1: case PISCSI_DBG_VAL2: case PISCSI_DBG_VAL3: case PISCSI_DBG_VAL4:
        case PISCSI_DBG_VAL5: case PISCSI_DBG_VAL6: case PISCSI_DBG_VAL7: case PISCSI_DBG_VAL8: {
            int i = ((addr & 0xFFFF) - PISCSI_DBG_VAL1) / 4;
            piscsi_dbg[i] = val;
            break;
        }
        case PISCSI_DBG_MSG:
#ifdef PISCSI_DEBUG
            print_piscsi_debug_message((int)val);
#endif
            break;
        default:
            DEBUG("[!!!PISCSI] WARN: Unhandled %s register write to %.8X: %d\n", op_type_names[type], addr, (int)val);
            break;
    }
}

#define PIB 0x00

uint32_t handle_piscsi_read(uint32_t addr, uint8_t type) {
    if (type) {}

    if ((addr & 0xFFFF) >= PISCSI_CMD_ROM) {
        uint32_t romoffs = (addr & 0xFFFF) - PISCSI_CMD_ROM;
        if (romoffs < (piscsi_rom_size + PIB)) {
            //DEBUG("[PISCSI] %s read from Boot ROM @$%.4X (%.8X): ", op_type_names[type], romoffs, addr);
            uint32_t v = 0;
            switch (type) {
                case OP_TYPE_BYTE:
                    v = piscsi_rom_ptr[romoffs - PIB];
                    //DEBUG("%.2X\n", v);
                    break;
                case OP_TYPE_WORD: {
                    uint16_t temp_val;
                    memcpy(&temp_val, &piscsi_rom_ptr[romoffs - PIB], sizeof(temp_val));
                    v = be16toh(temp_val);
                    //DEBUG("%.4X\n", v);
                    break;
                }
                case OP_TYPE_LONGWORD: {
                    uint32_t temp_val;
                    memcpy(&temp_val, &piscsi_rom_ptr[romoffs - PIB], sizeof(temp_val));
                    v = be32toh(temp_val);
                    //DEBUG("%.8X\n", v);
                    break;
                }
            }
            return v;
        }
        return 0;
    }

    switch (addr & 0xFFFF) {
        case PISCSI_CMD_ADDR1: case PISCSI_CMD_ADDR2: case PISCSI_CMD_ADDR3: case PISCSI_CMD_ADDR4: {
            int i = ((addr & 0xFFFF) - PISCSI_CMD_ADDR1) / 4;
            return piscsi_u32[i];
            break;
        }
        case PISCSI_CMD_DRVTYPE:
            if (devs[piscsi_cur_drive].fd == -1) {
                DEBUG("[PISCSI] %s Read from DRVTYPE %d, drive not attached.\n", op_type_names[type], piscsi_cur_drive);
                return 0;
            }
            DEBUG("[PISCSI] %s Read from DRVTYPE %d, drive attached.\n", op_type_names[type], piscsi_cur_drive);
            {
                struct piscsi_dev *d = &devs[piscsi_cur_drive];
                uint8_t scsi_type = (d->media_kind == PISCSI_MEDIA_CDROM)
                                      ? PISCSI_SCSI_TYPE_CDROM
                                      : PISCSI_SCSI_TYPE_DIRECT_ACCESS;
                return PISCSI_DRVTYPE_BUILD(scsi_type, d->read_only);
            }
            break;
        case PISCSI_CMD_DRVNUM:
            return piscsi_cur_drive;
            break;
        case PISCSI_CMD_CYLS:
            DEBUG("[PISCSI] %s Read from CYLS %d: %d\n", op_type_names[type], piscsi_cur_drive, devs[piscsi_cur_drive].c);
            return devs[piscsi_cur_drive].c;
            break;
        case PISCSI_CMD_HEADS:
            DEBUG("[PISCSI] %s Read from HEADS %d: %d\n", op_type_names[type], piscsi_cur_drive, devs[piscsi_cur_drive].h);
            return devs[piscsi_cur_drive].h;
            break;
        case PISCSI_CMD_SECS:
            DEBUG("[PISCSI] %s Read from SECS %d: %d\n", op_type_names[type], piscsi_cur_drive, devs[piscsi_cur_drive].s);
            return devs[piscsi_cur_drive].s;
            break;
        case PISCSI_CMD_BLOCKS: {
            uint32_t blox = 0;
            uint64_t blocks64 = 0;
            if (devs[piscsi_cur_drive].block_size == 0) {
                return 0;
            }
            blocks64 = devs[piscsi_cur_drive].fs / devs[piscsi_cur_drive].block_size;
            blox = (blocks64 > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t)blocks64;
            DEBUG("[PISCSI] %s Read from BLOCKS %d: %u\n", op_type_names[type], piscsi_cur_drive, blox);
            DEBUG("fs: %llu (%u)\n", (unsigned long long)devs[piscsi_cur_drive].fs, blox);
            return blox;
            break;
        }
        case PISCSI_CMD_GETPART: {
            LOG_INFO("[PISCSI] GETPART: idx=%u offset=0x%08X\n",
                     rom_cur_partition, rom_partitions[rom_cur_partition]);
            return rom_partitions[rom_cur_partition];
            break;
        }
        case PISCSI_CMD_GETPRIO:
            LOG_INFO("[PISCSI] GETPRIO: idx=%u prio=%d\n",
                     rom_cur_partition, (int32_t)rom_partition_prio[rom_cur_partition]);
            return rom_partition_prio[rom_cur_partition];
            break;
        case PISCSI_CMD_CHECKFS:
            DEBUG("[PISCSI] Get current loaded file system: %.8X\n", filesystems[rom_cur_fs].FS_ID);
            return filesystems[rom_cur_fs].FS_ID;
        case PISCSI_CMD_FSSIZE:
            DEBUG("[PISCSI] Get alloc size of loaded file system: %d\n", filesystems[rom_cur_fs].h_info.alloc_size);
            return filesystems[rom_cur_fs].h_info.alloc_size;
        case PISCSI_CMD_BLOCKSIZE:
            DEBUG("[PISCSI] Get block size of drive %d: %d\n", piscsi_cur_drive, devs[piscsi_cur_drive].block_size);
            return devs[piscsi_cur_drive].block_size;
        case PISCSI_CMD_BACKEND_INFO: {
            const struct piscsi_dev *d = &devs[piscsi_cur_drive];
            uint32_t info = 0;
            uint32_t backend = (uint32_t)d->backend_type & PISCSI_BACKEND_INFO_TYPE_MASK;
            info |= backend;
            info |= (((uint32_t)d->media_kind << PISCSI_BACKEND_INFO_MEDIA_SHIFT) & PISCSI_BACKEND_INFO_MEDIA_MASK);
            if (d->backend_type == PISCSI_BACKEND_REMOTE) {
                info |= PISCSI_BACKEND_INFO_REMOTE;
            }
            DEBUG("[PISCSI] Get backend info of drive %d: 0x%08X\n", piscsi_cur_drive, info);
            return info;
        }
        case PISCSI_CMD_GET_FS_INFO: {
            int fs_idx = 0;
            uint32_t val = piscsi_u32[1];
            int32_t r = get_mapped_item_by_address(cfg, val);
            if (r != -1) {
#ifdef PISCSI_DEBUG
                char *dosID = (char *)&rom_partition_dostype[rom_cur_partition];
                DEBUG("[PISCSI-GET-FS-INFO] Partition DOSType is %c%c%c/%d\n", dosID[0], dosID[1], dosID[2], dosID[3]);
#endif
                // First try exact match
                for (fs_idx = 0; fs_idx < piscsi_num_fs; fs_idx++) {
                    if (rom_partition_dostype[rom_cur_partition] == filesystems[fs_idx].FS_ID) {
                        return 0;
                    }
                }

                // If no exact match, try fallback mappings (e.g., DOS/3 -> DOS/1 for FastFileSystem)
                uint32_t fallback_dostype = rom_partition_dostype[rom_cur_partition];

                // Map DOS/3 (FFS International) to DOS/1 (FFS) handler since they use the same filesystem
                if (fallback_dostype == 0x444F5303) { // DOS/3
                    fallback_dostype = 0x444F5301;   // DOS/1
                    for (fs_idx = 0; fs_idx < piscsi_num_fs; fs_idx++) {
                        if (fallback_dostype == filesystems[fs_idx].FS_ID) {
                            DEBUG("[PISCSI-GET-FS-INFO] Fallback: Mapped DOS/3 partition to DOS/1 filesystem handler.\n");
                            return 0;
                        }
                    }
                }
            }
            return 1;
        }
        default:
            DEBUG("[!!!PISCSI] WARN: Unhandled %s register read from %.8X\n", op_type_names[type], addr);
            break;
    }

    return 0;
}

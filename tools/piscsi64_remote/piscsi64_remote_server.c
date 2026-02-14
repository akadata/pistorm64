// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/fs.h>
#endif
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#ifndef htobe16
#include <endian.h>
#endif

#define PS64_REMOTE_DEFAULT_PORT 4964
#define PS64_REMOTE_MAGIC_HELLO 0x50533634u /* "PS64" */
#define PS64_REMOTE_MAGIC_IOREQ 0x50533640u /* "PS6@" */
#define PS64_REMOTE_MAGIC_IORSP 0x50533641u /* "PS6A" */
#define PS64_REMOTE_VERSION 1u

#define PS64_REMOTE_FLAG_REQ_RW  (1u << 0)
#define PS64_REMOTE_FLAG_HINT_CD (1u << 1)

#define PS64_REMOTE_STATUS_OK        0u
#define PS64_REMOTE_STATUS_AUTH      1u
#define PS64_REMOTE_STATUS_EXPORT    2u
#define PS64_REMOTE_STATUS_OPEN      3u
#define PS64_REMOTE_STATUS_BADREQ    4u

#define PS64_REMOTE_OP_READ  1u
#define PS64_REMOTE_OP_WRITE 2u
#define PS64_REMOTE_OP_SYNC  3u
#define PS64_REMOTE_OP_CLOSE 4u
#define PS64_REMOTE_OP_PING  5u

#define PS64_MEDIA_DISK  1u
#define PS64_MEDIA_CDROM 2u

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

typedef struct server_cfg {
    char listen_host[128];
    uint16_t listen_port;
    char export_name[128];
    char path[512];
    char token[128];
    int read_only;
    uint8_t media_kind;
    uint32_t block_size;
} server_cfg_t;

static volatile sig_atomic_t g_stop = 0;

static uint64_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000ull) + (uint64_t)(ts.tv_nsec / 1000000ull);
}

static void peer_to_string(int fd, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';

    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    if (getpeername(fd, (struct sockaddr *)&ss, &slen) != 0) {
        snprintf(out, out_len, "unknown");
        return;
    }

    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    if (getnameinfo((struct sockaddr *)&ss, slen,
                    host, sizeof(host),
                    serv, sizeof(serv),
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        snprintf(out, out_len, "unknown");
        return;
    }
    size_t pos = 0;
    int host_has_colon = strchr(host, ':') != NULL;
    if (host_has_colon && (pos + 1) < out_len) {
        out[pos++] = '[';
    }
    const char *src = host;
    while (*src && (pos + 1) < out_len) {
        out[pos++] = *src++;
    }
    if (host_has_colon && (pos + 1) < out_len) {
        out[pos++] = ']';
    }
    if ((pos + 1) < out_len) {
        out[pos++] = ':';
    }
    src = serv;
    while (*src && (pos + 1) < out_len) {
        out[pos++] = *src++;
    }
    out[pos] = '\0';
}

static unsigned int tls_psk_server_cb(SSL *ssl, const char *identity,
                                      unsigned char *psk, unsigned int max_psk_len)
{
    const server_cfg_t *cfg = (const server_cfg_t *)SSL_get_app_data(ssl);
    if (!cfg || !identity || !cfg->token[0]) {
        return 0;
    }
    if (strcmp(identity, cfg->export_name) != 0) {
        return 0;
    }

    size_t token_len = strlen(cfg->token);
    if (token_len == 0 || token_len > max_psk_len) {
        return 0;
    }
    memcpy(psk, cfg->token, token_len);
    return (unsigned int)token_len;
}

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
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

static int send_hello_rsp(SSL *ssl, uint16_t status, uint64_t size_bytes,
                          uint32_t block_size, uint8_t media_kind, uint8_t read_only,
                          const uint8_t server_nonce[16])
{
    ps64_hello_rsp_t rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.magic_be = htobe32(PS64_REMOTE_MAGIC_HELLO);
    rsp.version_be = htobe16(PS64_REMOTE_VERSION);
    rsp.status_be = htobe16(status);
    rsp.size_bytes_be = htobe64(size_bytes);
    rsp.block_size_be = htobe32(block_size);
    rsp.media_kind = media_kind;
    rsp.read_only = read_only;
    if (server_nonce) {
        memcpy(rsp.server_nonce, server_nonce, 16);
    }
    return tls_send_all(ssl, &rsp, sizeof(rsp));
}

static int send_io_rsp(SSL *ssl, uint16_t status, uint32_t length, int32_t err)
{
    ps64_io_rsp_t rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.magic_be = htobe32(PS64_REMOTE_MAGIC_IORSP);
    rsp.version_be = htobe16(PS64_REMOTE_VERSION);
    rsp.status_be = htobe16(status);
    rsp.length_be = htobe32(length);
    rsp.err_be = (int32_t)htobe32((uint32_t)err);
    return tls_send_all(ssl, &rsp, sizeof(rsp));
}

static uint64_t detect_size_bytes(int fd)
{
    off_t end = lseek(fd, 0, SEEK_END);
    if (end >= 0) {
        (void)lseek(fd, 0, SEEK_SET);
        return (uint64_t)end;
    }
#ifdef __linux__
    uint64_t bytes = 0;
    if (ioctl(fd, BLKGETSIZE64, &bytes) == 0) {
        return bytes;
    }
#endif
    return 0;
}

static int parse_port_u16(const char *s, uint16_t *port_out)
{
    if (!s || !*s || !port_out) {
        return -1;
    }
    char *endp = NULL;
    unsigned long p = strtoul(s, &endp, 10);
    if (!endp || *endp != '\0' || p == 0 || p > 65535ul) {
        return -1;
    }
    *port_out = (uint16_t)p;
    return 0;
}

static int parse_host_port(const char *in, char *host, size_t host_len, uint16_t *port)
{
    if (!in || !host || !port) {
        return -1;
    }

    char tmp[256];
    size_t in_len = strlen(in);
    if (in_len == 0 || in_len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, in, in_len + 1);
    *port = PS64_REMOTE_DEFAULT_PORT;

    if (tmp[0] == '[') {
        char *rb = strchr(tmp + 1, ']');
        if (!rb) {
            return -1;
        }
        *rb = '\0';
        size_t host_part_len = strlen(tmp + 1);
        if (host_part_len == 0 || host_part_len >= host_len) {
            return -1;
        }
        memcpy(host, tmp + 1, host_part_len + 1);
        if (rb[1] == '\0') {
            return 0;
        }
        if (rb[1] != ':') {
            return -1;
        }
        return parse_port_u16(rb + 2, port);
    }

    int colon_count = 0;
    for (char *p = tmp; *p; ++p) {
        if (*p == ':') {
            colon_count++;
        }
    }
    if (colon_count == 1) {
        char *colon = strrchr(tmp, ':');
        *colon = '\0';
        if (parse_port_u16(colon + 1, port) != 0) {
            return -1;
        }
    } else if (colon_count > 1) {
        /* Bare IPv6 literal with default port. */
    }

    size_t host_part_len = strlen(tmp);
    if (host_part_len == 0 || host_part_len >= host_len) {
        return -1;
    }
    memcpy(host, tmp, host_part_len + 1);
    return 0;
}

static int parse_args(int argc, char **argv, server_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->listen_host, sizeof(cfg->listen_host), "%s", "0.0.0.0");
    cfg->listen_port = PS64_REMOTE_DEFAULT_PORT;
    cfg->read_only = 1;
    cfg->media_kind = PS64_MEDIA_DISK;
    cfg->block_size = 512;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            if (parse_host_port(argv[++i], cfg->listen_host, sizeof(cfg->listen_host), &cfg->listen_port) != 0) {
                return -1;
            }
        } else if (strcmp(argv[i], "--export") == 0 && i + 1 < argc) {
            snprintf(cfg->export_name, sizeof(cfg->export_name), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            snprintf(cfg->path, sizeof(cfg->path), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
            snprintf(cfg->token, sizeof(cfg->token), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            const char *m = argv[++i];
            if (strcasecmp(m, "rw") == 0) {
                cfg->read_only = 0;
            } else if (strcasecmp(m, "ro") == 0) {
                cfg->read_only = 1;
            } else {
                return -1;
            }
        } else if (strcmp(argv[i], "--kind") == 0 && i + 1 < argc) {
            const char *k = argv[++i];
            if (strcasecmp(k, "cdrom") == 0) {
                cfg->media_kind = PS64_MEDIA_CDROM;
                cfg->block_size = 2048;
                cfg->read_only = 1;
            } else if (strcasecmp(k, "disk") == 0) {
                cfg->media_kind = PS64_MEDIA_DISK;
            } else {
                return -1;
            }
        } else if (strcmp(argv[i], "--block-size") == 0 && i + 1 < argc) {
            long b = strtol(argv[++i], NULL, 10);
            if (b <= 0 || b > 65536) {
                return -1;
            }
            cfg->block_size = (uint32_t)b;
        } else {
            return -1;
        }
    }

    if (cfg->path[0] == '\0' || cfg->export_name[0] == '\0' || cfg->token[0] == '\0') {
        return -1;
    }

    if (cfg->media_kind == PS64_MEDIA_CDROM) {
        cfg->read_only = 1;
        if (cfg->block_size == 0) {
            cfg->block_size = 2048;
        }
    }
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s --export NAME --path PATH --token TOKEN [--listen HOST[:PORT]|[IPv6]:PORT] [--mode ro|rw] [--kind disk|cdrom] [--block-size N]\n",
            argv0);
}

static int handle_client(int cfd, const server_cfg_t *cfg, SSL_CTX *tls_ctx)
{
    uint8_t server_nonce[16];
    uint64_t conn_start_ms = now_ms();
    uint64_t read_ops = 0;
    uint64_t write_ops = 0;
    uint64_t ping_ops = 0;
    uint64_t read_bytes = 0;
    uint64_t write_bytes = 0;
    char peer[128];
    peer_to_string(cfd, peer, sizeof(peer));

    SSL *ssl = SSL_new(tls_ctx);
    if (!ssl) {
        return -1;
    }
    SSL_set_fd(ssl, cfd);
    SSL_set_app_data(ssl, (void *)cfg);
    if (SSL_accept(ssl) <= 0) {
        SSL_free(ssl);
        return -1;
    }
    {
        int bits = SSL_get_cipher_bits(ssl, NULL);
        const char *tls_ver = SSL_get_version(ssl);
        const char *tls_cipher = SSL_get_cipher_name(ssl);
        printf("[piscsi64-remote] tls client=%s version=%s cipher=%s bits=%d\n",
               peer,
               tls_ver ? tls_ver : "unknown",
               tls_cipher ? tls_cipher : "unknown",
               bits);
    }

    ps64_hello_req_t hello;
    if (tls_recv_all(ssl, &hello, sizeof(hello)) != 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return -1;
    }
    if (be32toh(hello.magic_be) != PS64_REMOTE_MAGIC_HELLO ||
        be16toh(hello.version_be) != PS64_REMOTE_VERSION) {
        (void)send_hello_rsp(ssl, PS64_REMOTE_STATUS_BADREQ, 0, 0, 0, 1, NULL);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return -1;
    }

    uint16_t flags = be16toh(hello.flags_be);
    uint16_t token_len = be16toh(hello.token_len_be);
    uint16_t export_len = be16toh(hello.export_len_be);
    if (token_len > 127 || export_len > 127) {
        (void)send_hello_rsp(ssl, PS64_REMOTE_STATUS_BADREQ, 0, 0, 0, 1, NULL);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return -1;
    }

    char export_name[128];
    char token_unused[128];
    memset(token_unused, 0, sizeof(token_unused));
    memset(export_name, 0, sizeof(export_name));
    if (token_len && tls_recv_all(ssl, token_unused, token_len) != 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return -1;
    }
    if (export_len && tls_recv_all(ssl, export_name, export_len) != 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return -1;
    }

    if (token_len != 0) {
        (void)send_hello_rsp(ssl, PS64_REMOTE_STATUS_BADREQ, 0, 0, 0, 1, NULL);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return -1;
    }
    if (strcmp(cfg->export_name, export_name) != 0) {
        (void)send_hello_rsp(ssl, PS64_REMOTE_STATUS_EXPORT, 0, 0, 0, 1, NULL);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return -1;
    }
    (void)RAND_bytes(server_nonce, sizeof(server_nonce));

    int can_write = (!cfg->read_only && (flags & PS64_REMOTE_FLAG_REQ_RW));
    int oflags = can_write ? O_RDWR : O_RDONLY;
    int fd = open(cfg->path, oflags);
    if (fd < 0) {
        (void)send_hello_rsp(ssl, PS64_REMOTE_STATUS_OPEN, 0, 0, 0, 1, NULL);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return -1;
    }

    uint64_t size_bytes = detect_size_bytes(fd);
    uint8_t ro = can_write ? 0u : 1u;
    if (send_hello_rsp(ssl, PS64_REMOTE_STATUS_OK, size_bytes, cfg->block_size, cfg->media_kind, ro,
                       server_nonce) != 0) {
        close(fd);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return -1;
    }
    printf("[piscsi64-remote] client=%s export=%s mode=%s kind=%s block=%u size=%llu\n",
           peer,
           cfg->export_name,
           ro ? "ro" : "rw",
           (cfg->media_kind == PS64_MEDIA_CDROM) ? "cdrom" : "disk",
           cfg->block_size,
           (unsigned long long)size_bytes);

    uint8_t *buf = NULL;
    size_t buf_cap = 0;

    while (!g_stop) {
        ps64_io_req_t req;
        if (tls_recv_all(ssl, &req, sizeof(req)) != 0) {
            break;
        }
        if (be32toh(req.magic_be) != PS64_REMOTE_MAGIC_IOREQ ||
            be16toh(req.version_be) != PS64_REMOTE_VERSION) {
            (void)send_io_rsp(ssl, 1, 0, EPROTO);
            break;
        }

        uint16_t op = be16toh(req.op_be);
        uint64_t offset = be64toh(req.offset_be);
        uint32_t length = be32toh(req.length_be);

        if (op == PS64_REMOTE_OP_PING) {
            ping_ops++;
            if (send_io_rsp(ssl, 0, 0, 0) != 0) {
                break;
            }
            continue;
        }

        if (op == PS64_REMOTE_OP_CLOSE) {
            (void)send_io_rsp(ssl, 0, 0, 0);
            break;
        }

        if (op == PS64_REMOTE_OP_SYNC) {
            int rc = fsync(fd);
            if (rc != 0) {
                if (send_io_rsp(ssl, 1, 0, errno) != 0) {
                    break;
                }
            } else {
                if (send_io_rsp(ssl, 0, 0, 0) != 0) {
                    break;
                }
            }
            continue;
        }

        if ((uint64_t)length > size_bytes || offset > size_bytes || offset + (uint64_t)length > size_bytes) {
            if (send_io_rsp(ssl, 1, 0, EINVAL) != 0) {
                break;
            }
            continue;
        }

        if (length > buf_cap) {
            uint8_t *nb = realloc(buf, length);
            if (!nb) {
                if (send_io_rsp(ssl, 1, 0, ENOMEM) != 0) {
                    break;
                }
                continue;
            }
            buf = nb;
            buf_cap = length;
        }

        if (op == PS64_REMOTE_OP_READ) {
            ssize_t r = pread(fd, buf, length, (off_t)offset);
            if (r < 0) {
                fprintf(stderr,
                        "[piscsi64-remote] READ error client=%s off=0x%llX len=%u errno=%d\n",
                        peer, (unsigned long long)offset, (unsigned int)length, errno);
                if (send_io_rsp(ssl, 1, 0, errno) != 0) {
                    break;
                }
                continue;
            }
            if ((uint32_t)r != length) {
                if (send_io_rsp(ssl, 1, (uint32_t)r, EIO) != 0) {
                    break;
                }
                continue;
            }
            if (send_io_rsp(ssl, 0, length, 0) != 0 || tls_send_all(ssl, buf, length) != 0) {
                break;
            }
            read_ops++;
            read_bytes += (uint64_t)length;
            continue;
        }

        if (op == PS64_REMOTE_OP_WRITE) {
            if (ro) {
                if (send_io_rsp(ssl, 1, 0, EROFS) != 0) {
                    break;
                }
                if (length) {
                    // drain payload
                    if (tls_recv_all(ssl, buf, length) != 0) {
                        break;
                    }
                }
                continue;
            }

            if (length && tls_recv_all(ssl, buf, length) != 0) {
                break;
            }
            ssize_t w = pwrite(fd, buf, length, (off_t)offset);
            if (w < 0) {
                fprintf(stderr,
                        "[piscsi64-remote] WRITE error client=%s off=0x%llX len=%u errno=%d\n",
                        peer, (unsigned long long)offset, (unsigned int)length, errno);
                if (send_io_rsp(ssl, 1, 0, errno) != 0) {
                    break;
                }
                continue;
            }
            if ((uint32_t)w != length) {
                if (send_io_rsp(ssl, 1, (uint32_t)w, EIO) != 0) {
                    break;
                }
                continue;
            }
            if (send_io_rsp(ssl, 0, length, 0) != 0) {
                break;
            }
            write_ops++;
            write_bytes += (uint64_t)length;
            continue;
        }

        if (send_io_rsp(ssl, 1, 0, EINVAL) != 0) {
            break;
        }
    }

    free(buf);
    close(fd);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    uint64_t conn_end_ms = now_ms();
    uint64_t conn_ms = (conn_end_ms >= conn_start_ms) ? (conn_end_ms - conn_start_ms) : 0;
    printf("[piscsi64-remote] disconnect client=%s ms=%llu reads=%llu/%lluB writes=%llu/%lluB pings=%llu\n",
           peer,
           (unsigned long long)conn_ms,
           (unsigned long long)read_ops,
           (unsigned long long)read_bytes,
           (unsigned long long)write_ops,
           (unsigned long long)write_bytes,
           (unsigned long long)ping_ops);
    return 0;
}

int main(int argc, char **argv)
{
    server_cfg_t cfg;
    if (parse_args(argc, argv, &cfg) != 0) {
        usage(argv[0]);
        return 1;
    }

    SSL_CTX *tls_ctx = SSL_CTX_new(TLS_server_method());
    if (!tls_ctx) {
        fprintf(stderr, "TLS context init failed.\n");
        return 1;
    }
    if (SSL_CTX_set_min_proto_version(tls_ctx, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(tls_ctx, TLS1_2_VERSION) != 1) {
        fprintf(stderr, "TLS version setup failed.\n");
        SSL_CTX_free(tls_ctx);
        return 1;
    }
    if (SSL_CTX_set_cipher_list(tls_ctx, "PSK-AES256-GCM-SHA384:PSK-AES128-GCM-SHA256") != 1) {
        fprintf(stderr, "TLS PSK cipher setup failed.\n");
        SSL_CTX_free(tls_ctx);
        return 1;
    }
    SSL_CTX_set_psk_server_callback(tls_ctx, tls_psk_server_cb);

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    char port_s[16];
    snprintf(port_s, sizeof(port_s), "%u", (unsigned int)cfg.listen_port);
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int gai = getaddrinfo(cfg.listen_host, port_s, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(gai));
        return 1;
    }

    int lfd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        lfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (lfd < 0) {
            continue;
        }
        int one = 1;
        (void)setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (bind(lfd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        close(lfd);
        lfd = -1;
    }
    freeaddrinfo(res);

    if (lfd < 0) {
        perror("bind");
        return 1;
    }
    if (listen(lfd, 8) != 0) {
        perror("listen");
        close(lfd);
        return 1;
    }

    int host_has_colon = strchr(cfg.listen_host, ':') != NULL;
    printf("[piscsi64-remote] listen=%s%s%s:%u export=%s path=%s mode=%s kind=%s block=%u\n",
           host_has_colon ? "[" : "",
           cfg.listen_host,
           host_has_colon ? "]" : "",
           (unsigned int)cfg.listen_port,
           cfg.export_name,
           cfg.path,
           cfg.read_only ? "ro" : "rw",
           (cfg.media_kind == PS64_MEDIA_CDROM) ? "cdrom" : "disk",
           cfg.block_size);

    while (!g_stop) {
        struct pollfd pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = lfd;
        pfd.events = POLLIN;

        int prc = poll(&pfd, 1, 250);
        if (prc < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }
        if (prc == 0 || !(pfd.revents & POLLIN)) {
            continue;
        }

        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int cfd = accept(lfd, (struct sockaddr *)&ss, &slen);
        if (cfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }
        (void)handle_client(cfd, &cfg, tls_ctx);
        close(cfd);
    }

    close(lfd);
    SSL_CTX_free(tls_ctx);
    return 0;
}
